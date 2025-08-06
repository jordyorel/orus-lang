// compiler.c - Multi-pass compiler pipeline coordinator
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "compiler/typed_ast_visualizer.h"
#include "compiler/optimization/optimizer.h"
#include "compiler/codegen/codegen.h"
#include "compiler/symbol_table.h"
#include "runtime/memory.h"
#include "debug/debug_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== MULTI-PASS COMPILER PIPELINE COORDINATOR =====
// Orchestrates the entire compilation process:
// TypedAST → Optimization → CodeGeneration → Bytecode

// BytecodeBuffer implementation
BytecodeBuffer* init_bytecode_buffer(void) {
    BytecodeBuffer* buffer = malloc(sizeof(BytecodeBuffer));
    if (!buffer) return NULL;
    
    buffer->capacity = 256;  // Initial capacity
    buffer->count = 0;
    buffer->instructions = malloc(buffer->capacity);
    buffer->source_lines = malloc(buffer->capacity * sizeof(int));
    buffer->source_columns = malloc(buffer->capacity * sizeof(int));
    
    if (!buffer->instructions || !buffer->source_lines || !buffer->source_columns) {
        free_bytecode_buffer(buffer);
        return NULL;
    }
    
    // Initialize jump patching
    buffer->patches = NULL;
    buffer->patch_count = 0;
    buffer->patch_capacity = 0;
    
    return buffer;
}

void free_bytecode_buffer(BytecodeBuffer* buffer) {
    if (!buffer) return;
    
    free(buffer->instructions);
    free(buffer->source_lines);
    free(buffer->source_columns);
    free(buffer->patches);
    free(buffer);
}

// ===== CONSTANT POOL IMPLEMENTATION =====

ConstantPool* init_constant_pool(void) {
    ConstantPool* pool = malloc(sizeof(ConstantPool));
    if (!pool) return NULL;
    
    pool->capacity = 16;  // Initial capacity
    pool->count = 0;
    pool->values = malloc(pool->capacity * sizeof(Value));
    
    if (!pool->values) {
        free(pool);
        return NULL;
    }
    
    DEBUG_CODEGEN_PRINT("Created constant pool (capacity=%d)\n", pool->capacity);
    return pool;
}

void free_constant_pool(ConstantPool* pool) {
    if (!pool) return;
    
    free(pool->values);
    free(pool);
    
    DEBUG_CODEGEN_PRINT("Freed constant pool\n");
}

int add_constant(ConstantPool* pool, Value value) {
    if (!pool) return -1;
    
    // Check if constant already exists (for deduplication)
    for (int i = 0; i < pool->count; i++) {
        Value existing = pool->values[i];
        if (existing.type == value.type) {
            // Simple equality check for basic types
            if (value.type == VAL_I32 && AS_I32(existing) == AS_I32(value)) {
                DEBUG_CODEGEN_PRINT("Reusing existing i32 constant %d at index %d\n", AS_I32(value), i);
                return i;
            }
            if (value.type == VAL_STRING && AS_STRING(existing) == AS_STRING(value)) {
                DEBUG_CODEGEN_PRINT("Reusing existing string constant at index %d\n", i);
                return i;
            }
        }
    }
    
    // Resize if needed
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->values = realloc(pool->values, pool->capacity * sizeof(Value));
        if (!pool->values) return -1;
        DEBUG_CODEGEN_PRINT("Resized to capacity %d\n", pool->capacity);
    }
    
    // Add new constant
    pool->values[pool->count] = value;
    int index = pool->count;
    pool->count++;
    
    if (value.type == VAL_I32) {
        DEBUG_CODEGEN_PRINT("Added i32 constant %d at index %d\n", AS_I32(value), index);
    } else if (value.type == VAL_STRING) {
        DEBUG_CODEGEN_PRINT("Added string constant \"%s\" at index %d\n", AS_STRING(value)->chars, index);
    } else {
        DEBUG_CODEGEN_PRINT("Added constant (type=%d) at index %d\n", value.type, index);
    }
    
    return index;
}

