#include "compiler/codegen/functions.h"
#include "compiler/codegen/statements.h"
#include "compiler/codegen/expressions.h"
#include "compiler/codegen/modules.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/register_allocator.h"
#include "compiler/symbol_table.h"
#include "compiler/scope_stack.h"
#include "compiler/error_reporter.h"
#include "type/type.h"
#include "vm/vm.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "debug/debug_config.h"
#include <stdlib.h>
#include <string.h>

void finalize_functions_to_vm(CompilerContext* ctx) {
    if (!ctx) return;
    
    extern VM vm; // Access global VM instance
    
    DEBUG_CODEGEN_PRINT("Finalizing %d functions to VM\n", ctx->function_count);
    
    for (int i = 0; i < ctx->function_count; i++) {
        if (vm.functionCount >= UINT8_COUNT) {
            DEBUG_CODEGEN_PRINT("Error: VM function array full\n");
            break;
        }
        
        BytecodeBuffer* func_chunk = ctx->function_chunks[i];
        if (!func_chunk) continue;
        
        // Create a Chunk from BytecodeBuffer
        Chunk* chunk = malloc(sizeof(Chunk));
        if (!chunk) continue;

        // Initialize chunk with function bytecode
        chunk->code = malloc(func_chunk->count);
        if (!chunk->code) {
            free(chunk);
            continue;
        }

        memcpy(chunk->code, func_chunk->instructions, func_chunk->count);
        chunk->count = func_chunk->count;
        chunk->capacity = func_chunk->count;
        chunk->lines = func_chunk->count > 0 ? malloc(sizeof(int) * func_chunk->count) : NULL;
        chunk->columns = func_chunk->count > 0 ? malloc(sizeof(int) * func_chunk->count) : NULL;
        chunk->files = func_chunk->count > 0 ? malloc(sizeof(const char*) * func_chunk->count) : NULL;
        if (chunk->lines && func_chunk->source_lines) {
            memcpy(chunk->lines, func_chunk->source_lines, sizeof(int) * func_chunk->count);
        }
        if (chunk->columns && func_chunk->source_columns) {
            memcpy(chunk->columns, func_chunk->source_columns, sizeof(int) * func_chunk->count);
        }
        if (chunk->files && func_chunk->source_files) {
            memcpy(chunk->files, func_chunk->source_files, sizeof(const char*) * func_chunk->count);
        } else if (chunk->files) {
            for (int j = 0; j < func_chunk->count; ++j) {
                chunk->files[j] = NULL;
            }
        }
        // Copy constants from main context
        if (ctx->constants && ctx->constants->count > 0) {
            chunk->constants.count = ctx->constants->count;
            chunk->constants.capacity = ctx->constants->capacity;
            chunk->constants.values = malloc(sizeof(Value) * chunk->constants.capacity);
            if (chunk->constants.values) {
                memcpy(chunk->constants.values, ctx->constants->values, sizeof(Value) * ctx->constants->count);
            } else {
                // Fallback to empty constants if allocation fails
                chunk->constants.count = 0;
                chunk->constants.capacity = 0;
            }
        } else {
            chunk->constants.values = NULL;
            chunk->constants.count = 0;
            chunk->constants.capacity = 0;
        }
        
        // Register function in VM
        Function* vm_function = &vm.functions[vm.functionCount];
        vm_function->start = 0; // Always start at beginning of chunk
        vm_function->arity = ctx->function_arities[i]; // Use stored arity
        vm_function->chunk = chunk;
        
        DEBUG_CODEGEN_PRINT("Added function %d to VM (index %d)\n", i, vm.functionCount);
        vm.functionCount++;
    }
}

// ====== FUNCTION COMPILATION IMPLEMENTATION ======

