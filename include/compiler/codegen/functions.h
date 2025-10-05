#ifndef CODEGEN_FUNCTIONS_H
#define CODEGEN_FUNCTIONS_H

#include "compiler/compiler.h"
#include "compiler/typed_ast.h"

void finalize_functions_to_vm(CompilerContext* ctx);
int compile_function_declaration(CompilerContext* ctx, TypedASTNode* func);
void compile_return_statement(CompilerContext* ctx, TypedASTNode* ret);

#endif // CODEGEN_FUNCTIONS_H