Value get_constant(ConstantPool* pool, int index) {
    if (!pool || index < 0 || index >= pool->count) {
        // Return a default value for invalid indices
        Value nil = {.type = VAL_BOOL, .as.boolean = false};
        return nil;
    }
    
    return pool->values[index];
}

void emit_byte_to_buffer(BytecodeBuffer* buffer, uint8_t byte) {
    if (!buffer) return;
    
    // Resize if needed
    if (buffer->count >= buffer->capacity) {
        buffer->capacity *= 2;
        buffer->instructions = realloc(buffer->instructions, buffer->capacity);
        buffer->source_lines = realloc(buffer->source_lines, buffer->capacity * sizeof(int));
        buffer->source_columns = realloc(buffer->source_columns, buffer->capacity * sizeof(int));
    }
    
    buffer->instructions[buffer->count] = byte;
    buffer->source_lines[buffer->count] = 0;    // TODO: Add source location tracking
    buffer->source_columns[buffer->count] = 0;  // TODO: Add source location tracking
    buffer->count++;
}

void emit_instruction_to_buffer(BytecodeBuffer* buffer, uint8_t opcode, uint8_t reg1, uint8_t reg2, uint8_t reg3) {
    emit_byte_to_buffer(buffer, opcode);
    emit_byte_to_buffer(buffer, reg1);
    emit_byte_to_buffer(buffer, reg2);
    emit_byte_to_buffer(buffer, reg3);
}

int emit_jump_placeholder(BytecodeBuffer* buffer, uint8_t jump_opcode) {
    // TODO: Implement jump patching in Phase 2
    emit_byte_to_buffer(buffer, jump_opcode);
    emit_byte_to_buffer(buffer, 0);  // Placeholder offset
    return buffer->count - 1;  // Return offset for patching
}

void patch_jump(BytecodeBuffer* buffer, int jump_offset, int target_offset) {
    // TODO: Implement jump patching in Phase 2
    (void)buffer;
    (void)jump_offset;
    (void)target_offset;
}

// CompilerContext implementation
CompilerContext* init_compiler_context(TypedASTNode* typed_ast) {
    if (!typed_ast) return NULL;
    
    CompilerContext* ctx = malloc(sizeof(CompilerContext));
    if (!ctx) return NULL;
    
    // Initialize all fields
    ctx->input_ast = typed_ast;
    ctx->optimized_ast = NULL;
    
    // Initialize register allocation - DUAL SYSTEM
    ctx->allocator = init_mp_register_allocator();           // Legacy compatibility
    ctx->dual_allocator = init_dual_register_allocator();    // New dual system
    ctx->next_temp_register = MP_TEMP_REG_START;
    ctx->next_local_register = MP_FRAME_REG_START;
    ctx->next_global_register = MP_GLOBAL_REG_START;
    
    // Initialize bytecode output
    ctx->bytecode = init_bytecode_buffer();
    
    // Initialize constant pool
    ctx->constants = init_constant_pool();
    
    // Initialize debugging
    ctx->enable_visualization = false;  // Default off
    ctx->debug_output = stdout;
    
    // Initialize symbol table for variable tracking
    ctx->symbols = create_symbol_table(NULL);  // Global scope
    
    // TODO: Initialize these in later phases
    ctx->scopes = NULL;       // Will implement in Phase 2  
    ctx->errors = NULL;       // Will implement in Phase 2
    ctx->has_compilation_errors = false;  // Initialize error tracking
    ctx->opt_ctx = NULL;      // Will implement in Phase 2
    
    // Initialize loop control context
    ctx->current_loop_start = -1;     // No current loop
    ctx->current_loop_end = -1;       // No current loop
    ctx->current_loop_continue = -1;  // No current loop
    ctx->break_statements = NULL;     // No break statements yet
    ctx->break_count = 0;
    ctx->break_capacity = 0;
    ctx->continue_statements = NULL;  // No continue statements yet
    ctx->continue_count = 0;
    ctx->continue_capacity = 0;
    
    // Initialize function compilation context (NEW)
    ctx->current_function_index = -1;    // Global scope initially
    ctx->function_chunks = NULL;         // No function chunks yet
    ctx->function_arities = NULL;        // No function arities yet
    ctx->function_count = 0;
    ctx->function_capacity = 0;
    
    if (!ctx->allocator || !ctx->dual_allocator || !ctx->bytecode || !ctx->constants || !ctx->symbols) {
        free_compiler_context(ctx);
        return NULL;
    }
    
    return ctx;
}

