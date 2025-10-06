// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/codegen/codegen.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Aggregated code generation interface exposing the specialized compilation helpers for expressions, statements, functions, and module import/export handling.


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
