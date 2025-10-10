//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/codegen/codegen.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2022 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Shared code generation utilities coordinating specialized
//               expression, statement, function, and module compilation units.

#include "compiler/codegen/codegen.h"
#include "compiler/codegen/expressions.h"
#include "compiler/codegen/statements.h"
#include "compiler/codegen/functions.h"
#include "compiler/codegen/modules.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/codegen/peephole.h"
#include "compiler/typed_ast.h"
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "compiler/symbol_table.h"
#include "compiler/scope_stack.h"
#include "compiler/error_reporter.h"
#include "config/config.h"
#include "type/type.h"
#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "vm/vm_string_ops.h"
#include "vm/module_manager.h"
#include "errors/features/variable_errors.h"
#include "errors/features/control_flow_errors.h"
#include "internal/error_reporting.h"
#include "debug/debug_config.h"
#include "internal/strutil.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void predeclare_function_symbols(CompilerContext* ctx, TypedASTNode* ast) {
    if (!ctx || !ast || !ctx->symbols) {
        return;
    }

    if (!ctx->allocator) {
        return;
    }

    if (!ast->original || ast->original->type != NODE_PROGRAM) {
        return;
    }

    for (int i = 0; i < ast->typed.program.count; i++) {
        TypedASTNode* stmt = ast->typed.program.declarations[i];
        if (!stmt || !stmt->original || stmt->original->type != NODE_FUNCTION) {
            continue;
        }

        const char* name = stmt->original->function.name;
        if (!name || stmt->typed.function.isMethod) {
            continue;
        }

        if (resolve_symbol_local_only(ctx->symbols, name)) {
            continue;
        }

        int reg = compiler_alloc_global(ctx->allocator);
        if (reg == -1) {
            reg = compiler_alloc_frame(ctx->allocator);
            if (reg == -1) {
                continue;
            }
        }

        Type* function_type = stmt->resolvedType ? stmt->resolvedType : getPrimitiveType(TYPE_FUNCTION);
        if (!register_variable(ctx, ctx->symbols, name, reg, function_type, false, false,
                               stmt->original->location, true)) {
            compiler_free_register(ctx->allocator, reg);
            continue;
        }

        stmt->suggestedRegister = reg;
    }
}

bool repl_mode_active(void) {
    const OrusConfig* config = config_get_global();
    if (config && config->repl_mode) {
        return true;
    }
    if (vm.filePath && strcmp(vm.filePath, "<repl>") == 0) {
        return true;
    }
    return false;
}

void set_location_from_node(CompilerContext* ctx, TypedASTNode* node) {
    if (!ctx || !ctx->bytecode) {
        return;
    }
    if (node && node->original) {
        SrcLocation location = node->original->location;
        if (vm.filePath) {
            location.file = vm.filePath;
        }
        bytecode_set_location(ctx->bytecode, location);
    } else {
        bytecode_set_synthetic_location(ctx->bytecode);
    }
}

ScopeFrame* get_scope_frame_by_index(CompilerContext* ctx, int index) {
    if (!ctx || !ctx->scopes || index < 0) {
        return NULL;
    }
    return scope_stack_get_frame(ctx->scopes, index);
}


int lookup_variable(CompilerContext* ctx, const char* name) {
    if (!ctx || !ctx->symbols || !name) return -1;

    Symbol* symbol = resolve_symbol(ctx->symbols, name);
    if (symbol) {
        if (symbol->reg_allocation) {
            return symbol->reg_allocation->logical_id;
        } else {
            return symbol->legacy_register_id;
        }
    }

    return -1; // Variable not found
}

Symbol* register_variable(CompilerContext* ctx, SymbolTable* scope,
                          const char* name, int reg, Type* type,
                          bool is_mutable, bool declared_mutable,
                          SrcLocation location, bool is_initialized) {
    if (!ctx || !scope || !name) {
        return NULL;
    }

    Symbol* existing = resolve_symbol_local_only(scope, name);
    if (existing) {
        bool reported = false;
        if (ctx && ctx->errors) {
            if (existing->declaration_location.line > 0) {
                reported = error_reporter_add_feature_error(ctx->errors, E1011_VARIABLE_REDEFINITION,
                                                           location,
                                                           "Variable '%s' is already defined on line %d",
                                                           name, existing->declaration_location.line);
            } else {
                reported = error_reporter_add_feature_error(ctx->errors, E1011_VARIABLE_REDEFINITION,
                                                           location,
                                                           "Variable '%s' is already defined in this scope",
                                                           name);
            }
        }
        if (!reported) {
            report_variable_redefinition(location, name,
                                         existing->declaration_location.line);
        }
        ctx->has_compilation_errors = true;
        return NULL;
    }

    Symbol* symbol = declare_symbol_legacy(scope, name, type, is_mutable, reg,
                                           location, is_initialized);
    if (!symbol) {
        DEBUG_CODEGEN_PRINT("Error: Failed to register variable %s", name);
        ctx->has_compilation_errors = true;
    } else {
        symbol->declared_mutable = declared_mutable;
    }
    return symbol;
}

bool generate_bytecode_from_ast(CompilerContext* ctx) {
    if (!ctx || !ctx->optimized_ast) {
        DEBUG_CODEGEN_PRINT("Error: Invalid context or AST");
        return false;
    }

    DEBUG_CODEGEN_PRINT("🚀 Starting production-grade code generation...");
    DEBUG_CODEGEN_PRINT("Leveraging VM's 256 registers and 150+ specialized opcodes");
    DEBUG_CODEGEN_PRINT("ctx->optimized_ast = %p\n", (void*)ctx->optimized_ast);

    TypedASTNode* ast = ctx->optimized_ast;

    int initial_count = ctx->bytecode->count;

    if (ast->original->type == NODE_PROGRAM) {
        predeclare_function_symbols(ctx, ast);
        for (int i = 0; i < ast->typed.program.count; i++) {
            TypedASTNode* stmt = ast->typed.program.declarations[i];
            if (stmt) {
                compile_statement(ctx, stmt);
            }
        }
    } else {
        compile_statement(ctx, ast);
    }

    DEBUG_CODEGEN_PRINT("🔧 Applying bytecode optimizations...");
    apply_peephole_optimizations(ctx);

    bytecode_set_synthetic_location(ctx->bytecode);
    emit_byte_to_buffer(ctx->bytecode, OP_HALT);
    DEBUG_CODEGEN_PRINT("Emitted OP_HALT");

    int final_count = ctx->bytecode->count;
    int saved_instructions = initial_count > 0 ? initial_count - final_count + initial_count : 0;

    DEBUG_CODEGEN_PRINT("✅ Code generation completed, %d instructions generated\n", final_count);
    if (saved_instructions > 0) {
        DEBUG_CODEGEN_PRINT("🚀 Bytecode optimizations saved %d instructions (%.1f%% reduction)\n",
               saved_instructions, (float)saved_instructions / initial_count * 100);
    }

    if (ctx->has_compilation_errors) {
        DEBUG_CODEGEN_PRINT("❌ Code generation failed due to compilation errors");
        return false;
    }

    return true;
}