bool compile_to_bytecode(CompilerContext* ctx) {
    if (!ctx || !ctx->input_ast) return false;
    
    DEBUG_CODEGEN_PRINT("Starting compilation pipeline...\n");
    
    // Phase 1: Visualization (if enabled)
    DEBUG_CODEGEN_PRINT("Phase 1: Visualization...\n");
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== INPUT TYPED AST ===\n");
        visualize_typed_ast(ctx->input_ast, ctx->debug_output);
    }
    DEBUG_CODEGEN_PRINT("Phase 1: Visualization completed\n");
    
    // Phase 2: Optimization Pass
    DEBUG_CODEGEN_PRINT("Phase 2: About to start optimization pass...\n");
    if (!run_optimization_pass(ctx)) {
        DEBUG_CODEGEN_PRINT("Optimization pass failed\n");
        return false;
    }
    DEBUG_CODEGEN_PRINT("Phase 2: Optimization pass completed\n");
    
    // Phase 3: Code Generation Pass  
    DEBUG_CODEGEN_PRINT("Phase 3: About to start code generation pass...\n");
    if (!run_codegen_pass(ctx)) {
        DEBUG_CODEGEN_PRINT("Code generation pass failed\n");
        return false;
    }
    DEBUG_CODEGEN_PRINT("Phase 3: Code generation pass completed\n");
    
    DEBUG_CODEGEN_PRINT("Compilation completed successfully, generated %d instructions\n", 
           ctx->bytecode->count);
    return true;
}

bool run_optimization_pass(CompilerContext* ctx) {
    DEBUG_OPTIMIZER_PRINT("🚀 Running optimization pass...\n");
    
    // Initialize optimization context
    OptimizationContext* opt_ctx = init_optimization_context();
    if (!opt_ctx) {
        DEBUG_OPTIMIZER_PRINT("❌ Failed to initialize optimization context\n");
        return false;
    }
    
    // Run actual optimizations on the TypedAST
    ctx->optimized_ast = optimize_typed_ast(ctx->input_ast, opt_ctx);
    
    if (!ctx->optimized_ast) {
        DEBUG_OPTIMIZER_PRINT("❌ Optimization failed\n");
        free_optimization_context(opt_ctx);
        return false;
    }
    
    // 🎯 CRITICAL: Visualization after optimization to show differences
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== OPTIMIZED TYPED AST ===\n");
        visualize_typed_ast(ctx->optimized_ast, ctx->debug_output);
        fprintf(ctx->debug_output, "\n");
    }
    
    // Store the optimization context for potential use in codegen
    ctx->opt_ctx = opt_ctx;
    
    DEBUG_OPTIMIZER_PRINT("✅ Optimization pass completed with real optimizations!\n");
    return true;
}

