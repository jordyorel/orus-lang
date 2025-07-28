// compiler.c - Simple compiler implementation
#include "compiler/compiler.h"
#include "compiler/parser.h"
#include <stdio.h>
#include <stdlib.h>

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
    emitByte(compiler, OP_LOAD_I32_CONST);  // Load constant
    emitByte(compiler, 0);                   // Register 0
    emitByte(compiler, 42);                  // Value (low byte)
    emitByte(compiler, 0);                   // Value (high byte)
    
    emitByte(compiler, OP_PRINT_R);          // Print register
    emitByte(compiler, 0);                   // Register 0
    
    return true;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    if (compiler && compiler->chunk) {
        writeChunk(compiler->chunk, byte, 0, 0); // Line and column will be 0 for now
    }
}