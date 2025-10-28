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
#include "vm/vm_tiering.h"
#include "internal/strutil.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "debug/debug_config.h"
#include <stdlib.h>
#include <string.h>

static bool type_is_void_like(Type* type) {
    if (!type) {
        return true;
    }

    Type* resolved = prune(type);
    if (!resolved) {
        return true;
    }

    return resolved->kind == TYPE_VOID || resolved->kind == TYPE_UNKNOWN ||
           resolved->kind == TYPE_ERROR;
}

static Chunk* materialize_chunk_from_buffer(CompilerContext* ctx, BytecodeBuffer* buffer) {
    if (!buffer) {
        return NULL;
    }

    Chunk* chunk = malloc(sizeof(Chunk));
    if (!chunk) {
        return NULL;
    }

    memset(chunk, 0, sizeof(Chunk));

    chunk->count = buffer->count;
    chunk->capacity = buffer->count;
    chunk->code = buffer->count > 0 ? malloc(buffer->count) : NULL;
    if (buffer->count > 0 && (!chunk->code || !buffer->instructions)) {
        free(chunk->code);
        free(chunk);
        return NULL;
    }
    if (chunk->code && buffer->instructions) {
        memcpy(chunk->code, buffer->instructions, buffer->count);
    }

    chunk->lines = buffer->count > 0 ? malloc(sizeof(int) * buffer->count) : NULL;
    chunk->columns = buffer->count > 0 ? malloc(sizeof(int) * buffer->count) : NULL;
    chunk->files = buffer->count > 0 ? malloc(sizeof(const char*) * buffer->count) : NULL;

    if (chunk->lines && buffer->source_lines) {
        memcpy(chunk->lines, buffer->source_lines, sizeof(int) * buffer->count);
    }
    if (chunk->columns && buffer->source_columns) {
        memcpy(chunk->columns, buffer->source_columns, sizeof(int) * buffer->count);
    }
    if (chunk->files && buffer->source_files) {
        memcpy(chunk->files, buffer->source_files, sizeof(const char*) * buffer->count);
    } else if (chunk->files) {
        for (int j = 0; j < buffer->count; ++j) {
            chunk->files[j] = NULL;
        }
    }

    if (ctx && ctx->constants && ctx->constants->count > 0) {
        chunk->constants.count = ctx->constants->count;
        chunk->constants.capacity = ctx->constants->capacity;
        chunk->constants.values = malloc(sizeof(Value) * chunk->constants.capacity);
        if (chunk->constants.values) {
            memcpy(chunk->constants.values, ctx->constants->values,
                   sizeof(Value) * ctx->constants->count);
        } else {
            chunk->constants.count = 0;
            chunk->constants.capacity = 0;
        }
    } else {
        chunk->constants.values = NULL;
        chunk->constants.count = 0;
        chunk->constants.capacity = 0;
    }

    return chunk;
}

static bool node_is_expression_type(NodeType type) {
    switch (type) {
        case NODE_IDENTIFIER:
        case NODE_LITERAL:
        case NODE_ARRAY_LITERAL:
        case NODE_ARRAY_FILL:
        case NODE_ARRAY_SLICE:
        case NODE_INDEX_ACCESS:
        case NODE_BINARY:
        case NODE_TERNARY:
        case NODE_UNARY:
        case NODE_CALL:
        case NODE_CAST:
        case NODE_STRUCT_LITERAL:
        case NODE_MEMBER_ACCESS:
        case NODE_ENUM_MATCH_TEST:
        case NODE_ENUM_PAYLOAD:
        case NODE_MATCH_EXPRESSION:
        case NODE_TIME_STAMP:
        case NODE_TYPE:
            return true;
        default:
            return false;
    }
}

static bool emit_return_from_register(CompilerContext* ctx, TypedASTNode* origin, int value_reg) {
    if (!ctx || value_reg == -1) {
        return false;
    }

    set_location_from_node(ctx, origin);
    emit_byte_to_buffer(ctx->bytecode, OP_RETURN_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)value_reg);

    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    return true;
}

static bool ensure_statement_terminates_with_return(CompilerContext* ctx,
                                                   TypedASTNode* stmt,
                                                   Type* return_type);

static bool emit_implicit_return_from_expression(CompilerContext* ctx,
                                                 TypedASTNode* expr) {
    if (!ctx || !expr) {
        return false;
    }

    int value_reg = compile_expression(ctx, expr);
    if (value_reg == -1) {
        return false;
    }

    return emit_return_from_register(ctx, expr, value_reg);
}

static bool emit_branch_return(CompilerContext* ctx,
                               TypedASTNode* branch,
                               Type* return_type);