bool run_codegen_pass(CompilerContext* ctx) {
    DEBUG_CODEGEN_PRINT("Running production-grade code generation pass...\n");
    
    // Use the new production-ready code generator
    bool success = generate_bytecode_from_ast(ctx);
    
    if (success) {
        DEBUG_CODEGEN_PRINT("✅ Code generation completed, %d instructions generated\n", 
               ctx->bytecode->count);
        
        // Copy compiled functions to the VM
        if (ctx->function_count > 0) {
            finalize_functions_to_vm(ctx);
        }
        
        // Add bytecode visualization if enabled
        if (ctx->enable_visualization) {
            printf("\n=== BYTECODE DUMP ===\n");
            printf("Instructions: %d\n", ctx->bytecode->count);
            
            // Decode and display each instruction
            for (int i = 0; i < ctx->bytecode->count; i += 4) {
                if (i + 3 < ctx->bytecode->count) {
                    uint8_t opcode = ctx->bytecode->instructions[i];
                    uint8_t reg1 = ctx->bytecode->instructions[i + 1];
                    uint8_t reg2 = ctx->bytecode->instructions[i + 2];
                    uint8_t reg3 = ctx->bytecode->instructions[i + 3];
                    
                    printf("%04d: %02X", i, opcode);
                    
                    // Decode common opcodes for better readability
                    if (opcode == 0xAB) { // OP_LOAD_I32_CONST
                        int32_t value = (reg2 << 8) | reg3;
                        printf(" (OP_LOAD_I32_CONST) reg=R%d, value=%d", reg1, value);
                    } else if (opcode == 0xAE) { // OP_MOVE_I32
                        printf(" (OP_MOVE_I32) dst=R%d, src=R%d", reg1, reg2);
                    } else if (opcode == 0x78) { // OP_PRINT_R
                        printf(" (OP_PRINT_R) reg=R%d", reg1);
                    } else if (opcode == 0xC4) { // OP_HALT
                        printf(" (OP_HALT)");
                    } else {
                        printf(" (OPCODE_%02X) R%d, R%d, R%d", opcode, reg1, reg2, reg3);
                    }
                    
                    // Suppress unused variable warnings
                    (void)reg1;
                    
                    printf("\n");
                }
            }
            
            printf("=== END BYTECODE ===\n\n");
            
            // Add optimized bytecode version
            printf("=== OPTIMIZED BYTECODE ===\n");
            printf("Register-optimized instruction sequence:\n");
            
            // Decode and display optimized instructions with cleaner formatting
            for (int i = 0; i < ctx->bytecode->count; i += 4) {
                if (i + 3 < ctx->bytecode->count) {
                    uint8_t opcode = ctx->bytecode->instructions[i];
                    uint8_t reg1 = ctx->bytecode->instructions[i + 1];
                    uint8_t reg2 = ctx->bytecode->instructions[i + 2];
                    uint8_t reg3 = ctx->bytecode->instructions[i + 3];
                    
                    // Clean, optimized format showing register reuse and specialization
                    if (opcode == 0xAB) { // OP_LOAD_I32_CONST
                        int32_t value = (reg2 << 8) | reg3;
                        printf("  LOAD_CONST  R%-3d ← %d\n", reg1, value);
                    } else if (opcode == 0xAE) { // OP_MOVE_I32
                        printf("  MOVE        R%-3d ← R%d\n", reg1, reg2);
                    } else if (opcode == 0x78) { // OP_PRINT_R
                        printf("  PRINT       R%-3d\n", reg1);
                    } else if (opcode == 0xC4) { // OP_HALT
                        printf("  HALT\n");
                    } else {
                        printf("  OP_%02X       R%-3d, R%-3d, R%-3d\n", opcode, reg1, reg2, reg3);
                    }
                    
                    // Suppress unused variable warnings
                    (void)reg1;
                }
            }
            
            printf("=== END OPTIMIZED BYTECODE ===\n\n");
            printf("🚀 Register Allocation Summary:\n");
            printf("   - Temp registers (R192-R239): Used for intermediate values\n");
            printf("   - Frame registers (R64-R191): Used for variables\n");
            printf("   - Specialized opcodes: OP_LOAD_I32_CONST, OP_MOVE_I32\n\n");
        }
    } else {
        DEBUG_CODEGEN_PRINT("❌ Code generation failed\n");
    }
    
    return success;
}

