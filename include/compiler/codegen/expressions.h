#ifndef CODEGEN_EXPRESSIONS_H
#define CODEGEN_EXPRESSIONS_H

#include "compiler/compiler.h"
#include "compiler/typed_ast.h"

int compile_expression(CompilerContext* ctx, TypedASTNode* expr);
void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg);
void compile_binary_op(CompilerContext* ctx, TypedASTNode* binary, int target_reg, int left_reg, int right_reg);

#endif // CODEGEN_EXPRESSIONS_H