static bool emit_implicit_return_from_block(CompilerContext* ctx,
                                            TypedASTNode* block,
                                            Type* return_type) {
    if (!ctx || !block || !block->original ||
        block->original->type != NODE_BLOCK) {
        return false;
    }

    bool creates_scope = block->original->block.createsScope;
    SymbolTable* saved_scope = ctx->symbols;
    ScopeFrame* lexical_frame = NULL;
    int lexical_frame_index = -1;

    if (creates_scope) {
        ctx->symbols = create_symbol_table(saved_scope);
        if (!ctx->symbols) {
            ctx->symbols = saved_scope;
            return false;
        }

        if (ctx->allocator) {
            compiler_enter_scope(ctx->allocator);
        }

        if (ctx->scopes) {
            lexical_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
            if (lexical_frame) {
                lexical_frame->symbols = ctx->symbols;
                lexical_frame->start_offset = ctx->bytecode ? ctx->bytecode->count : 0;
                lexical_frame->end_offset = lexical_frame->start_offset;
                lexical_frame_index = lexical_frame->lexical_depth;
            }
        }
    }

    bool success = false;
    int count = block->typed.block.count;
    for (int i = 0; i < count; i++) {
        TypedASTNode* nested = block->typed.block.statements[i];
        if (!nested) {
            continue;
        }

        bool is_last = (i == count - 1);
        if (!is_last) {
            compile_statement(ctx, nested);
            continue;
        }

        if (ensure_statement_terminates_with_return(ctx, nested, return_type)) {
            success = true;
        } else {
            compile_statement(ctx, nested);
            if (nested->original && nested->original->type == NODE_RETURN) {
                success = true;
            }
        }
    }

    if (creates_scope) {
        if (ctx->symbols) {
            for (int i = 0; i < ctx->symbols->capacity; i++) {
                Symbol* symbol = ctx->symbols->symbols[i];
                while (symbol) {
                    if (symbol->legacy_register_id >= MP_FRAME_REG_START &&
                        symbol->legacy_register_id <= MP_FRAME_REG_END) {
                        compiler_free_register(ctx->allocator, symbol->legacy_register_id);
                    }
                    symbol = symbol->next;
                }
            }
        }

        if (lexical_frame) {
            ScopeFrame* refreshed = get_scope_frame_by_index(ctx, lexical_frame_index);
            if (refreshed) {
                refreshed->end_offset = ctx->bytecode ? ctx->bytecode->count : lexical_frame->start_offset;
            }
            if (ctx->scopes) {
                scope_stack_pop(ctx->scopes);
            }
        }

        if (ctx->allocator) {
            compiler_exit_scope(ctx->allocator);
        }

        free_symbol_table(ctx->symbols);
        ctx->symbols = saved_scope;
    }

    return success;
}

static bool emit_implicit_return_from_if(CompilerContext* ctx,
                                         TypedASTNode* if_stmt,
                                         Type* return_type) {
    if (!ctx || !if_stmt || !if_stmt->original ||
        if_stmt->original->type != NODE_IF ||
        !if_stmt->typed.ifStmt.condition ||
        !if_stmt->typed.ifStmt.elseBranch) {
        return false;
    }

    int condition_reg = compile_expression(ctx, if_stmt->typed.ifStmt.condition);
    if (condition_reg == -1) {
        return false;
    }

    set_location_from_node(ctx, if_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)condition_reg);
    int else_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_R);
    if (else_patch < 0) {
        if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, condition_reg);
        }
        return false;
    }

    if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, condition_reg);
    }

    if (!emit_branch_return(ctx, if_stmt->typed.ifStmt.thenBranch, return_type)) {
        return false;
    }

    int else_target = ctx->bytecode ? ctx->bytecode->count : 0;
    if (!patch_jump(ctx->bytecode, else_patch, else_target)) {
        return false;
    }

    return emit_branch_return(ctx, if_stmt->typed.ifStmt.elseBranch, return_type);
}

static bool emit_branch_return(CompilerContext* ctx,
                               TypedASTNode* branch,
                               Type* return_type) {
    if (!ctx || !branch || !branch->original) {
        return false;
    }

    if (branch->original->type == NODE_BLOCK) {
        return emit_implicit_return_from_block(ctx, branch, return_type);
    }

    if (ensure_statement_terminates_with_return(ctx, branch, return_type)) {
        return true;
    }

    compile_statement(ctx, branch);
    return branch->original->type == NODE_RETURN;
}