// Compile a function declaration or expression and return a register
// containing the function index. Closures and upvalues are not yet
// supported for anonymous functions.
int compile_function_declaration(CompilerContext* ctx, TypedASTNode* func) {
    if (!ctx || !func || !func->original) return -1;

    const char* func_name = func->original->function.name;
    const char* method_struct = func->original->function.methodStructName;
    bool is_method = func->original->function.isMethod;
    int arity = func->original->function.paramCount;

    DEBUG_CODEGEN_PRINT("Compiling function declaration: %s\n",
           func_name ? func_name : "(anonymous)");

    Type* function_type = func->resolvedType ? func->resolvedType : getPrimitiveType(TYPE_FUNCTION);

    int func_reg;
    if (func_name) {
        func_reg = ctx->compiling_function ?
            compiler_alloc_frame(ctx->allocator) :
            compiler_alloc_global(ctx->allocator);
        if (func_reg == -1) return -1;
        if (!register_variable(ctx, ctx->symbols, func_name, func_reg,
                               function_type, false, false,
                               func->original->location, true)) {
            return -1;
        }
        if (!ctx->compiling_function && ctx->is_module &&
            func->original->function.isPublic && !func->original->function.isMethod && func_name) {
            set_module_export_metadata(ctx, func_name, func_reg, function_type);
        }
        if (is_method && method_struct) {
            char* alias_name = create_method_symbol_name(method_struct, func_name);
            if (!alias_name) {
                return -1;
            }
            if (!register_variable(ctx, ctx->symbols, alias_name, func_reg,
                                   function_type, false, false,
                                   func->original->location, true)) {
                free(alias_name);
                return -1;
            }
            free(alias_name);
        }
        compiler_reset_frame_registers(ctx->allocator);
    } else {
        func_reg = compiler_alloc_temp(ctx->allocator);
        if (func_reg == -1) return -1;
    }

    BytecodeBuffer* function_bytecode = init_bytecode_buffer();
    if (!function_bytecode) return -1;

    // Save outer compilation state
    BytecodeBuffer* saved_bytecode = ctx->bytecode;
    SymbolTable* old_scope = ctx->symbols;
    bool old_compiling_function = ctx->compiling_function;
    int saved_function_scope_depth = ctx->function_scope_depth;

    // Switch to function compilation context
    ctx->bytecode = function_bytecode;
    ctx->symbols = create_symbol_table(old_scope);
    ctx->compiling_function = true;
    ctx->function_scope_depth = ctx->symbols->scope_depth;

    // Make function name visible inside its own body for recursion
    if (func_name) {
        if (!register_variable(ctx, ctx->symbols, func_name, func_reg,
                               function_type, false, false,
                               func->original->location, true)) {
            ctx->has_compilation_errors = true;
            return -1;
        }
    }

    // Register parameters
    int param_base = FRAME_REG_START + FRAME_REGISTERS - arity;
    if (param_base < FRAME_REG_START) param_base = FRAME_REG_START;
    for (int i = 0; i < arity; i++) {
        if (func->original->function.params[i].name) {
            int param_reg = param_base + i;
            if (!register_variable(ctx, ctx->symbols,
                                   func->original->function.params[i].name,
                                   param_reg, getPrimitiveType(TYPE_I32), false, false,
                                   func->original->location, true)) {
                ctx->has_compilation_errors = true;
                return -1;
            }
        }
    }

    // Compile function body
    if (func->typed.function.body) {
        if (func->typed.function.body->original->type == NODE_BLOCK) {
            for (int i = 0; i < func->typed.function.body->typed.block.count; i++) {
                TypedASTNode* stmt = func->typed.function.body->typed.block.statements[i];
                if (stmt) compile_statement(ctx, stmt);
            }
        } else {
            compile_statement(ctx, func->typed.function.body);
        }
    }

    // Ensure function ends with return
    if (function_bytecode->count == 0 ||
        (function_bytecode->count >= 2 &&
         function_bytecode->instructions[function_bytecode->count - 2] != OP_RETURN_R)) {
        emit_byte_to_buffer(function_bytecode, OP_RETURN_VOID);
    }

    // Restore outer compilation state
    ctx->bytecode = saved_bytecode;
    free_symbol_table(ctx->symbols);
    ctx->symbols = old_scope;
    ctx->compiling_function = old_compiling_function;
    ctx->function_scope_depth = saved_function_scope_depth;

    // Register function for VM finalization and get index
    const char* debug_name = func_name ? func_name : "(lambda)";
    char* mangled_debug = NULL;
    if (is_method && method_struct && func_name) {
        mangled_debug = create_method_symbol_name(method_struct, func_name);
        if (mangled_debug) {
            debug_name = mangled_debug;
        }
    }

    int function_index = register_function(ctx, debug_name, arity, function_bytecode);
    if (function_index < 0) {
        if (mangled_debug) {
            free(mangled_debug);
        }
        free_bytecode_buffer(function_bytecode);
        return -1;
    }

    if (mangled_debug) {
        free(mangled_debug);
    }

    // Load function index into target register
    emit_load_constant(ctx, func_reg, I32_VAL(function_index));
    return func_reg;
}

// Compile a return statement
void compile_return_statement(CompilerContext* ctx, TypedASTNode* ret) {
    if (!ctx || !ret || !ret->original) return;
    
    DEBUG_CODEGEN_PRINT("Compiling return statement\n");
    
    if (ret->original->returnStmt.value) {
        // Return with value
        int value_reg = compile_expression(ctx, ret->typed.returnStmt.value);
        if (value_reg == -1) {
            DEBUG_CODEGEN_PRINT("Error: Failed to compile return value\n");
            return;
        }

        // Emit OP_RETURN_R with value register
        set_location_from_node(ctx, ret);
        emit_byte_to_buffer(ctx->bytecode, OP_RETURN_R);
        emit_byte_to_buffer(ctx->bytecode, value_reg);
        
        DEBUG_CODEGEN_PRINT("Emitted OP_RETURN_R R%d\n", value_reg);
        
        // Free value register if it's temporary
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
    } else {
        // Return void
        set_location_from_node(ctx, ret);
        emit_byte_to_buffer(ctx->bytecode, OP_RETURN_VOID);
        DEBUG_CODEGEN_PRINT("Emitted OP_RETURN_VOID\n");
    }
}
