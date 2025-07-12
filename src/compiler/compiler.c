// Author: Hierat
// Date: 2023-10-01
// Description: A single pass compiler for the Orus language, handling AST compilation to bytecode.



#include "../../include/compiler.h"
#include "../../include/memory.h"
#include "../../include/symbol_table.h"
#include <string.h>

static bool compileNode(ASTNode* node, Compiler* compiler);
static int compileExpr(ASTNode* node, Compiler* compiler);

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName,
                   const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->maxRegisters = 0;
    compiler->localCount = 0;
    compiler->scopeDepth = 0;
    compiler->hadError = false;
    compiler->currentLine = 1;     // Initialize line tracking
    compiler->currentColumn = 1;   // Initialize column tracking
    symbol_table_init(&compiler->symbols);
}

void freeCompiler(Compiler* compiler) {
    symbol_table_free(&compiler->symbols);
}

uint8_t allocateRegister(Compiler* compiler) {
    uint8_t r = compiler->nextRegister++;
    if (compiler->nextRegister > compiler->maxRegisters)
        compiler->maxRegisters = compiler->nextRegister;
    return r;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    if (reg + 1 == compiler->nextRegister && compiler->nextRegister > 0)
        compiler->nextRegister--;
    (void)compiler;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, compiler->currentLine, compiler->currentColumn);
}

void emitBytes(Compiler* compiler, uint8_t b1, uint8_t b2) {
    emitByte(compiler, b1);
    emitByte(compiler, b2);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int idx = addConstant(compiler->chunk, value);
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)((idx >> 8) & 0xFF));
    emitByte(compiler, (uint8_t)(idx & 0xFF));
}

static int compileExpr(ASTNode* node, Compiler* compiler) {
    // Update current line/column for error reporting
    if (node) {
        compiler->currentLine = node->location.line;
        compiler->currentColumn = node->location.column;
    }
    
    switch (node->type) {
        case NODE_LITERAL: {
            uint8_t r = allocateRegister(compiler);
            emitConstant(compiler, r, node->literal.value);
            return r;
        }
        case NODE_IDENTIFIER: {
            int index;
            if (!symbol_table_get(&compiler->symbols, node->identifier.name, &index)) {
                compiler->hadError = true;
                return allocateRegister(compiler);
            }
            uint8_t r = allocateRegister(compiler);
            emitByte(compiler, OP_LOAD_GLOBAL);
            emitByte(compiler, r);
            emitByte(compiler, (uint8_t)index);
            return r;
        }
        case NODE_BINARY: {
            int left = compileExpr(node->binary.left, compiler);
            int right = compileExpr(node->binary.right, compiler);
            uint8_t dst = allocateRegister(compiler);
            
            if (strcmp(node->binary.op, "+") == 0) {
                emitByte(compiler, OP_ADD_I32_R);
            } else if (strcmp(node->binary.op, "-") == 0) {
                emitByte(compiler, OP_SUB_I32_R);
            } else if (strcmp(node->binary.op, "*") == 0) {
                emitByte(compiler, OP_MUL_I32_R);
            } else if (strcmp(node->binary.op, "/") == 0) {
                emitByte(compiler, OP_DIV_I32_R);
            } else if (strcmp(node->binary.op, "%") == 0) {
                emitByte(compiler, OP_MOD_I32_R);
            } else if (strcmp(node->binary.op, "and") == 0) {
                // Use runtime polymorphic AND (handles any type to boolean conversion)
                emitByte(compiler, OP_AND_BOOL_R);
            } else if (strcmp(node->binary.op, "or") == 0) {
                // Use runtime polymorphic OR (handles any type to boolean conversion)
                emitByte(compiler, OP_OR_BOOL_R);
            } else {
                compiler->hadError = true;
                emitByte(compiler, OP_ADD_I32_R); // Fallback
            }
            
            emitByte(compiler, dst);
            emitByte(compiler, (uint8_t)left);
            emitByte(compiler, (uint8_t)right);
            freeRegister(compiler, right);
            freeRegister(compiler, left);
            return dst;
        }
        case NODE_TIME_STAMP: {
            uint8_t r = allocateRegister(compiler);
            emitByte(compiler, OP_TIME_STAMP);
            emitByte(compiler, r);
            return r;
        }
        case NODE_CAST: {
            int srcReg = compileExpr(node->cast.expression, compiler);
            uint8_t dstReg = allocateRegister(compiler);
            
            // Get the target type name from the type annotation
            const char* targetType = node->cast.targetType->typeAnnotation.name;
            
            // For now, assume source is i32 and determine conversion opcode
            // TODO: This should use proper type inference
            if (strcmp(targetType, "i64") == 0) {
                emitByte(compiler, OP_I32_TO_I64_R);
            } else if (strcmp(targetType, "f64") == 0) {
                emitByte(compiler, OP_I32_TO_F64_R);
            } else if (strcmp(targetType, "u32") == 0) {
                emitByte(compiler, OP_I32_TO_U32_R);
            } else if (strcmp(targetType, "u64") == 0) {
                emitByte(compiler, OP_I32_TO_U64_R);
            } else if (strcmp(targetType, "bool") == 0) {
                emitByte(compiler, OP_I32_TO_BOOL_R);
            } else if (strcmp(targetType, "string") == 0) {
                emitByte(compiler, OP_TO_STRING_R);
            } else {
                // Invalid or same-type cast, just copy the value
                emitByte(compiler, OP_MOVE);
            }
            
            emitByte(compiler, dstReg);
            emitByte(compiler, (uint8_t)srcReg);
            freeRegister(compiler, srcReg);
            return dstReg;
        }
        case NODE_UNARY: {
            int operand = compileExpr(node->unary.operand, compiler);
            uint8_t dst = allocateRegister(compiler);
            
            if (strcmp(node->unary.op, "not") == 0) {
                emitByte(compiler, OP_NOT_BOOL_R);
                emitByte(compiler, dst);
                emitByte(compiler, (uint8_t)operand);
            } else if (strcmp(node->unary.op, "-") == 0) {
                // Use proper single-pass negation opcode (in-place operation)
                emitByte(compiler, OP_MOVE);  // First copy operand to destination
                emitByte(compiler, dst);
                emitByte(compiler, (uint8_t)operand);
                emitByte(compiler, OP_NEG_I32_R);  // Then negate in-place (single register)
                emitByte(compiler, dst);
            } else {
                // For now, unsupported unary operations
                compiler->hadError = true;
                emitByte(compiler, OP_MOVE); // Fallback
                emitByte(compiler, dst);
                emitByte(compiler, (uint8_t)operand);
            }
            
            freeRegister(compiler, operand);
            return dst;
        }
        default:
            compiler->hadError = true;
            return allocateRegister(compiler);
    }
}

