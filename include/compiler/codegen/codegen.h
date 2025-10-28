// Orus Language Project

#ifndef CODEGEN_H
#define CODEGEN_H

#include "compiler/codegen/expressions.h"
#include "compiler/codegen/statements.h"
#include "compiler/codegen/functions.h"
#include "compiler/codegen/modules.h"
#include "compiler/compiler.h"

bool generate_bytecode_from_ast(CompilerContext* ctx);

bool apply_peephole_optimizations(CompilerContext* ctx);
bool apply_register_coalescing(CompilerContext* ctx);

#endif // CODEGEN_H
