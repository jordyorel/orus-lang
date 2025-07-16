/**
 * @file orus.h
 * @brief Main entry point for the Orus language implementation
 * @details This header provides the complete public API for the Orus language
 *          interpreter and compiler. Include this header to access all
 *          public functionality.
 */

#ifndef ORUS_H
#define ORUS_H

// Public API - stable interface
#include "public/common.h"
#include "public/value.h"
#include "public/version.h"

// Compiler subsystem
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include "compiler/symbol_table.h"

// Virtual machine subsystem
#include "vm/vm.h"
#include "vm/vm_dispatch.h"
#include "vm/vm_arithmetic.h"
#include "vm/vm_comparison.h"
#include "vm/vm_constants.h"
#include "vm/vm_control_flow.h"
#include "vm/vm_string_ops.h"
#include "vm/vm_typed_ops.h"
#include "vm/vm_validation.h"

// Type system
#include "type/type.h"

// Error handling
#include "errors/error_interface.h"
#include "errors/error_types.h"
#include "errors/features/type_errors.h"

// Runtime support
#include "runtime/memory.h"
#include "runtime/builtins.h"
#include "runtime/jumptable.h"

// Development tools
#include "tools/debug.h"
#include "tools/repl.h"
#include "tools/scope_analysis.h"

#endif // ORUS_H