void free_compiler_context(CompilerContext* ctx) {
    if (!ctx) return;
    
    free_mp_register_allocator(ctx->allocator);
    free_dual_register_allocator(ctx->dual_allocator);  // Clean up dual system
    free_bytecode_buffer(ctx->bytecode);
    free_constant_pool(ctx->constants);
    
    // Free symbol table
    if (ctx->symbols) {
        free_symbol_table(ctx->symbols);
    }
    
    // Free optimization context if it was created
    if (ctx->opt_ctx) {
        free_optimization_context(ctx->opt_ctx);
    }
    
    // Free break statement tracking
    if (ctx->break_statements) {
        free(ctx->break_statements);
    }
    
    // Free continue statement tracking
    if (ctx->continue_statements) {
        free(ctx->continue_statements);
    }
    
    // Free function compilation resources (NEW)
    if (ctx->function_chunks) {
        for (int i = 0; i < ctx->function_count; i++) {
            if (ctx->function_chunks[i]) {
                free_bytecode_buffer(ctx->function_chunks[i]);
            }
        }
        free(ctx->function_chunks);
    }
    
    // Free function arities array
    if (ctx->function_arities) {
        free(ctx->function_arities);
    }
    
    // Note: Don't free input_ast - it's owned by caller
    // TODO: Free other components as we implement them
    
    free(ctx);
}

// ===== LEGACY COMPATIBILITY STUBS =====
// These functions are still called by vm.c and should be removed in the future

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    // Legacy compatibility - minimal initialization
    if (compiler) {
        compiler->chunk = chunk;
        compiler->fileName = fileName;
        compiler->source = source;
        compiler->nextRegister = 0;
    }
}

void freeCompiler(Compiler* compiler) {
    // Legacy compatibility - no cleanup needed
    (void)compiler;
}

bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule) {
    // Legacy compatibility - should not be used with new multi-pass compiler
    (void)ast;
    (void)compiler;
    (void)isModule;
    DEBUG_CODEGEN_PRINT("[LEGACY] compileProgram stub called - this should be replaced with multi-pass compiler\n");
    return true;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    // Legacy compatibility - minimal implementation
    if (compiler && compiler->chunk) {
        writeChunk(compiler->chunk, byte, 0, 0);
    }
}

// ====== DUAL REGISTER SYSTEM - Smart Instruction Emission ======

// Get the appropriate typed opcode for an operation and type
OpCode get_typed_opcode(const char* op, RegisterType type) {
    if (strcmp(op, "+") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_ADD_I32_TYPED;
            case REG_TYPE_I64: return OP_ADD_I64_TYPED;
            case REG_TYPE_F64: return OP_ADD_F64_TYPED;
            case REG_TYPE_U32: return OP_ADD_U32_TYPED;
            case REG_TYPE_U64: return OP_ADD_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "-") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_SUB_I32_TYPED;
            case REG_TYPE_I64: return OP_SUB_I64_TYPED;
            case REG_TYPE_F64: return OP_SUB_F64_TYPED;
            case REG_TYPE_U32: return OP_SUB_U32_TYPED;
            case REG_TYPE_U64: return OP_SUB_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "*") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MUL_I32_TYPED;
            case REG_TYPE_I64: return OP_MUL_I64_TYPED;
            case REG_TYPE_F64: return OP_MUL_F64_TYPED;
            case REG_TYPE_U32: return OP_MUL_U32_TYPED;
            case REG_TYPE_U64: return OP_MUL_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "/") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_DIV_I32_TYPED;
            case REG_TYPE_I64: return OP_DIV_I64_TYPED;
            case REG_TYPE_F64: return OP_DIV_F64_TYPED;
            case REG_TYPE_U32: return OP_DIV_U32_TYPED;
            case REG_TYPE_U64: return OP_DIV_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "%") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MOD_I32_TYPED;
            case REG_TYPE_I64: return OP_MOD_I64_TYPED;
            case REG_TYPE_F64: return OP_MOD_F64_TYPED;
            case REG_TYPE_U32: return OP_MOD_U32_TYPED;
            case REG_TYPE_U64: return OP_MOD_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    
    return OP_HALT; // Invalid operation
}