static bool compileNode(ASTNode* node, Compiler* compiler) {
    // Update current line/column for error reporting
    if (node) {
        compiler->currentLine = node->location.line;
        compiler->currentColumn = node->location.column;
    }
    
    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; i++) {
                if (!compileNode(node->program.declarations[i], compiler))
                    return false;
            }
            return true;

        case NODE_VAR_DECL: {
            int reg = compileExpr(node->varDecl.initializer, compiler);
            int idx = vm.variableCount++;
            ObjString* name = allocateString(node->varDecl.name,
                                             (int)strlen(node->varDecl.name));
            vm.variableNames[idx].name = name;
            vm.variableNames[idx].length = name->length;
            vm.globals[idx] = NIL_VAL;
            
            // CREATIVE SOLUTION: Type-aware single-pass compilation
            // Extract the declared type from AST and set proper type for global variable
            if (node->varDecl.typeAnnotation) {
                const char* typeName = node->varDecl.typeAnnotation->typeAnnotation.name;
                if (strcmp(typeName, "i32") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_I32);
                } else if (strcmp(typeName, "i64") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_I64);
                } else if (strcmp(typeName, "u32") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_U32);
                } else if (strcmp(typeName, "u64") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_U64);
                } else if (strcmp(typeName, "f64") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_F64);
                } else if (strcmp(typeName, "bool") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_BOOL);
                } else if (strcmp(typeName, "string") == 0) {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_STRING);
                } else {
                    vm.globalTypes[idx] = getPrimitiveType(TYPE_ANY);
                }
            } else {
                // Type inference: infer from literal value if no explicit type
                vm.globalTypes[idx] = getPrimitiveType(TYPE_ANY);
            }
            
            symbol_table_set(&compiler->symbols, node->varDecl.name, idx);
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)idx);
            emitByte(compiler, (uint8_t)reg);
            freeRegister(compiler, reg);
            return true;
        }

        case NODE_ASSIGN: {
            int reg = compileExpr(node->assign.value, compiler);
            int idx;
            if (!symbol_table_get(&compiler->symbols, node->assign.name, &idx)) {
                idx = vm.variableCount++;
                ObjString* name = allocateString(node->assign.name,
                                                 (int)strlen(node->assign.name));
                vm.variableNames[idx].name = name;
                vm.variableNames[idx].length = name->length;
                vm.globals[idx] = NIL_VAL;
                // For assignments without declaration, keep as TYPE_ANY for flexibility
                vm.globalTypes[idx] = getPrimitiveType(TYPE_ANY);
                symbol_table_set(&compiler->symbols, node->assign.name, idx);
            }
            emitByte(compiler, OP_STORE_GLOBAL);
            emitByte(compiler, (uint8_t)idx);
            emitByte(compiler, (uint8_t)reg);
            freeRegister(compiler, reg);
            return true;
        }

        case NODE_PRINT: {
            for (int i = 0; i < node->print.count; i++) {
                int r = compileExpr(node->print.values[i], compiler);
                if (i == node->print.count - 1 && node->print.newline) {
                    emitByte(compiler, OP_PRINT_R);
                } else {
                    emitByte(compiler, OP_PRINT_NO_NL_R);
                }
                emitByte(compiler, (uint8_t)r);
                freeRegister(compiler, r);
            }
            if (node->print.count == 0 && node->print.newline) {
                uint8_t r = allocateRegister(compiler);
                emitByte(compiler, OP_LOAD_NIL);
                emitByte(compiler, r);
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, r);
                freeRegister(compiler, r);
            }
            return true;
        }

        default:
            compileExpr(node, compiler);
            return !compiler->hadError;
    }
}

bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule;
    compiler->hadError = false;
    return compileNode(ast, compiler) && !compiler->hadError;
}
