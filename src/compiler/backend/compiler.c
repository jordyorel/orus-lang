// compiler.c - Multi-pass compiler implementation
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "compiler/typed_ast_visualizer.h"
#include "compiler/optimizer.h"
#include "compiler/parser.h"
#include "runtime/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== LEGACY SINGLE-PASS COMPILER =====

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
}

void freeCompiler(Compiler* compiler) {
    (void)compiler; // Suppress unused parameter warning
    // No dynamic memory to free in this simple implementation
}

bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule; // Suppress unused parameter warning
    if (!ast || !compiler) {
        return false;
    }
    
    // Simple compilation: just emit a placeholder
    // In a real implementation, this would traverse the AST and emit bytecode
    printf("[DEBUG] compileProgram: Compiling AST node type %d\n", ast->type);
    
    // Emit a simple instruction sequence for testing
    // First, add the literal value to the constants table
    Value literalValue = I32_VAL(42);
    int constantIndex = addConstant(compiler->chunk, literalValue);
    
    printf("[DEBUG] compileProgram: Added constant %d to table at index %d\n", 42, constantIndex);
    
    emitByte(compiler, OP_LOAD_I32_CONST);  // Load constant
    emitByte(compiler, 0);                   // Register 0
    emitByte(compiler, (constantIndex >> 8) & 0xFF);  // High byte of constant index
    emitByte(compiler, constantIndex & 0xFF);         // Low byte of constant index
    
    emitByte(compiler, OP_PRINT_R);          // Print register
    emitByte(compiler, 0);                   // Register 0
    
    return true;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    if (compiler && compiler->chunk) {
        writeChunk(compiler->chunk, byte, 0, 0); // Line and column will be 0 for now
    }
}

// ===== NEW MULTI-PASS COMPILER INFRASTRUCTURE =====

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
    
    // Initialize register allocation
    ctx->allocator = init_mp_register_allocator();
    ctx->next_temp_register = MP_TEMP_REG_START;
    ctx->next_local_register = MP_FRAME_REG_START;
    ctx->next_global_register = MP_GLOBAL_REG_START;
    
    // Initialize bytecode output
    ctx->bytecode = init_bytecode_buffer();
    
    // Initialize debugging
    ctx->enable_visualization = false;  // Default off
    ctx->debug_output = stdout;
    
    // TODO: Initialize these in later phases
    ctx->symbols = NULL;      // Will implement in Phase 2
    ctx->scopes = NULL;       // Will implement in Phase 2  
    ctx->constants = NULL;    // Will implement in Phase 2
    ctx->errors = NULL;       // Will implement in Phase 2
    ctx->opt_ctx = NULL;      // Will implement in Phase 2
    
    if (!ctx->allocator || !ctx->bytecode) {
        free_compiler_context(ctx);
        return NULL;
    }
    
    return ctx;
}

bool compile_to_bytecode(CompilerContext* ctx) {
    if (!ctx || !ctx->input_ast) return false;
    
    printf("[MULTI_COMPILER] Starting compilation pipeline...\n");
    
    // Phase 1: Visualization (if enabled)
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== INPUT TYPED AST ===\n");
        visualize_typed_ast(ctx->input_ast, ctx->debug_output);
    }
    
    // Phase 2: Optimization Pass
    if (!run_optimization_pass(ctx)) {
        printf("[MULTI_COMPILER] Optimization pass failed\n");
        return false;
    }
    
    // Phase 3: Code Generation Pass  
    if (!run_codegen_pass(ctx)) {
        printf("[MULTI_COMPILER] Code generation pass failed\n");
        return false;
    }
    
    printf("[MULTI_COMPILER] Compilation completed successfully, generated %d instructions\n", 
           ctx->bytecode->count);
    return true;
}

bool run_optimization_pass(CompilerContext* ctx) {
    printf("[MULTI_COMPILER] 🚀 Running optimization pass...\n");
    
    // Initialize optimization context
    OptimizationContext* opt_ctx = init_optimization_context();
    if (!opt_ctx) {
        printf("[MULTI_COMPILER] ❌ Failed to initialize optimization context\n");
        return false;
    }
    
    // Run actual optimizations on the TypedAST
    ctx->optimized_ast = optimize_typed_ast(ctx->input_ast, opt_ctx);
    
    if (!ctx->optimized_ast) {
        printf("[MULTI_COMPILER] ❌ Optimization failed\n");
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
    
    printf("[MULTI_COMPILER] ✅ Optimization pass completed with real optimizations!\n");
    return true;
}

bool run_codegen_pass(CompilerContext* ctx) {
    printf("[MULTI_COMPILER] Running code generation pass...\n");
    
    // For Phase 1: Minimal codegen - just emit a HALT instruction
    // TODO: Implement full codegen in Phase 3
    
    // Simple test: emit a HALT instruction to verify bytecode generation works
    emit_instruction_to_buffer(ctx->bytecode, OP_HALT, 0, 0, 0);
    
    printf("[MULTI_COMPILER] Code generation completed, %d instructions generated\n", 
           ctx->bytecode->count);
    return true;
}

void free_compiler_context(CompilerContext* ctx) {
    if (!ctx) return;
    
    free_mp_register_allocator(ctx->allocator);
    free_bytecode_buffer(ctx->bytecode);
    
    // Free optimization context if it was created
    if (ctx->opt_ctx) {
        free_optimization_context(ctx->opt_ctx);
    }
    
    // Note: Don't free input_ast - it's owned by caller
    // TODO: Free other components as we implement them
    
    free(ctx);
}