static bool ensure_statement_terminates_with_return(CompilerContext* ctx,
                                                   TypedASTNode* stmt,
                                                   Type* return_type) {
    if (!ctx || !stmt || !stmt->original) {
        return false;
    }

    NodeType node_type = stmt->original->type;

    if (node_type == NODE_RETURN) {
        compile_statement(ctx, stmt);
        return true;
    }

    if (type_is_void_like(return_type)) {
        return false;
    }

    if (node_is_expression_type(node_type)) {
        return emit_implicit_return_from_expression(ctx, stmt);
    }

    if (node_type == NODE_BLOCK) {
        return emit_implicit_return_from_block(ctx, stmt, return_type);
    }

    if (node_type == NODE_IF) {
        return emit_implicit_return_from_if(ctx, stmt, return_type);
    }

    return false;
}

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
        Chunk* chunk = materialize_chunk_from_buffer(ctx, func_chunk);
        if (!chunk) {
            continue;
        }

        Chunk* specialized_chunk = NULL;
        if (ctx->function_specialized_chunks && ctx->function_specialized_chunks[i]) {
            specialized_chunk = materialize_chunk_from_buffer(ctx, ctx->function_specialized_chunks[i]);
            free_bytecode_buffer(ctx->function_specialized_chunks[i]);
            ctx->function_specialized_chunks[i] = NULL;
        }

        Chunk* stub_chunk = NULL;
        if (ctx->function_deopt_stubs && ctx->function_deopt_stubs[i]) {
            stub_chunk = materialize_chunk_from_buffer(ctx, ctx->function_deopt_stubs[i]);
            free_bytecode_buffer(ctx->function_deopt_stubs[i]);
            ctx->function_deopt_stubs[i] = NULL;
        }

        // Register function in VM
        Function* vm_function = &vm.functions[vm.functionCount];
        vm_function->start = 0; // Always start at beginning of chunk
        vm_function->arity = ctx->function_arities[i]; // Use stored arity
        vm_function->chunk = chunk;
        vm_function->specialized_chunk = specialized_chunk;
        vm_function->deopt_stub_chunk = stub_chunk;
        if (ctx->function_hot_counts && specialized_chunk) {
            vm_function->specialization_hits = ctx->function_hot_counts[i];
        } else {
            vm_function->specialization_hits = 0;
        }
        vm_function->tier = specialized_chunk ? FUNCTION_TIER_SPECIALIZED : FUNCTION_TIER_BASELINE;
        vm_function->deopt_handler = specialized_chunk ? vm_default_deopt_stub : NULL;
        vm_function->debug_name = ctx->function_names && ctx->function_names[i]
                                   ? orus_strdup(ctx->function_names[i])
                                   : NULL;

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

    int func_reg = -1;
    if (func_name) {
        Symbol* existing = resolve_symbol_local_only(ctx->symbols, func_name);
        if (existing) {
            if (existing->reg_allocation) {
                func_reg = existing->reg_allocation->logical_id;
            } else {
                func_reg = existing->legacy_register_id;
            }

            if (func_reg >= 0) {
                existing->type = function_type;
                existing->is_initialized = true;
                existing->last_assignment_location = func->original->location;
            }
        }

        if (func_reg < 0) {
            if (func->suggestedRegister >= 0) {
                func_reg = func->suggestedRegister;
            } else if (ctx->compiling_function) {
                func_reg = compiler_alloc_frame(ctx->allocator);
            } else {
                func_reg = compiler_alloc_global(ctx->allocator);
            }

            if (func_reg == -1) {
                return -1;
            }

            if (!register_variable(ctx, ctx->symbols, func_name, func_reg,
                                   function_type, false, false,
                                   func->original->location, true)) {
                compiler_free_register(ctx->allocator, func_reg);
                return -1;
            }
        }

        bool export_candidate = !ctx->compiling_function && ctx->is_module && func_name &&
                                !func->original->function.isMethod &&
                                func->original->function.isPublic;
        if (export_candidate) {
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
    Type* return_type = NULL;
    if (function_type && function_type->kind == TYPE_FUNCTION) {
        return_type = function_type->info.function.returnType;
    }

    if (func->typed.function.body) {
        if (func->typed.function.body->original->type == NODE_BLOCK) {
            int statement_count = func->typed.function.body->typed.block.count;
            for (int i = 0; i < statement_count; i++) {
                TypedASTNode* stmt = func->typed.function.body->typed.block.statements[i];
                if (!stmt) {
                    continue;
                }

                bool is_last = (i == statement_count - 1);
                if (is_last && ensure_statement_terminates_with_return(ctx, stmt, return_type)) {
                    continue;
                }

                compile_statement(ctx, stmt);
            }
        } else {
            if (!ensure_statement_terminates_with_return(ctx, func->typed.function.body, return_type)) {
                compile_statement(ctx, func->typed.function.body);
            }
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

    if (!ctx->compiling_function && ctx->is_module && func_name &&
        !func->original->function.isMethod &&
        func->original->function.isPublic) {
        set_module_export_function_index(ctx, func_name, function_index);
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
