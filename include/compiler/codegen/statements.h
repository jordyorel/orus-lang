#ifndef CODEGEN_STATEMENTS_H
#define CODEGEN_STATEMENTS_H

#include "compiler/compiler.h"
#include "compiler/typed_ast.h"

void compile_statement(CompilerContext* ctx, TypedASTNode* stmt);
void compile_variable_declaration(CompilerContext* ctx, TypedASTNode* var_decl);
void compile_assignment(CompilerContext* ctx, TypedASTNode* assign);
void compile_print_statement(CompilerContext* ctx, TypedASTNode* print);
void compile_if_statement(CompilerContext* ctx, TypedASTNode* if_stmt);
void compile_try_statement(CompilerContext* ctx, TypedASTNode* try_stmt);
void compile_while_statement(CompilerContext* ctx, TypedASTNode* while_stmt);
void compile_for_range_statement(CompilerContext* ctx, TypedASTNode* for_stmt);
void compile_for_iter_statement(CompilerContext* ctx, TypedASTNode* for_stmt);
void compile_break_statement(CompilerContext* ctx, TypedASTNode* break_stmt);
void compile_continue_statement(CompilerContext* ctx, TypedASTNode* continue_stmt);
void compile_block_with_scope(CompilerContext* ctx, TypedASTNode* block, bool create_scope);

#endif // CODEGEN_STATEMENTS_H
