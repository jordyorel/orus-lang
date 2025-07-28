#ifndef ORUS_COMPILER_H
#define ORUS_COMPILER_H

#include "vm/vm.h"
#include "compiler/ast.h"

// Compiler structure is defined in vm/vm.h

// Compiler interface functions
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeCompiler(Compiler* compiler);
bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule);
void emitByte(Compiler* compiler, uint8_t byte);

#endif // ORUS_COMPILER_H