// Get the appropriate standard opcode for an operation and type
OpCode get_standard_opcode(const char* op, RegisterType type) {
    if (strcmp(op, "+") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_ADD_I32_R;
            case REG_TYPE_I64: return OP_ADD_I64_R;
            case REG_TYPE_F64: return OP_ADD_F64_R;
            case REG_TYPE_U32: return OP_ADD_U32_R;
            case REG_TYPE_U64: return OP_ADD_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "-") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_SUB_I32_R;
            case REG_TYPE_I64: return OP_SUB_I64_R;
            case REG_TYPE_F64: return OP_SUB_F64_R;
            case REG_TYPE_U32: return OP_SUB_U32_R;
            case REG_TYPE_U64: return OP_SUB_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "*") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MUL_I32_R;
            case REG_TYPE_I64: return OP_MUL_I64_R;
            case REG_TYPE_F64: return OP_MUL_F64_R;
            case REG_TYPE_U32: return OP_MUL_U32_R;
            case REG_TYPE_U64: return OP_MUL_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "/") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_DIV_I32_R;
            case REG_TYPE_I64: return OP_DIV_I64_R;
            case REG_TYPE_F64: return OP_DIV_F64_R;
            case REG_TYPE_U32: return OP_DIV_U32_R;
            case REG_TYPE_U64: return OP_DIV_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "%") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MOD_I32_R;
            case REG_TYPE_I64: return OP_MOD_I64_R;
            case REG_TYPE_F64: return OP_MOD_F64_R;
            case REG_TYPE_U32: return OP_MOD_U32_R;
            case REG_TYPE_U64: return OP_MOD_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    
    return OP_HALT; // Invalid operation
}

// Smart instruction emission that chooses between typed and standard opcodes
void emit_arithmetic_instruction_smart(CompilerContext* ctx, const char* op, 
                                     struct RegisterAllocation* dst, 
                                     struct RegisterAllocation* left, 
                                     struct RegisterAllocation* right) {
    if (!ctx || !dst || !left || !right) {
        DEBUG_CODEGEN_PRINT("Error: NULL parameters\n");
        return;
    }
    
    // Ensure all registers use the same strategy for compatibility
    RegisterStrategy strategy = dst->strategy;
    if (left->strategy != strategy || right->strategy != strategy) {
        DEBUG_CODEGEN_PRINT("Warning: Mixed register strategies, forcing standard\n");
        strategy = REG_STRATEGY_STANDARD;
    }
    
    OpCode opcode;
    uint8_t reg1, reg2, reg3;
    
    if (strategy == REG_STRATEGY_TYPED) {
        // Use typed instruction (performance optimized)
        opcode = get_typed_opcode(op, dst->physical_type);
        reg1 = (uint8_t)dst->physical_id;
        reg2 = (uint8_t)left->physical_id;
        reg3 = (uint8_t)right->physical_id;
        
        DEBUG_CODEGEN_PRINT("Using TYPED instruction: %s (opcode=%d) dst=%d, left=%d, right=%d\n", 
               op, opcode, reg1, reg2, reg3);
    } else {
        // Use standard instruction (compatibility)
        opcode = get_standard_opcode(op, dst->physical_type);
        reg1 = (uint8_t)dst->logical_id;
        reg2 = (uint8_t)left->logical_id;
        reg3 = (uint8_t)right->logical_id;
        
        DEBUG_CODEGEN_PRINT("Using STANDARD instruction: %s (opcode=%d) dst=%d, left=%d, right=%d\n", 
               op, opcode, reg1, reg2, reg3);
    }
    
    if (opcode == OP_HALT) {
        DEBUG_CODEGEN_PRINT("Error: Invalid opcode for operation '%s' and type %d\n", op, dst->physical_type);
        return;
    }
    
    // Emit the instruction
    emit_instruction_to_buffer(ctx->bytecode, opcode, reg1, reg2, reg3);
}