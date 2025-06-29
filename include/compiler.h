#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"
#include "vm.h"
#include "lexer.h"
#include "ast.h"

// Forward declarations
typedef struct ASTNode ASTNode;

// Compiler functions
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
uint8_t allocateRegister(Compiler* compiler);
void freeRegister(Compiler* compiler, uint8_t reg);
bool compile(ASTNode* ast, Compiler* compiler, bool isModule);

// Code emission functions
void emitByte(Compiler* compiler, uint8_t byte);
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2);
void emitConstant(Compiler* compiler, uint8_t reg, Value value);

// Simple parsing function for testing
ASTNode* parseSource(const char* source);
void freeAST(ASTNode* node);

// Expression parsing functions
ASTNode* parseExpression();
ASTNode* parseBinaryExpression(int minPrec);
ASTNode* parsePrimaryExpression();

// Helper functions
int getOperatorPrecedence(TokenType type);
const char* getOperatorString(TokenType type);

#endif