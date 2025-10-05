#include "compiler/codegen/expressions.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/codegen/functions.h"
#include "compiler/codegen/modules.h"
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

static bool type_is_numeric(const Type* type) {
    if (!type) {
        return false;
    }
    switch (type->kind) {
        case TYPE_I32:
        case TYPE_I64:
        case TYPE_U32:
        case TYPE_U64:
        case TYPE_F64:
            return true;
        default:
            return false;
    }
}

static void format_match_literal(Value value, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    switch (value.type) {
        case VAL_BOOL:
            snprintf(buffer, size, "%s", AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_I32:
            snprintf(buffer, size, "%d", AS_I32(value));
            break;
        case VAL_I64:
            snprintf(buffer, size, "%lld", (long long)AS_I64(value));
            break;
        case VAL_U32:
            snprintf(buffer, size, "%u", AS_U32(value));
            break;
        case VAL_U64:
            snprintf(buffer, size, "%llu", (unsigned long long)AS_U64(value));
            break;
        case VAL_F64:
            snprintf(buffer, size, "%g", AS_F64(value));
            break;
        case VAL_STRING: {
            ObjString* str = AS_STRING(value);
            if (str && str->chars) {
                snprintf(buffer, size, "\"%s\"", str->chars);
            } else {
                snprintf(buffer, size, "<string>");
            }
            break;
        }
        default:
            snprintf(buffer, size, "<literal>");
            break;
    }
}

static TypeKind fallback_type_kind_from_value(Value value) {
    switch (value.type) {
        case VAL_I32:
            return TYPE_I32;
        case VAL_I64:
            return TYPE_I64;
        case VAL_U32:
            return TYPE_U32;
        case VAL_U64:
            return TYPE_U64;
        case VAL_F64:
            return TYPE_F64;
        case VAL_BOOL:
            return TYPE_BOOL;
        case VAL_STRING:
            return TYPE_STRING;
        default:
            return TYPE_I32;
    }
}

static TypeKind fallback_type_kind_from_ast(const ASTNode* node) {
    if (!node) {
        return TYPE_I32;
    }

    if (node->dataType &&
        node->dataType->kind != TYPE_ERROR &&
        node->dataType->kind != TYPE_UNKNOWN) {
        if (node->dataType->kind == TYPE_ARRAY &&
            node->dataType->info.array.elementType) {
            return node->dataType->info.array.elementType->kind;
        }
        return node->dataType->kind;
    }

    switch (node->type) {
        case NODE_LITERAL:
            return fallback_type_kind_from_value(node->literal.value);
        case NODE_INDEX_ACCESS:
            if (node->indexAccess.array) {
                return fallback_type_kind_from_ast(node->indexAccess.array);
            }
            break;
        case NODE_UNARY:
            if (node->unary.operand) {
                return fallback_type_kind_from_ast(node->unary.operand);
            }
            break;
        default:
            break;
    }

    return TYPE_I32;
}

static Type* unwrap_struct_type(Type* type) {
    if (!type) {
        return NULL;
    }

    if (type->kind == TYPE_INSTANCE && type->info.instance.base) {
        return type->info.instance.base;
    }

    return type;
}

int resolve_struct_field_index(Type* struct_type, const char* field_name) {
    if (!struct_type || !field_name) {
        return -1;
    }

    Type* base = unwrap_struct_type(struct_type);
    if (!base || base->kind != TYPE_STRUCT) {
        return -1;
    }

    TypeExtension* ext = get_type_extension(base);
    if (!ext || !ext->extended.structure.fields) {
        return -1;
    }

    for (int i = 0; i < ext->extended.structure.fieldCount; i++) {
        FieldInfo* info = &ext->extended.structure.fields[i];
        if (info && info->name && info->name->chars &&
            strcmp(info->name->chars, field_name) == 0) {
            return i;
        }
    }

    return -1;
}

static TypedASTNode* find_struct_literal_value(TypedASTNode* literal,
                                               const char* field_name) {
    if (!literal || !field_name || !literal->typed.structLiteral.values ||
        !literal->typed.structLiteral.fields) {
        return NULL;
    }

    for (int i = 0; i < literal->typed.structLiteral.fieldCount; i++) {
        StructLiteralField* field = &literal->typed.structLiteral.fields[i];
        if (field && field->name && strcmp(field->name, field_name) == 0) {
            return literal->typed.structLiteral.values[i];
        }
    }

    return NULL;
}

char* create_method_symbol_name(const char* struct_name, const char* method_name) {
    if (!struct_name || !method_name) {
        return NULL;
    }

    size_t struct_len = strlen(struct_name);
    size_t method_len = strlen(method_name);
    size_t total = struct_len + 1 + method_len + 1;

    char* combined = malloc(total);
    if (!combined) {
        return NULL;
    }

    snprintf(combined, total, "%s.%s", struct_name, method_name);
    return combined;
}

static int compile_struct_method_call(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->typed.call.callee || !call->original) {
        return -1;
    }

    TypedASTNode* callee = call->typed.call.callee;
    if (!callee->original || callee->original->type != NODE_MEMBER_ACCESS) {
        return -1;
    }

    const char* method_name = callee->typed.member.member;
    TypedASTNode* object_node = callee->typed.member.object;
    bool is_instance_method = callee->typed.member.isInstanceMethod;

    const char* struct_name = NULL;
    Type* object_type = object_node ? object_node->resolvedType : NULL;
    if (!object_type && object_node && object_node->original) {
        object_type = object_node->original->dataType;
    }
    Type* base_struct = unwrap_struct_type(object_type);
    if (base_struct) {
        TypeExtension* ext = get_type_extension(base_struct);
        if (ext && ext->extended.structure.name && ext->extended.structure.name->chars) {
            struct_name = ext->extended.structure.name->chars;
        }
    }

    if (!struct_name && object_node && object_node->original &&
        object_node->original->type == NODE_IDENTIFIER) {
        struct_name = object_node->original->identifier.name;
    }

    if (!struct_name) {
        if (ctx->errors) {
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               call->original->location,
                               "Cannot resolve struct for method call",
                               "Ensure the method is called on a struct instance or type.",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    char* mangled_name = create_method_symbol_name(struct_name, method_name);
    if (!mangled_name) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate method symbol name buffer\n");
        return -1;
    }

    int callee_reg = lookup_variable(ctx, mangled_name);
    if (callee_reg == -1 && method_name) {
        callee_reg = lookup_variable(ctx, method_name);
    }

    if (callee_reg == -1) {
        if (ctx->errors) {
            char message[256];
            snprintf(message, sizeof(message),
                     "Unknown method '%s' on struct '%s'",
                     method_name ? method_name : "<unknown>", struct_name);
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               call->original->location,
                               message,
                               "Define the method in an impl block before calling it.",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        free(mangled_name);
        return -1;
    }

    int explicit_arg_count = call->original->call.argCount;
    int total_args = explicit_arg_count + (is_instance_method ? 1 : 0);

    int* arg_regs = NULL;
    int* temp_arg_regs = NULL;
    int first_arg_reg = 0;

    if (total_args > 0) {
        arg_regs = malloc(sizeof(int) * total_args);
        if (!arg_regs) {
            free(mangled_name);
            return -1;
        }

        for (int i = 0; i < total_args; i++) {
            arg_regs[i] = compiler_alloc_temp(ctx->allocator);
            if (arg_regs[i] == -1) {
                for (int j = 0; j < i; j++) {
                    if (arg_regs[j] >= MP_TEMP_REG_START && arg_regs[j] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[j]);
                    }
                }
                free(arg_regs);
                free(mangled_name);
                return -1;
            }
            if (i == 0) {
                first_arg_reg = arg_regs[i];
            }
        }

        temp_arg_regs = malloc(sizeof(int) * total_args);
        if (!temp_arg_regs) {
            for (int i = 0; i < total_args; i++) {
                if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, arg_regs[i]);
                }
            }
            free(arg_regs);
            free(mangled_name);
            return -1;
        }
    }

    int compiled_count = 0;

    if (is_instance_method) {
        if (!object_node) {
            if (temp_arg_regs) free(temp_arg_regs);
            if (arg_regs) {
                for (int i = 0; i < total_args; i++) {
                    if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[i]);
                    }
                }
                free(arg_regs);
            }
            free(mangled_name);
            return -1;
        }

        int self_reg = compile_expression(ctx, object_node);
        if (self_reg == -1) {
            if (temp_arg_regs) free(temp_arg_regs);
            if (arg_regs) {
                for (int i = 0; i < total_args; i++) {
                    if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[i]);
                    }
                }
                free(arg_regs);
            }
            free(mangled_name);
            return -1;
        }
        if (temp_arg_regs) {
            temp_arg_regs[compiled_count++] = self_reg;
        }
    }

    for (int i = 0; i < explicit_arg_count; i++) {
        TypedASTNode* arg_node = (call->typed.call.args && i < call->typed.call.argCount)
                                 ? call->typed.call.args[i]
                                 : NULL;
        if (!arg_node) {
            if (temp_arg_regs) {
                for (int j = 0; j < compiled_count; j++) {
                    int reg = temp_arg_regs[j];
                    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, reg);
                    }
                }
                free(temp_arg_regs);
            }
            if (arg_regs) {
                for (int j = 0; j < total_args; j++) {
                    if (arg_regs[j] >= MP_TEMP_REG_START && arg_regs[j] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[j]);
                    }
                }
                free(arg_regs);
            }
            free(mangled_name);
            return -1;
        }

        int arg_reg = compile_expression(ctx, arg_node);
        if (arg_reg == -1) {
            if (temp_arg_regs) {
                for (int j = 0; j < compiled_count; j++) {
                    int reg = temp_arg_regs[j];
                    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, reg);
                    }
                }
                free(temp_arg_regs);
            }
            if (arg_regs) {
                for (int j = 0; j < total_args; j++) {
                    if (arg_regs[j] >= MP_TEMP_REG_START && arg_regs[j] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[j]);
                    }
                }
                free(arg_regs);
            }
            free(mangled_name);
            return -1;
        }

        if (temp_arg_regs) {
            temp_arg_regs[compiled_count++] = arg_reg;
        }
    }

    if (total_args > 0 && temp_arg_regs) {
        for (int i = 0; i < total_args; i++) {
            if (temp_arg_regs[i] != arg_regs[i]) {
                emit_move(ctx, arg_regs[i], temp_arg_regs[i]);
                if (temp_arg_regs[i] >= MP_TEMP_REG_START && temp_arg_regs[i] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, temp_arg_regs[i]);
                }
            }
        }
        free(temp_arg_regs);
        temp_arg_regs = NULL;
    }

    int return_reg = compiler_alloc_temp(ctx->allocator);
    if (return_reg == -1) {
        if (arg_regs) {
            for (int i = 0; i < total_args; i++) {
                if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, arg_regs[i]);
                }
            }
            free(arg_regs);
        }
        free(mangled_name);
        return -1;
    }

    set_location_from_node(ctx, call);
    int actual_first_arg = (total_args > 0) ? first_arg_reg : 0;
    emit_instruction_to_buffer(ctx->bytecode, OP_CALL_R, callee_reg, actual_first_arg, total_args);
    emit_byte_to_buffer(ctx->bytecode, return_reg);

    if (arg_regs) {
        for (int i = 0; i < total_args; i++) {
            if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, arg_regs[i]);
            }
        }
        free(arg_regs);
    }

    free(mangled_name);
    return return_reg;
}

// ===== CODE GENERATION COORDINATOR =====
// Orchestrates bytecode generation and low-level optimizations
// Delegates to specific codegen algorithms

// Add or reuse an upvalue for the current function
static int add_upvalue(CompilerContext* ctx, bool isLocal, uint8_t index) {
    if (!ctx) return -1;

    // Check if upvalue already exists
    for (int i = 0; i < ctx->upvalue_count; i++) {
        if (ctx->upvalues[i].isLocal == isLocal && ctx->upvalues[i].index == index) {
            return i;
        }
    }

    // Ensure capacity
    if (ctx->upvalue_count >= ctx->upvalue_capacity) {
        int new_cap = ctx->upvalue_capacity == 0 ? 8 : ctx->upvalue_capacity * 2;
        ctx->upvalues = realloc(ctx->upvalues, sizeof(UpvalueInfo) * new_cap);
        if (!ctx->upvalues) {
            ctx->upvalue_capacity = ctx->upvalue_count = 0;
            return -1;
        }
        ctx->upvalue_capacity = new_cap;
    }

    ctx->upvalues[ctx->upvalue_count].isLocal = isLocal;
    ctx->upvalues[ctx->upvalue_count].index = index;
    return ctx->upvalue_count++;
}

// Resolve variable access, tracking upvalues if needed
int resolve_variable_or_upvalue(CompilerContext* ctx, const char* name,
                                       bool* is_upvalue, int* upvalue_index) {
    if (!ctx || !ctx->symbols || !name) return -1;

    // Traverse current function's scopes to find a regular variable
    SymbolTable* table = ctx->symbols;
    while (table && table->scope_depth >= ctx->function_scope_depth) {
        Symbol* local = resolve_symbol_local_only(table, name);
        if (local) {
            if (is_upvalue) *is_upvalue = false;
            if (upvalue_index) *upvalue_index = -1;
            return local->reg_allocation ?
                local->reg_allocation->logical_id : local->legacy_register_id;
        }
        table = table->parent;
    }

    // If compiling a function, search outer scopes as potential upvalues
    if (ctx->compiling_function) {
        while (table) {
            Symbol* symbol = resolve_symbol_local_only(table, name);
            if (symbol) {
                int reg = symbol->reg_allocation ?
                    symbol->reg_allocation->logical_id : symbol->legacy_register_id;
                if (table->parent == NULL) {
                    if (is_upvalue) *is_upvalue = false;
                    if (upvalue_index) *upvalue_index = -1;
                    return reg;
                }
                if (is_upvalue) *is_upvalue = true;
                int index = add_upvalue(ctx, true, (uint8_t)reg);
                if (upvalue_index) *upvalue_index = index;
                return reg;
            }
            table = table->parent;
        }
    }

    return -1; // Not found
}

// ===== VM OPCODE SELECTION =====

uint8_t select_optimal_opcode(const char* op, Type* type) {
    if (!op || !type) {
        DEBUG_CODEGEN_PRINT("select_optimal_opcode: op=%s, type=%p", op ? op : "NULL", (void*)type);
        return OP_HALT; // Fallback
    }
    
    DEBUG_CODEGEN_PRINT("select_optimal_opcode: op='%s', type->kind=%d", op, type->kind);
    
    // Convert Type kind to RegisterType for opcode selection
    RegisterType reg_type;
    switch (type->kind) {
        case TYPE_I32: 
            reg_type = REG_TYPE_I32; 
            DEBUG_CODEGEN_PRINT("Converting TYPE_I32 (%d) to REG_TYPE_I32 (%d)", TYPE_I32, REG_TYPE_I32);
            break;
        case TYPE_I64: 
            reg_type = REG_TYPE_I64; 
            DEBUG_CODEGEN_PRINT("Converting TYPE_I64 (%d) to REG_TYPE_I64 (%d)", TYPE_I64, REG_TYPE_I64);
            break;
        case TYPE_U32: 
            reg_type = REG_TYPE_U32; 
            DEBUG_CODEGEN_PRINT("Converting TYPE_U32 (%d) to REG_TYPE_U32 (%d)", TYPE_U32, REG_TYPE_U32);
            break;
        case TYPE_U64: 
            reg_type = REG_TYPE_U64; 
            DEBUG_CODEGEN_PRINT("Converting TYPE_U64 (%d) to REG_TYPE_U64 (%d)", TYPE_U64, REG_TYPE_U64);
            break;
        case TYPE_F64: 
            reg_type = REG_TYPE_F64; 
            DEBUG_CODEGEN_PRINT("Converting TYPE_F64 (%d) to REG_TYPE_F64 (%d)", TYPE_F64, REG_TYPE_F64);
            break;
        case TYPE_BOOL:
            reg_type = REG_TYPE_BOOL;
            DEBUG_CODEGEN_PRINT("Converting TYPE_BOOL (%d) to REG_TYPE_BOOL (%d)", TYPE_BOOL, REG_TYPE_BOOL);
            break;
        case TYPE_STRING:
            reg_type = REG_TYPE_HEAP;
            DEBUG_CODEGEN_PRINT("Converting TYPE_STRING (%d) to REG_TYPE_HEAP (%d)", TYPE_STRING, REG_TYPE_HEAP);
            break;
        case TYPE_VOID:
            reg_type = REG_TYPE_I64;  // Void values should not reach arithmetic, but keep legacy fallback.
            DEBUG_CODEGEN_PRINT("WORKAROUND: Converting TYPE_VOID (%d) to REG_TYPE_I64 (%d)", type->kind, REG_TYPE_I64);
            break;
        default:
            DEBUG_CODEGEN_PRINT("Warning: Unsupported type %d for opcode selection", type->kind);
            DEBUG_CODEGEN_PRINT("TYPE_I32=%d, TYPE_I64=%d, TYPE_U32=%d, TYPE_U64=%d, TYPE_F64=%d, TYPE_BOOL=%d", 
                   TYPE_I32, TYPE_I64, TYPE_U32, TYPE_U64, TYPE_F64, TYPE_BOOL);
            return OP_HALT;
    }
    
    DEBUG_CODEGEN_PRINT("Converting TYPE_%d to REG_TYPE_%d for opcode selection", type->kind, reg_type);
    
    // Check for logical operations on bool
    if (reg_type == REG_TYPE_BOOL) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_BOOL logical operation: %s", op);

        if (strcmp(op, "and") == 0) return OP_AND_BOOL_R;
        if (strcmp(op, "or") == 0) return OP_OR_BOOL_R;
        if (strcmp(op, "not") == 0) return OP_NOT_BOOL_R;

        // Comparison operators (result is boolean)
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }

    // Heap-backed values (strings, arrays, etc.) use the boxed register path.
    if (reg_type == REG_TYPE_HEAP) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_HEAP operation: %s", op);

        if (strcmp(op, "+") == 0) {
            // OP_ADD_I32_R performs boxed addition and includes the string
            // concatenation slow path used by the interpreter.
            return OP_ADD_I32_R;
        }

        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;

        // Fall through for unsupported operations on heap values.
        return OP_HALT;
    }
    
    // Check for arithmetic operations on i32
    if (reg_type == REG_TYPE_I32) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_I32 arithmetic operation: %s", op);
        
        // Arithmetic and bitwise operators
        if (strcmp(op, "+") == 0) return OP_ADD_I32_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_I32_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_I32_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_I32_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_I32_TYPED;
        if (strcmp(op, "and") == 0) return OP_AND_I32_R;
        if (strcmp(op, "or") == 0) return OP_OR_I32_R;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_I32_TYPED;
        if (strcmp(op, ">") == 0) return OP_GT_I32_TYPED;
        if (strcmp(op, "<=") == 0) return OP_LE_I32_TYPED;
        if (strcmp(op, ">=") == 0) return OP_GE_I32_TYPED;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }
    
    // Check for arithmetic operations on i64
    if (reg_type == REG_TYPE_I64) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_I64 arithmetic operation: %s", op);
        
        // Arithmetic operators
        if (strcmp(op, "+") == 0) {
            DEBUG_CODEGEN_PRINT("Returning OP_ADD_I64_TYPED for i64 addition");
            return OP_ADD_I64_TYPED;
        }
        if (strcmp(op, "-") == 0) return OP_SUB_I64_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_I64_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_I64_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_I64_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_I64_TYPED;
        if (strcmp(op, ">") == 0) return OP_GT_I64_TYPED;
        if (strcmp(op, "<=") == 0) return OP_LE_I64_TYPED;
        if (strcmp(op, ">=") == 0) return OP_GE_I64_TYPED;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }
    
    // Check for arithmetic operations on u32
    if (reg_type == REG_TYPE_U32) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_U32 arithmetic operation: %s", op);
        
        // Arithmetic operators
        if (strcmp(op, "+") == 0) return OP_ADD_U32_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_U32_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_U32_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_U32_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_U32_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_U32_TYPED;
        if (strcmp(op, ">") == 0) return OP_GT_U32_TYPED;
        if (strcmp(op, "<=") == 0) return OP_LE_U32_TYPED;
        if (strcmp(op, ">=") == 0) return OP_GE_U32_TYPED;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }
    
    // Check for arithmetic operations on u64
    if (reg_type == REG_TYPE_U64) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_U64 arithmetic operation: %s", op);
        
        // Arithmetic operators
        if (strcmp(op, "+") == 0) return OP_ADD_U64_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_U64_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_U64_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_U64_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_U64_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_U64_TYPED;
        if (strcmp(op, ">") == 0) return OP_GT_U64_TYPED;
        if (strcmp(op, "<=") == 0) return OP_LE_U64_TYPED;
        if (strcmp(op, ">=") == 0) return OP_GE_U64_TYPED;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }
    
    // Check for arithmetic operations on f64
    if (reg_type == REG_TYPE_F64) {
        DEBUG_CODEGEN_PRINT("Handling REG_TYPE_F64 arithmetic operation: %s", op);
        
        // Arithmetic operators
        if (strcmp(op, "+") == 0) return OP_ADD_F64_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_F64_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_F64_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_F64_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_F64_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_F64_TYPED;
        if (strcmp(op, ">") == 0) return OP_GT_F64_TYPED;
        if (strcmp(op, "<=") == 0) return OP_LE_F64_TYPED;
        if (strcmp(op, ">=") == 0) return OP_GE_F64_TYPED;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_NE_R;
    }
    
    // For other types, use existing logic but simplified for debugging
    DEBUG_CODEGEN_PRINT("Warning: Unhandled register type %d for operation %s", reg_type, op);
    return OP_HALT;
}

// Helper function to get cast opcode for type coercion
uint8_t get_cast_opcode(TypeKind from_type, TypeKind to_type) {
    // Handle same-type (no cast needed)
    if (from_type == to_type) {
        return OP_HALT; // No cast needed
    }
    
    // i32 source casts
    if (from_type == TYPE_I32) {
        switch (to_type) {
            case TYPE_I64: return OP_I32_TO_I64_R;
            case TYPE_F64: return OP_I32_TO_F64_R;
            case TYPE_U32: return OP_I32_TO_U32_R;
            case TYPE_U64: return OP_I32_TO_U64_R;
            case TYPE_BOOL: return OP_I32_TO_BOOL_R;
            default: break;
        }
    }
    
    // i64 source casts
    if (from_type == TYPE_I64) {
        switch (to_type) {
            case TYPE_I32: return OP_I64_TO_I32_R;
            case TYPE_F64: return OP_I64_TO_F64_R;
            case TYPE_U64: return OP_I64_TO_U64_R;
            case TYPE_U32: return OP_I64_TO_U32_R;
            default: break;
        }
    }
    
    // u32 source casts
    if (from_type == TYPE_U32) {
        switch (to_type) {
            case TYPE_I32: return OP_U32_TO_I32_R;
            case TYPE_F64: return OP_U32_TO_F64_R;
            case TYPE_U64: return OP_U32_TO_U64_R;
            case TYPE_I64: return OP_U32_TO_U64_R; // Treat as u64 then interpret as i64
            default: break;
        }
    }
    
    // u64 source casts
    if (from_type == TYPE_U64) {
        switch (to_type) {
            case TYPE_I32: return OP_U64_TO_I32_R;
            case TYPE_I64: return OP_U64_TO_I64_R;
            case TYPE_F64: return OP_U64_TO_F64_R;
            case TYPE_U32: return OP_U64_TO_U32_R;
            default: break;
        }
    }
    
    // f64 source casts
    if (from_type == TYPE_F64) {
        switch (to_type) {
            case TYPE_I32: return OP_F64_TO_I32_R;
            case TYPE_I64: return OP_F64_TO_I64_R;
            case TYPE_U32: return OP_F64_TO_U32_R;
            case TYPE_U64: return OP_F64_TO_U64_R;
            default: break;
        }
    }
    
    DEBUG_CODEGEN_PRINT("Warning: No cast opcode for %d -> %d", from_type, to_type);
    return OP_HALT; // Unsupported cast
}

// ===== INSTRUCTION EMISSION =====

void emit_typed_instruction(CompilerContext* ctx, uint8_t opcode, int dst, int src1, int src2) {
    emit_instruction_to_buffer(ctx->bytecode, opcode, dst, src1, src2);
}

void emit_load_constant(CompilerContext* ctx, int reg, Value constant) {
    // Use VM's specialized constant loading for optimal performance
    switch (constant.type) {
        case VAL_I32: {
            // OP_LOAD_I32_CONST: Add to constant pool and reference by index
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_I32_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_I32_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_I32_CONST R%d, #%d (%d)", 
                       reg, const_index, AS_I32(constant));
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add i32 constant to pool");
            }
            break;
        }
            
        case VAL_I64: {
            // OP_LOAD_I64_CONST: Add to constant pool and reference by index
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_I64_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_I64_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_I64_CONST R%d, #%d (%lld)\n", 
                       reg, const_index, (long long)AS_I64(constant));
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add i64 constant to pool");
            }
            break;
        }
            
        case VAL_U32: {
            // Use generic OP_LOAD_CONST for u32 - no specialized opcode available
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_CONST R%d, #%d (%u)\n", 
                       reg, const_index, AS_U32(constant));
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add u32 constant to pool");
            }
            break;
        }
            
        case VAL_U64: {
            // Use generic OP_LOAD_CONST for u64 - no specialized opcode available
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_CONST R%d, #%d (%llu)\n", 
                       reg, const_index, (unsigned long long)AS_U64(constant));
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add u64 constant to pool");
            }
            break;
        }
            
        case VAL_F64: {
            // OP_LOAD_F64_CONST: Add to constant pool and reference by index
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_F64_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_F64_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_F64_CONST R%d, #%d (%.2f)\n", 
                       reg, const_index, AS_F64(constant));
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add f64 constant to pool");
            }
            break;
        }
            
        case VAL_BOOL: {
            // Use dedicated boolean opcodes for proper type safety
            if (AS_BOOL(constant)) {
                // OP_LOAD_TRUE format: opcode + register (2 bytes)
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_TRUE);
                emit_byte_to_buffer(ctx->bytecode, reg);
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_TRUE R%d", reg);
            } else {
                // OP_LOAD_FALSE format: opcode + register (2 bytes)
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_FALSE);
                emit_byte_to_buffer(ctx->bytecode, reg);
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_FALSE R%d", reg);
            }
            break;
        }
            
        case VAL_STRING: {
            // String constants using VM's string support with constant pool
            // Add string to constant pool and get index
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_CONST R%d, #%d \"%s\"\n",
                       reg, const_index, AS_STRING(constant)->chars);
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add string constant to pool");
            }
            break;
        }

        case VAL_FUNCTION:
        case VAL_CLOSURE: {
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_CONST R%d, #%d (function)\n",
                       reg, const_index);
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add function constant to pool");
            }
            break;
        }
            
        case VAL_ARRAY:
        case VAL_ERROR:
        case VAL_RANGE_ITERATOR:
        case VAL_ARRAY_ITERATOR:
        default: {
            // Fallback for complex or object types - use generic constant loader
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF);
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);
                DEBUG_CODEGEN_PRINT("Emitted OP_LOAD_CONST R%d, #%d (type=%d)\n",
                       reg, const_index, constant.type);
            } else {
                DEBUG_CODEGEN_PRINT("Error: Failed to add constant of type %d to pool\n",
                       constant.type);
            }
            break;
        }
    }
}

void emit_binary_op(CompilerContext* ctx, const char* op, Type* operand_type, int dst, int src1, int src2) {
    // Debug output removed
    int operand_kind = operand_type ? (int)operand_type->kind : -1;
    DEBUG_CODEGEN_PRINT("emit_binary_op called: op='%s', type=%d, dst=R%d, src1=R%d, src2=R%d\n",
           op, operand_kind, dst, src1, src2);
    (void)operand_kind;

    uint8_t opcode = select_optimal_opcode(op, operand_type);
    // Debug output removed
    DEBUG_CODEGEN_PRINT("select_optimal_opcode returned: %d (OP_HALT=%d)\n", opcode, OP_HALT);

    if (opcode == OP_HALT) {
        // Fallback: emit a conservative boxed operation when the typed opcode
        // selection fails (e.g. due to HM type inference holes). This prevents
        // silently skipping the operation and guarantees we still compute a
        // boolean result at runtime.
        if (strcmp(op, "+") == 0) opcode = OP_ADD_I32_R;
        else if (strcmp(op, "-") == 0) opcode = OP_SUB_I32_R;
        else if (strcmp(op, "*") == 0) opcode = OP_MUL_I32_R;
        else if (strcmp(op, "/") == 0) opcode = OP_DIV_I32_R;
        else if (strcmp(op, "%") == 0) opcode = OP_MOD_I32_R;
        else if (strcmp(op, "<") == 0) opcode = OP_LT_I32_R;
        else if (strcmp(op, ">") == 0) opcode = OP_GT_I32_R;
        else if (strcmp(op, "<=") == 0) opcode = OP_LE_I32_R;
        else if (strcmp(op, ">=") == 0) opcode = OP_GE_I32_R;
        else if (strcmp(op, "==") == 0) opcode = OP_EQ_R;
        else if (strcmp(op, "!=") == 0) opcode = OP_NE_R;
    }

    if (opcode != OP_HALT) {
        emit_typed_instruction(ctx, opcode, dst, src1, src2);

        // Check if this is a comparison operation (returns boolean)
        bool is_comparison = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                             strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                             strcmp(op, "==") == 0 || strcmp(op, "!=") == 0);
        
        if (is_comparison) {
            DEBUG_CODEGEN_PRINT("Emitted %s_CMP R%d, R%d, R%d (result: boolean)\n", op, dst, src1, src2);
        } else {
            DEBUG_CODEGEN_PRINT("Emitted %s_TYPED R%d, R%d, R%d\n", op, dst, src1, src2);
        }
    } else {
        DEBUG_CODEGEN_PRINT("ERROR: No valid opcode found for operation '%s' with type %d\n",
               op, operand_kind);
    }
}

void emit_move(CompilerContext* ctx, int dst, int src) {
    // OP_MOVE format: opcode + dst_reg + src_reg (3 bytes total)
    emit_byte_to_buffer(ctx->bytecode, OP_MOVE);
    emit_byte_to_buffer(ctx->bytecode, dst);
    emit_byte_to_buffer(ctx->bytecode, src);
    DEBUG_CODEGEN_PRINT("Emitted OP_MOVE R%d, R%d (3 bytes)\n", dst, src);
}

void ensure_i32_typed_register(CompilerContext* ctx, int reg, const TypedASTNode* source) {
    if (!ctx || !ctx->bytecode) {
        return;
    }
    if (reg < 0 || reg >= REGISTER_COUNT) {
        return;
    }

    if (source && source->resolvedType && source->resolvedType->kind != TYPE_I32) {
        return;
    }

    emit_byte_to_buffer(ctx->bytecode, OP_MOVE_I32);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)reg);
}

static TypedASTNode* get_call_argument_node(TypedASTNode* call, int index,
                                            bool* should_free) {
    if (should_free) {
        *should_free = false;
    }
    if (!call || !call->original) {
        return NULL;
    }

    if (call->typed.call.args && index < call->typed.call.argCount) {
        return call->typed.call.args[index];
    }

    if (call->original->call.args && index < call->original->call.argCount) {
        if (should_free) {
            *should_free = true;
        }
        return create_typed_ast_node(call->original->call.args[index]);
    }

    return NULL;
}

static int compile_builtin_array_push(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 2) {
        DEBUG_CODEGEN_PRINT("Error: push() expects 2 arguments, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_array = false;
    bool free_value = false;
    TypedASTNode* array_arg = get_call_argument_node(call, 0, &free_array);
    TypedASTNode* value_arg = get_call_argument_node(call, 1, &free_value);
    if (!array_arg || !value_arg) {
        if (free_array && array_arg) free_typed_ast_node(array_arg);
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int array_reg = compile_expression(ctx, array_arg);
    if (array_reg == -1) {
        if (free_array) free_typed_ast_node(array_arg);
        if (free_value) free_typed_ast_node(value_arg);
        return -1;
    }

    int value_reg = compile_expression(ctx, value_arg);
    if (value_reg == -1) {
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        if (free_array) free_typed_ast_node(array_arg);
        if (free_value) free_typed_ast_node(value_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_PUSH_R);
    emit_byte_to_buffer(ctx->bytecode, array_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (value_reg != array_reg &&
        value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    if (free_array) free_typed_ast_node(array_arg);
    if (free_value) free_typed_ast_node(value_arg);

    return array_reg;
}

static int compile_builtin_array_pop(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: pop() expects 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_array = false;
    TypedASTNode* array_arg = get_call_argument_node(call, 0, &free_array);
    if (!array_arg) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int array_reg = compile_expression(ctx, array_arg);
    if (array_reg == -1) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for pop() builtin\n");
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_POP_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, array_reg);

    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, array_reg);
    }

    if (free_array) free_typed_ast_node(array_arg);

    return result_reg;
}

static int compile_builtin_array_len(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: len() expects 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_array = false;
    TypedASTNode* array_arg = get_call_argument_node(call, 0, &free_array);
    if (!array_arg) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int array_reg = compile_expression(ctx, array_arg);
    if (array_reg == -1) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for len() builtin\n");
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_LEN_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, array_reg);

    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, array_reg);
    }

    if (free_array) free_typed_ast_node(array_arg);

    return result_reg;
}

static int compile_builtin_sorted(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: sorted() expects 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_array = false;
    TypedASTNode* array_arg = get_call_argument_node(call, 0, &free_array);
    if (!array_arg) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int array_reg = compile_expression(ctx, array_arg);
    if (array_reg == -1) {
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for sorted() builtin\n");
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        if (free_array) free_typed_ast_node(array_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_SORTED_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, array_reg);

    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, array_reg);
    }

    if (free_array) free_typed_ast_node(array_arg);

    return result_reg;
}

static int compile_builtin_range(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    int arg_count = call->original->call.argCount;
    if (arg_count < 1 || arg_count > 3) {
        DEBUG_CODEGEN_PRINT("Error: range() expects between 1 and 3 arguments, got %d\n",
                            arg_count);
        ctx->has_compilation_errors = true;
        return -1;
    }

    TypedASTNode* arg_nodes[3] = {NULL, NULL, NULL};
    bool free_nodes[3] = {false, false, false};
    int arg_regs[3] = {0, 0, 0};

    for (int i = 0; i < arg_count; i++) {
        arg_nodes[i] = get_call_argument_node(call, i, &free_nodes[i]);
        if (!arg_nodes[i]) {
            for (int j = 0; j < i; j++) {
                if (arg_regs[j] >= MP_TEMP_REG_START && arg_regs[j] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, arg_regs[j]);
                }
                if (free_nodes[j] && arg_nodes[j]) {
                    free_typed_ast_node(arg_nodes[j]);
                }
            }
            return -1;
        }

        int reg = compile_expression(ctx, arg_nodes[i]);
        if (reg == -1) {
            for (int j = 0; j < i; j++) {
                if (arg_regs[j] >= MP_TEMP_REG_START && arg_regs[j] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, arg_regs[j]);
                }
                if (free_nodes[j] && arg_nodes[j]) {
                    free_typed_ast_node(arg_nodes[j]);
                }
            }
            if (free_nodes[i] && arg_nodes[i]) {
                free_typed_ast_node(arg_nodes[i]);
            }
            return -1;
        }

        arg_regs[i] = reg;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for range() builtin\n");
        for (int i = 0; i < arg_count; i++) {
            if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, arg_regs[i]);
            }
            if (free_nodes[i] && arg_nodes[i]) {
                free_typed_ast_node(arg_nodes[i]);
            }
        }
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_RANGE_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)arg_count);
    emit_byte_to_buffer(ctx->bytecode, arg_regs[0]);
    emit_byte_to_buffer(ctx->bytecode, arg_regs[1]);
    emit_byte_to_buffer(ctx->bytecode, arg_regs[2]);

    for (int i = 0; i < arg_count; i++) {
        if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, arg_regs[i]);
        }
        if (free_nodes[i] && arg_nodes[i]) {
            free_typed_ast_node(arg_nodes[i]);
        }
    }
    return result_reg;
}

static int compile_builtin_input(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    int arg_count = call->original->call.argCount;
    if (arg_count < 0 || arg_count > 1) {
        DEBUG_CODEGEN_PRINT("Error: input() expects 0 or 1 arguments, got %d\n", arg_count);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_prompt = false;
    TypedASTNode* prompt_arg = NULL;
    int prompt_reg = 0;

    if (arg_count == 1) {
        prompt_arg = get_call_argument_node(call, 0, &free_prompt);
        if (!prompt_arg) {
            if (free_prompt && prompt_arg) free_typed_ast_node(prompt_arg);
            return -1;
        }

        prompt_reg = compile_expression(ctx, prompt_arg);
        if (prompt_reg == -1) {
            if (free_prompt && prompt_arg) free_typed_ast_node(prompt_arg);
            return -1;
        }
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for input() result\n");
        if (arg_count == 1 && prompt_reg >= MP_TEMP_REG_START && prompt_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, prompt_reg);
        }
        if (free_prompt && prompt_arg) free_typed_ast_node(prompt_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_INPUT_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)arg_count);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)prompt_reg);

    if (arg_count == 1 && prompt_reg >= MP_TEMP_REG_START && prompt_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, prompt_reg);
    }

    if (free_prompt && prompt_arg) free_typed_ast_node(prompt_arg);

    return result_reg;
}

static int compile_builtin_int(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: int() expects exactly 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_value = false;
    TypedASTNode* value_arg = get_call_argument_node(call, 0, &free_value);
    if (!value_arg) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int value_reg = compile_expression(ctx, value_arg);
    if (value_reg == -1) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for int() result\n");
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_PARSE_INT_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    if (free_value && value_arg) free_typed_ast_node(value_arg);

    return result_reg;
}

static int compile_builtin_float(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: float() expects exactly 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_value = false;
    TypedASTNode* value_arg = get_call_argument_node(call, 0, &free_value);
    if (!value_arg) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int value_reg = compile_expression(ctx, value_arg);
    if (value_reg == -1) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for float() result\n");
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_PARSE_FLOAT_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    if (free_value && value_arg) free_typed_ast_node(value_arg);

    return result_reg;
}

static int compile_builtin_typeof(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 1) {
        DEBUG_CODEGEN_PRINT("Error: typeof() expects exactly 1 argument, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_value = false;
    TypedASTNode* value_arg = get_call_argument_node(call, 0, &free_value);
    if (!value_arg) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int value_reg = compile_expression(ctx, value_arg);
    if (value_reg == -1) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for typeof() result\n");
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_TYPE_OF_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    if (free_value && value_arg) free_typed_ast_node(value_arg);

    return result_reg;
}

static int compile_builtin_istype(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 2) {
        DEBUG_CODEGEN_PRINT("Error: istype() expects exactly 2 arguments, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_value = false;
    TypedASTNode* value_arg = get_call_argument_node(call, 0, &free_value);
    if (!value_arg) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        return -1;
    }

    bool free_type = false;
    TypedASTNode* type_arg = get_call_argument_node(call, 1, &free_type);
    if (!type_arg) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        if (free_type && type_arg) free_typed_ast_node(type_arg);
        return -1;
    }

    int value_reg = compile_expression(ctx, value_arg);
    if (value_reg == -1) {
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        if (free_type && type_arg) free_typed_ast_node(type_arg);
        return -1;
    }

    int type_reg = compile_expression(ctx, type_arg);
    if (type_reg == -1) {
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        if (free_type && type_arg) free_typed_ast_node(type_arg);
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for istype() result\n");
        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        if (type_reg >= MP_TEMP_REG_START && type_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, type_reg);
        }
        if (free_value && value_arg) free_typed_ast_node(value_arg);
        if (free_type && type_arg) free_typed_ast_node(type_arg);
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_IS_TYPE_R);
    emit_byte_to_buffer(ctx->bytecode, result_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);
    emit_byte_to_buffer(ctx->bytecode, type_reg);

    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, value_reg);
    }
    if (type_reg >= MP_TEMP_REG_START && type_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, type_reg);
    }

    if (free_value && value_arg) free_typed_ast_node(value_arg);
    if (free_type && type_arg) free_typed_ast_node(type_arg);

    return result_reg;
}

static int compile_builtin_assert_eq(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original) {
        return -1;
    }

    if (call->original->call.argCount != 3) {
        DEBUG_CODEGEN_PRINT("Error: assert_eq() expects exactly 3 arguments, got %d\n",
                            call->original->call.argCount);
        ctx->has_compilation_errors = true;
        return -1;
    }

    bool free_nodes[3] = {false, false, false};
    TypedASTNode* args[3] = {NULL, NULL, NULL};
    int regs[3] = {-1, -1, -1};

    for (int i = 0; i < 3; i++) {
        args[i] = get_call_argument_node(call, i, &free_nodes[i]);
        if (!args[i]) {
            for (int j = 0; j <= i; j++) {
                if (regs[j] >= MP_TEMP_REG_START && regs[j] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, regs[j]);
                }
                if (free_nodes[j] && args[j]) {
                    free_typed_ast_node(args[j]);
                }
            }
            return -1;
        }
        regs[i] = compile_expression(ctx, args[i]);
        if (regs[i] == -1) {
            for (int j = 0; j <= i; j++) {
                if (regs[j] >= MP_TEMP_REG_START && regs[j] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, regs[j]);
                }
                if (free_nodes[j] && args[j]) {
                    free_typed_ast_node(args[j]);
                }
            }
            return -1;
        }
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for assert_eq() result\n");
        ctx->has_compilation_errors = true;
        for (int i = 0; i < 3; i++) {
            if (regs[i] >= MP_TEMP_REG_START && regs[i] <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, regs[i]);
            }
            if (free_nodes[i] && args[i]) {
                free_typed_ast_node(args[i]);
            }
        }
        return -1;
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ASSERT_EQ_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)regs[0]);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)regs[1]);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)regs[2]);

    for (int i = 0; i < 3; i++) {
        if (regs[i] >= MP_TEMP_REG_START && regs[i] <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, regs[i]);
        }
        if (free_nodes[i] && args[i]) {
            free_typed_ast_node(args[i]);
        }
    }

    return result_reg;
}

static int ensure_string_constant(CompilerContext* ctx, const char* text) {
    if (!ctx || !ctx->constants || !text) {
        return -1;
    }

    ObjString* interned = intern_string(text, (int)strlen(text));
    if (!interned) {
        return -1;
    }

    Value value = STRING_VAL(interned);
    return add_constant(ctx->constants, value);
}

static int compile_enum_variant_access(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr || !expr->original) {
        return -1;
    }

    if (expr->typed.member.enumVariantArity > 0) {
        if (ctx->errors) {
            char message[160];
            const char* variant = expr->typed.member.member ? expr->typed.member.member : "<variant>";
            int arity = expr->typed.member.enumVariantArity;
            snprintf(message, sizeof(message),
                     "Enum variant '%s' expects %d argument%s", variant, arity,
                     arity == 1 ? "" : "s");
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               expr->original->location,
                               message,
                               "Call the variant with parentheses and the required arguments.",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    const char* typeName = expr->typed.member.enumTypeName;
    if (!typeName && expr->typed.member.object && expr->typed.member.object->original &&
        expr->typed.member.object->original->type == NODE_IDENTIFIER) {
        typeName = expr->typed.member.object->original->identifier.name;
    }
    const char* variantName = expr->typed.member.member;

    if (!typeName || !variantName) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int typeConstIndex = ensure_string_constant(ctx, typeName);
    int variantConstIndex = ensure_string_constant(ctx, variantName);
    if (typeConstIndex < 0 || variantConstIndex < 0) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    set_location_from_node(ctx, expr);
    emit_byte_to_buffer(ctx->bytecode, OP_ENUM_NEW_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)expr->typed.member.enumVariantIndex);
    emit_byte_to_buffer(ctx->bytecode, 0); // payload count
    emit_byte_to_buffer(ctx->bytecode, 0); // payload start register
    emit_byte_to_buffer(ctx->bytecode, (typeConstIndex >> 8) & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, typeConstIndex & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, (variantConstIndex >> 8) & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, variantConstIndex & 0xFF);

    return result_reg;
}

static int compile_enum_constructor_call(CompilerContext* ctx, TypedASTNode* call) {
    if (!ctx || !call || !call->original || !call->typed.call.callee) {
        return -1;
    }

    TypedASTNode* callee = call->typed.call.callee;
    int expectedArgs = callee->typed.member.enumVariantArity;
    int providedArgs = call->original->call.argCount;
    if (providedArgs != expectedArgs) {
        if (ctx->errors) {
            char message[160];
            const char* variant = callee->typed.member.member ? callee->typed.member.member : "<variant>";
            snprintf(message, sizeof(message),
                     "Enum variant '%s' expects %d argument%s but got %d",
                     variant, expectedArgs, expectedArgs == 1 ? "" : "s", providedArgs);
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               call->original->location,
                               message,
                               "Adjust the constructor call to pass the correct number of arguments.",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    const char* typeName = callee->typed.member.enumTypeName;
    if (!typeName && callee->typed.member.object && callee->typed.member.object->original &&
        callee->typed.member.object->original->type == NODE_IDENTIFIER) {
        typeName = callee->typed.member.object->original->identifier.name;
    }
    const char* variantName = callee->typed.member.member;

    if (!typeName || !variantName) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int typeConstIndex = ensure_string_constant(ctx, typeName);
    int variantConstIndex = ensure_string_constant(ctx, variantName);
    if (typeConstIndex < 0 || variantConstIndex < 0) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int* arg_regs = NULL;
    int* temp_arg_regs = NULL;
    int payloadStart = 0;
    bool success = false;

    if (expectedArgs > 0) {
        arg_regs = calloc((size_t)expectedArgs, sizeof(int));
        temp_arg_regs = calloc((size_t)expectedArgs, sizeof(int));
        if (!arg_regs || !temp_arg_regs) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        for (int i = 0; i < expectedArgs; i++) {
            arg_regs[i] = compiler_alloc_temp(ctx->allocator);
            if (arg_regs[i] == -1) {
                ctx->has_compilation_errors = true;
                goto cleanup;
            }
        }

        payloadStart = arg_regs[0];

        for (int i = 0; i < expectedArgs; i++) {
            TypedASTNode* argNode = call->typed.call.args[i];
            if (!argNode) {
                ctx->has_compilation_errors = true;
                goto cleanup;
            }
            int tempReg = compile_expression(ctx, argNode);
            if (tempReg == -1) {
                ctx->has_compilation_errors = true;
                goto cleanup;
            }
            temp_arg_regs[i] = tempReg;
        }

        for (int i = 0; i < expectedArgs; i++) {
            if (temp_arg_regs[i] != arg_regs[i]) {
                emit_move(ctx, arg_regs[i], temp_arg_regs[i]);
                if (temp_arg_regs[i] >= MP_TEMP_REG_START && temp_arg_regs[i] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, temp_arg_regs[i]);
                    temp_arg_regs[i] = arg_regs[i];
                }
            }
        }
    }

    set_location_from_node(ctx, call);
    emit_byte_to_buffer(ctx->bytecode, OP_ENUM_NEW_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)callee->typed.member.enumVariantIndex);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)expectedArgs);
    emit_byte_to_buffer(ctx->bytecode, expectedArgs > 0 ? (uint8_t)payloadStart : 0);
    emit_byte_to_buffer(ctx->bytecode, (typeConstIndex >> 8) & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, typeConstIndex & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, (variantConstIndex >> 8) & 0xFF);
    emit_byte_to_buffer(ctx->bytecode, variantConstIndex & 0xFF);

    success = true;

cleanup:
    if (temp_arg_regs) {
        if (!success) {
            for (int i = 0; i < expectedArgs; i++) {
                if (temp_arg_regs[i] >= MP_TEMP_REG_START && temp_arg_regs[i] <= MP_TEMP_REG_END &&
                    (!arg_regs || temp_arg_regs[i] != arg_regs[i])) {
                    compiler_free_temp(ctx->allocator, temp_arg_regs[i]);
                }
            }
        }
        free(temp_arg_regs);
    }

    if (arg_regs) {
        for (int i = 0; i < expectedArgs; i++) {
            if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, arg_regs[i]);
            }
        }
        free(arg_regs);
    }

    if (!success) {
        if (result_reg >= MP_TEMP_REG_START && result_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, result_reg);
        }
        return -1;
    }

    return result_reg;
}

static int compile_enum_match_test(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr || !expr->typed.enumMatchTest.value) {
        return -1;
    }

    int variant_index = expr->typed.enumMatchTest.variantIndex;
    if (variant_index < 0 || variant_index > 255) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int enum_reg = compile_expression(ctx, expr->typed.enumMatchTest.value);
    if (enum_reg == -1) {
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        ctx->has_compilation_errors = true;
        if (enum_reg >= MP_TEMP_REG_START && enum_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, enum_reg);
        }
        return -1;
    }

    set_location_from_node(ctx, expr);
    emit_byte_to_buffer(ctx->bytecode, OP_ENUM_TAG_EQ_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)enum_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)variant_index);

    if (enum_reg >= MP_TEMP_REG_START && enum_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, enum_reg);
    }

    return result_reg;
}

static int compile_enum_payload_extract(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr || !expr->typed.enumPayload.value) {
        return -1;
    }

    int variant_index = expr->typed.enumPayload.variantIndex;
    int field_index = expr->typed.enumPayload.fieldIndex;
    if (variant_index < 0 || variant_index > 255 || field_index < 0 || field_index > 255) {
        ctx->has_compilation_errors = true;
        return -1;
    }

    int enum_reg = compile_expression(ctx, expr->typed.enumPayload.value);
    if (enum_reg == -1) {
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        ctx->has_compilation_errors = true;
        if (enum_reg >= MP_TEMP_REG_START && enum_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, enum_reg);
        }
        return -1;
    }

    set_location_from_node(ctx, expr);
    emit_byte_to_buffer(ctx->bytecode, OP_ENUM_PAYLOAD_R);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)enum_reg);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)variant_index);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)field_index);

    if (enum_reg >= MP_TEMP_REG_START && enum_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, enum_reg);
    }

    return result_reg;
}

static int compile_match_expression(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr || !expr->typed.matchExpr.subject || expr->typed.matchExpr.armCount <= 0) {
        return -1;
    }

    int scrutinee_reg = compile_expression(ctx, expr->typed.matchExpr.subject);
    if (scrutinee_reg == -1) {
        return -1;
    }

    int result_reg = compiler_alloc_temp(ctx->allocator);
    if (result_reg == -1) {
        if (scrutinee_reg >= MP_TEMP_REG_START && scrutinee_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, scrutinee_reg);
        }
        return -1;
    }

    SymbolTable* parent_scope = ctx->symbols;
    SymbolTable* match_scope = create_symbol_table(parent_scope);
    if (!match_scope) {
        compiler_free_temp(ctx->allocator, result_reg);
        if (scrutinee_reg >= MP_TEMP_REG_START && scrutinee_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, scrutinee_reg);
        }
        return -1;
    }

    ScopeFrame* match_frame = NULL;
    int match_frame_index = -1;

    ctx->symbols = match_scope;
    if (ctx->allocator) {
        compiler_enter_scope(ctx->allocator);
    }
    if (ctx->scopes) {
        match_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
        if (match_frame) {
            match_frame->symbols = match_scope;
            match_frame->start_offset = ctx->bytecode ? ctx->bytecode->count : 0;
            match_frame->end_offset = match_frame->start_offset;
            match_frame_index = match_frame->lexical_depth;
        }
    }

    if (expr->typed.matchExpr.tempName) {
        Type* scrutinee_type = expr->typed.matchExpr.subject ? expr->typed.matchExpr.subject->resolvedType : NULL;
        if (!register_variable(ctx, ctx->symbols, expr->typed.matchExpr.tempName, scrutinee_reg,
                               scrutinee_type, false, false, expr->original->location, true)) {
            if (ctx->allocator) {
                compiler_exit_scope(ctx->allocator);
            }
            free_symbol_table(match_scope);
            ctx->symbols = parent_scope;
            if (match_frame && ctx->scopes) {
                scope_stack_pop(ctx->scopes);
            }
            compiler_free_temp(ctx->allocator, result_reg);
            if (scrutinee_reg >= MP_TEMP_REG_START && scrutinee_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, scrutinee_reg);
            }
            return -1;
        }
    }

    int arm_count = expr->typed.matchExpr.armCount;
    int* false_jumps = NULL;
    int* end_jumps = NULL;
    bool success = true;

    typedef struct MatchLiteralEntry {
        Value value;
    } MatchLiteralEntry;

    MatchLiteralEntry* literal_entries = NULL;
    int literal_count = 0;
    int literal_capacity = 0;

    if (arm_count > 0) {
        false_jumps = calloc((size_t)arm_count, sizeof(int));
        end_jumps = calloc((size_t)arm_count, sizeof(int));
        if (!false_jumps || !end_jumps) {
            success = false;
        }
        if (false_jumps) {
            for (int i = 0; i < arm_count; i++) {
                false_jumps[i] = -1;
            }
        }
        if (end_jumps) {
            for (int i = 0; i < arm_count; i++) {
                end_jumps[i] = -1;
            }
        }
    }

    for (int i = 0; success && i < arm_count; i++) {
        TypedMatchArm* arm = &expr->typed.matchExpr.arms[i];

        if (arm->valuePattern && arm->valuePattern->original &&
            arm->valuePattern->original->type == NODE_LITERAL) {
            Value literal_value = arm->valuePattern->original->literal.value;
            bool duplicate_literal = false;
            for (int j = 0; j < literal_count; j++) {
                if (valuesEqual(literal_entries[j].value, literal_value)) {
                    duplicate_literal = true;
                    break;
                }
            }

            if (duplicate_literal) {
                char repr[128];
                format_match_literal(literal_value, repr, sizeof(repr));
                report_duplicate_literal_match_arm(arm->location, repr);
                ctx->has_compilation_errors = true;
                success = false;
            } else {
                if (literal_count == literal_capacity) {
                    int new_capacity = literal_capacity == 0 ? 4 : literal_capacity * 2;
                    MatchLiteralEntry* resized =
                        realloc(literal_entries, sizeof(MatchLiteralEntry) * (size_t)new_capacity);
                    if (!resized) {
                        ctx->has_compilation_errors = true;
                        success = false;
                    } else {
                        literal_entries = resized;
                        literal_capacity = new_capacity;
                    }
                }

                if (success && literal_count < literal_capacity) {
                    literal_entries[literal_count++].value = literal_value;
                }
            }
        }

        if (!success) {
            break;
        }

        int false_patch = -1;
        if (arm->condition) {
            int condition_reg = compile_expression(ctx, arm->condition);
            if (condition_reg == -1) {
                success = false;
            } else {
                set_location_from_node(ctx, arm->condition);
                emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_R);
                emit_byte_to_buffer(ctx->bytecode, (uint8_t)condition_reg);
                false_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_R);
                if (false_patch < 0) {
                    success = false;
                }
                if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, condition_reg);
                }
            }
        }

        SymbolTable* branch_parent = ctx->symbols;
        SymbolTable* branch_scope = create_symbol_table(branch_parent);
        ScopeFrame* branch_frame = NULL;
        int branch_frame_index = -1;
        if (!branch_scope) {
            success = false;
        } else {
            ctx->symbols = branch_scope;
            if (ctx->allocator) {
                compiler_enter_scope(ctx->allocator);
            }
            if (ctx->scopes) {
                branch_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
                if (branch_frame) {
                    branch_frame->symbols = branch_scope;
                    branch_frame->start_offset = ctx->bytecode ? ctx->bytecode->count : 0;
                    branch_frame->end_offset = branch_frame->start_offset;
                    branch_frame_index = branch_frame->lexical_depth;
                }
            }

            if (arm->payloadAccesses && arm->payloadCount > 0) {
                for (int j = 0; success && j < arm->payloadCount; j++) {
                    TypedASTNode* payload_node = arm->payloadAccesses[j];
                    const char* binding = (arm->payloadNames && j < arm->payloadCount) ? arm->payloadNames[j] : NULL;
                    if (!payload_node) {
                        continue;
                    }
                    int payload_reg = compile_expression(ctx, payload_node);
                    if (payload_reg == -1) {
                        success = false;
                        break;
                    }
                    if (binding) {
                        if (!register_variable(ctx, ctx->symbols, binding, payload_reg,
                                               payload_node->resolvedType, false, false,
                                               payload_node->original ? payload_node->original->location
                                                                      : expr->original->location,
                                               true)) {
                            success = false;
                            if (payload_reg >= MP_TEMP_REG_START && payload_reg <= MP_TEMP_REG_END) {
                                compiler_free_temp(ctx->allocator, payload_reg);
                            }
                            break;
                        }
                    } else {
                        if (payload_reg >= MP_TEMP_REG_START && payload_reg <= MP_TEMP_REG_END) {
                            compiler_free_temp(ctx->allocator, payload_reg);
                        }
                    }
                }
            }

            int body_reg = -1;
            if (success && arm->body) {
                body_reg = compile_expression(ctx, arm->body);
                if (body_reg == -1) {
                    success = false;
                }
            }

            if (success) {
                if (body_reg != result_reg) {
                    set_location_from_node(ctx, arm->body ? arm->body : expr);
                    emit_move(ctx, result_reg, body_reg);
                    if (body_reg >= MP_TEMP_REG_START && body_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, body_reg);
                    }
                }
            }

            if (branch_frame) {
                ScopeFrame* refreshed = get_scope_frame_by_index(ctx, branch_frame_index);
                if (refreshed) {
                    refreshed->end_offset = ctx->bytecode ? ctx->bytecode->count : refreshed->start_offset;
                }
                if (ctx->scopes) {
                    scope_stack_pop(ctx->scopes);
                }
            }
            if (ctx->allocator) {
                compiler_exit_scope(ctx->allocator);
            }
            free_symbol_table(branch_scope);
            ctx->symbols = branch_parent;
        }

        if (!success) {
            break;
        }

        set_location_from_node(ctx, expr);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP_SHORT);
        int end_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_SHORT);
        if (end_patch < 0) {
            success = false;
            break;
        }
        if (end_jumps) {
            end_jumps[i] = end_patch;
        }

        if (false_patch != -1) {
            if (!patch_jump(ctx->bytecode, false_patch, ctx->bytecode->count)) {
                success = false;
                break;
            }
        }
    }

    if (literal_entries) {
        free(literal_entries);
    }

    if (success && end_jumps) {
        int end_target = ctx->bytecode->count;
        for (int i = 0; i < arm_count; i++) {
            if (end_jumps[i] != -1) {
                if (!patch_jump(ctx->bytecode, end_jumps[i], end_target)) {
                    success = false;
                    break;
                }
            }
        }
    }

    if (end_jumps) {
        free(end_jumps);
    }
    if (false_jumps) {
        free(false_jumps);
    }

    if (match_frame) {
        ScopeFrame* refreshed = get_scope_frame_by_index(ctx, match_frame_index);
        if (refreshed) {
            refreshed->end_offset = ctx->bytecode ? ctx->bytecode->count : refreshed->start_offset;
        }
        if (ctx->scopes) {
            scope_stack_pop(ctx->scopes);
        }
    }
    if (ctx->allocator) {
        compiler_exit_scope(ctx->allocator);
    }
    free_symbol_table(match_scope);
    ctx->symbols = parent_scope;

    if (!success) {
        compiler_free_temp(ctx->allocator, result_reg);
        if (scrutinee_reg >= MP_TEMP_REG_START && scrutinee_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, scrutinee_reg);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    return result_reg;
}

bool evaluate_constant_i32(TypedASTNode* node, int32_t* out_value) {
    if (!node || !out_value || !node->original) {
        return false;
    }

    ASTNode* original = node->original;
    switch (original->type) {
        case NODE_LITERAL: {
            Value val = original->literal.value;
            switch (val.type) {
                case VAL_I32:
                    *out_value = val.as.i32;
                    return true;
                case VAL_I64:
                    *out_value = (int32_t)val.as.i64;
                    return true;
                case VAL_U32:
                    *out_value = (int32_t)val.as.u32;
                    return true;
                case VAL_U64:
                    *out_value = (int32_t)val.as.u64;
                    return true;
                case VAL_NUMBER:
                    *out_value = (int32_t)val.as.number;
                    return true;
                default:
                    return false;
            }
        }
        case NODE_UNARY: {
            if (!original->unary.op || strcmp(original->unary.op, "-") != 0) {
                return false;
            }
            TypedASTNode* operand = node->typed.unary.operand;
            if (!operand) {
                return false;
            }
            int32_t inner = 0;
            if (!evaluate_constant_i32(operand, &inner)) {
                return false;
            }
            *out_value = -inner;
            return true;
        }
        default:
            return false;
    }
}

// ===== EXPRESSION COMPILATION =====

int compile_expression(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr) return -1;
    
    DEBUG_CODEGEN_PRINT("Compiling expression type %d\n", expr->original->type);
    
    switch (expr->original->type) {
        case NODE_LITERAL: {
            int reg = compiler_alloc_temp(ctx->allocator);
            if (reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for literal");
                return -1;
            }
            compile_literal(ctx, expr, reg);
            return reg;
        }

        case NODE_ARRAY_LITERAL: {
            int element_count = expr->original->arrayLiteral.count;
            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for array literal result\n");
                return -1;
            }

            if (element_count == 0) {
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, 0);
                emit_byte_to_buffer(ctx->bytecode, 0);
                return result_reg;
            } else {
                int base_reg = compiler_alloc_consecutive_temps(ctx->allocator, element_count);
                if (base_reg == -1) {
                    compiler_free_temp(ctx->allocator, result_reg);
                    DEBUG_CODEGEN_PRINT("Error: Failed to allocate consecutive registers for array literal\n");
                    return -1;
                }

                int* element_regs = malloc(sizeof(int) * element_count);
                if (!element_regs) {
                    for (int i = 0; i < element_count; i++) {
                        compiler_free_temp(ctx->allocator, base_reg + i);
                    }
                    compiler_free_temp(ctx->allocator, result_reg);
                    DEBUG_CODEGEN_PRINT("Error: Failed to allocate element register list for array literal\n");
                    return -1;
                }

                for (int i = 0; i < element_count; i++) {
                    element_regs[i] = base_reg + i;
                }

                bool success = true;
                for (int i = 0; i < element_count; i++) {
                    TypedASTNode* element_node = NULL;
                    if (expr->typed.arrayLiteral.elements && i < expr->typed.arrayLiteral.count) {
                        element_node = expr->typed.arrayLiteral.elements[i];
                    }

                    if (!element_node && expr->original->arrayLiteral.elements &&
                        i < expr->original->arrayLiteral.count) {
                        element_node = create_typed_ast_node(expr->original->arrayLiteral.elements[i]);
                    }

                    if (!element_node) {
                        success = false;
                        break;
                    }

                    int value_reg = compile_expression(ctx, element_node);
                    if (expr->typed.arrayLiteral.elements == NULL && element_node) {
                        free_typed_ast_node(element_node);
                    }

                    if (value_reg == -1) {
                        success = false;
                        break;
                    }

                    if (value_reg != element_regs[i]) {
                        emit_move(ctx, element_regs[i], value_reg);
                        if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
                            compiler_free_temp(ctx->allocator, value_reg);
                        }
                    }
                }

                if (!success) {
                    for (int i = 0; i < element_count; i++) {
                        if (element_regs[i] >= MP_TEMP_REG_START && element_regs[i] <= MP_TEMP_REG_END) {
                            compiler_free_temp(ctx->allocator, element_regs[i]);
                        }
                    }
                    free(element_regs);
                    compiler_free_temp(ctx->allocator, result_reg);
                    DEBUG_CODEGEN_PRINT("Error: Failed to compile array literal element\n");
                    return -1;
                }

                int first_element_reg = element_regs[0];
                if (element_count <= 0) {
                    first_element_reg = result_reg;
                }

                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, first_element_reg);
                emit_byte_to_buffer(ctx->bytecode, element_count);

                for (int i = 0; i < element_count; i++) {
                    if (element_regs[i] >= MP_TEMP_REG_START && element_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, element_regs[i]);
                    }
                }

                free(element_regs);
                return result_reg;
            }
        }
        case NODE_ARRAY_FILL: {
            ASTNode* fillAst = expr->original;
            if (!fillAst->arrayFill.hasResolvedLength) {
                DEBUG_CODEGEN_PRINT("Error: Array fill length unresolved at codegen time\n");
                return -1;
            }

            int length = fillAst->arrayFill.resolvedLength;
            if (length < 0) {
                DEBUG_CODEGEN_PRINT("Error: Negative array fill length\n");
                return -1;
            }

            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for array fill result\n");
                return -1;
            }

            if (length == 0) {
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, 0);
                emit_byte_to_buffer(ctx->bytecode, 0);
                return result_reg;
            }

            int base_reg = compiler_alloc_consecutive_temps(ctx->allocator, length);
            if (base_reg == -1) {
                compiler_free_temp(ctx->allocator, result_reg);
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate registers for array fill elements\n");
                return -1;
            }

            TypedASTNode* value_node = expr->typed.arrayFill.value;
            bool created_value_node = false;
            if (!value_node && fillAst->arrayFill.value) {
                value_node = create_typed_ast_node(fillAst->arrayFill.value);
                created_value_node = (value_node != NULL);
            }

            if (!value_node) {
                for (int i = 0; i < length; i++) {
                    int reg = base_reg + i;
                    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, reg);
                    }
                }
                compiler_free_temp(ctx->allocator, result_reg);
                DEBUG_CODEGEN_PRINT("Error: Missing value expression for array fill\n");
                return -1;
            }

            int value_reg = compile_expression(ctx, value_node);
            if (created_value_node && value_node) {
                free_typed_ast_node(value_node);
            }

            if (value_reg == -1) {
                for (int i = 0; i < length; i++) {
                    int reg = base_reg + i;
                    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, reg);
                    }
                }
                compiler_free_temp(ctx->allocator, result_reg);
                DEBUG_CODEGEN_PRINT("Error: Failed to compile array fill value expression\n");
                return -1;
            }

            bool value_reg_temp = (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END);

            if (value_reg != base_reg) {
                emit_move(ctx, base_reg, value_reg);
                if (value_reg_temp) {
                    compiler_free_temp(ctx->allocator, value_reg);
                    value_reg_temp = false;
                }
            } else {
                value_reg_temp = false;
            }

            for (int i = 1; i < length; i++) {
                emit_move(ctx, base_reg + i, base_reg);
            }

            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
            emit_byte_to_buffer(ctx->bytecode, result_reg);
            emit_byte_to_buffer(ctx->bytecode, base_reg);
            emit_byte_to_buffer(ctx->bytecode, length);

            for (int i = 0; i < length; i++) {
                int reg = base_reg + i;
                if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, reg);
                }
            }

            return result_reg;
        }
        case NODE_ENUM_MATCH_TEST:
            return compile_enum_match_test(ctx, expr);
        case NODE_MATCH_EXPRESSION:
            return compile_match_expression(ctx, expr);
        case NODE_ENUM_PAYLOAD:
            return compile_enum_payload_extract(ctx, expr);
        case NODE_STRUCT_LITERAL: {
            const char* struct_name = expr->typed.structLiteral.structName;
            Type* struct_type = expr->resolvedType;
            if (!struct_type && struct_name) {
                struct_type = findStructType(struct_name);
            }
            Type* base_struct = unwrap_struct_type(struct_type);
            TypeExtension* ext = base_struct ? get_type_extension(base_struct) : NULL;

            int field_count = 0;
            if (ext && ext->extended.structure.fieldCount > 0) {
                field_count = ext->extended.structure.fieldCount;
            } else if (expr->typed.structLiteral.fieldCount > 0) {
                field_count = expr->typed.structLiteral.fieldCount;
            }

            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for struct literal result\n");
                return -1;
            }

            if (field_count <= 0) {
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, 0);
                emit_byte_to_buffer(ctx->bytecode, 0);
                return result_reg;
            }

            int* field_regs = malloc(sizeof(int) * field_count);
            if (!field_regs) {
                compiler_free_temp(ctx->allocator, result_reg);
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register list for struct fields\n");
                return -1;
            }

            bool allocation_failed = false;
            for (int i = 0; i < field_count; i++) {
                field_regs[i] = compiler_alloc_temp(ctx->allocator);
                if (field_regs[i] == -1) {
                    allocation_failed = true;
                    break;
                }
            }

            if (allocation_failed) {
                for (int i = 0; i < field_count; i++) {
                    if (field_regs[i] >= MP_TEMP_REG_START && field_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, field_regs[i]);
                    }
                }
                free(field_regs);
                compiler_free_temp(ctx->allocator, result_reg);
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate struct field registers\n");
                return -1;
            }

            bool success = true;
            for (int i = 0; i < field_count; i++) {
                const char* field_name = NULL;
                if (ext && ext->extended.structure.fields &&
                    i < ext->extended.structure.fieldCount) {
                    FieldInfo* info = &ext->extended.structure.fields[i];
                    if (info && info->name) {
                        field_name = info->name->chars;
                    }
                }
                if (!field_name && expr->typed.structLiteral.fields &&
                    i < expr->typed.structLiteral.fieldCount) {
                    field_name = expr->typed.structLiteral.fields[i].name;
                }

                TypedASTNode* value_node = NULL;
                if (field_name) {
                    value_node = find_struct_literal_value(expr, field_name);
                }
                if (!value_node && expr->typed.structLiteral.values &&
                    i < expr->typed.structLiteral.fieldCount) {
                    value_node = expr->typed.structLiteral.values[i];
                }

                if (!value_node) {
                    DEBUG_CODEGEN_PRINT("Error: Missing value for struct field %d\n", i);
                    success = false;
                    break;
                }

                int value_reg = compile_expression(ctx, value_node);
                if (value_reg == -1) {
                    success = false;
                    break;
                }

                if (value_reg != field_regs[i]) {
                    emit_move(ctx, field_regs[i], value_reg);
                    if (value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, value_reg);
                    }
                }
            }

            if (!success) {
                for (int i = 0; i < field_count; i++) {
                    if (field_regs[i] >= MP_TEMP_REG_START && field_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, field_regs[i]);
                    }
                }
                free(field_regs);
                compiler_free_temp(ctx->allocator, result_reg);
                return -1;
            }

            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode, OP_MAKE_ARRAY_R);
            emit_byte_to_buffer(ctx->bytecode, result_reg);
            emit_byte_to_buffer(ctx->bytecode, field_regs[0]);
            emit_byte_to_buffer(ctx->bytecode, field_count);

            for (int i = 0; i < field_count; i++) {
                if (field_regs[i] >= MP_TEMP_REG_START && field_regs[i] <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, field_regs[i]);
                }
            }

            free(field_regs);
            return result_reg;
        }
        case NODE_INDEX_ACCESS: {
            TypedASTNode* array_node = expr->typed.indexAccess.array;
            TypedASTNode* index_node = expr->typed.indexAccess.index;
            int array_reg = compile_expression(ctx, array_node);
            if (array_reg == -1) {
                return -1;
            }

            int index_reg = compile_expression(ctx, index_node);
            if (index_reg == -1) {
                if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, array_reg);
                }
                return -1;
            }

            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for array access\n");
                if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, index_reg);
                }
                if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, array_reg);
                }
                return -1;
            }

            Type* container_type = array_node->resolvedType;
            if (!container_type && array_node->original &&
                array_node->original->dataType) {
                container_type = array_node->original->dataType;
            }

            Type* resolved_container_type = container_type;
            if (!resolved_container_type && array_node->original &&
                array_node->original->dataType) {
                resolved_container_type = array_node->original->dataType;
            }

            if (!resolved_container_type && array_node->original &&
                array_node->original->type == NODE_IDENTIFIER) {
                const char* ident_name = array_node->original->identifier.name;
                Symbol* symbol = resolve_symbol(ctx->symbols, ident_name);
                if (symbol && symbol->type) {
                    resolved_container_type = symbol->type;
                }
            }

            bool is_string_index = expr->typed.indexAccess.isStringIndex;
            if (!is_string_index) {
                const Type* base_type = resolved_container_type;
                if (base_type && base_type->kind == TYPE_INSTANCE &&
                    base_type->info.instance.base) {
                    base_type = base_type->info.instance.base;
                }

                if (base_type && base_type->kind == TYPE_STRING) {
                    is_string_index = true;
                } else if (!base_type && array_node->original &&
                           array_node->original->type == NODE_LITERAL &&
                           array_node->original->literal.value.type == VAL_STRING) {
                    is_string_index = true;
                }
            }

            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode,
                                is_string_index ? OP_STRING_INDEX_R : OP_ARRAY_GET_R);
            emit_byte_to_buffer(ctx->bytecode, result_reg);
            emit_byte_to_buffer(ctx->bytecode, array_reg);
            emit_byte_to_buffer(ctx->bytecode, index_reg);

            if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, index_reg);
            }
            if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, array_reg);
            }

            return result_reg;
        }

        case NODE_BINARY: {
            DEBUG_CODEGEN_PRINT("NODE_BINARY: About to check binary expression");
            DEBUG_CODEGEN_PRINT("NODE_BINARY: expr=%p\n", (void*)expr);
            DEBUG_CODEGEN_PRINT("NODE_BINARY: expr->original=%p\n", (void*)expr->original);
            if (expr->original) {
                DEBUG_CODEGEN_PRINT("NODE_BINARY: expr->original->type=%d\n", expr->original->type);
                DEBUG_CODEGEN_PRINT("NODE_BINARY: expr->original->binary.left=%p, expr->original->binary.right=%p\n", 
                       (void*)expr->original->binary.left, (void*)expr->original->binary.right);
            }
            DEBUG_CODEGEN_PRINT("NODE_BINARY: left=%p, right=%p\n", (void*)expr->typed.binary.left, (void*)expr->typed.binary.right);
            
            // Check if typed AST nodes are missing - create them if needed
            TypedASTNode* left_typed = expr->typed.binary.left;
            TypedASTNode* right_typed = expr->typed.binary.right;
            
            if (!left_typed && expr->original->binary.left) {
                left_typed = create_typed_ast_node(expr->original->binary.left);
                if (left_typed) {
                    // Infer type from the original AST node - use dataType if available, otherwise infer from content
                    if (expr->original->binary.left->dataType) {
                        left_typed->resolvedType = expr->original->binary.left->dataType;
                    } else if (expr->original->binary.left->type == NODE_LITERAL) {
                        // For literals, infer type from the value
                        Value val = expr->original->binary.left->literal.value;
                        left_typed->resolvedType = malloc(sizeof(Type));
                        if (left_typed->resolvedType) {
                            memset(left_typed->resolvedType, 0, sizeof(Type));
                            left_typed->resolvedType->kind = (val.type == VAL_I32) ? TYPE_I32 :
                                                             (val.type == VAL_I64) ? TYPE_I64 :
                                                             (val.type == VAL_F64) ? TYPE_F64 :
                                                             (val.type == VAL_BOOL) ? TYPE_BOOL : TYPE_I32;
                        }
                    } else if (expr->original->binary.left->type == NODE_IDENTIFIER) {
                        // For identifiers, look up type from symbol table
                        const char* var_name = expr->original->binary.left->identifier.name;
                        int var_reg = lookup_variable(ctx, var_name);
                        if (var_reg != -1) {
                            // Look up symbol to get type information
                            Symbol* symbol = resolve_symbol(ctx->symbols, var_name);
                            if (symbol && symbol->type) {
                                left_typed->resolvedType = symbol->type;
                            } else {
                                // Default to i32 if no type info available
                                left_typed->resolvedType = malloc(sizeof(Type));
                                if (left_typed->resolvedType) {
                                    memset(left_typed->resolvedType, 0, sizeof(Type));
                                    left_typed->resolvedType->kind = TYPE_I32;
                                }
                            }
                        } else {
                            // Variable not found, default to i32
                            left_typed->resolvedType = malloc(sizeof(Type));
                            if (left_typed->resolvedType) {
                                memset(left_typed->resolvedType, 0, sizeof(Type));
                                left_typed->resolvedType->kind = TYPE_I32;
                            }
                        }
                    } else {
                        // Default to i32 for other node types without explicit type info
                        left_typed->resolvedType = malloc(sizeof(Type));
                        if (left_typed->resolvedType) {
                            memset(left_typed->resolvedType, 0, sizeof(Type));
                            left_typed->resolvedType->kind = TYPE_I32;
                        }
                    }
                }
            }
            
            if (!right_typed && expr->original->binary.right) {
                right_typed = create_typed_ast_node(expr->original->binary.right);
                if (right_typed) {
                    // Infer type from the original AST node - use dataType if available, otherwise infer from content
                    if (expr->original->binary.right->dataType) {
                        right_typed->resolvedType = expr->original->binary.right->dataType;
                    } else if (expr->original->binary.right->type == NODE_LITERAL) {
                        // For literals, infer type from the value
                        Value val = expr->original->binary.right->literal.value;
                        right_typed->resolvedType = malloc(sizeof(Type));
                        if (right_typed->resolvedType) {
                            memset(right_typed->resolvedType, 0, sizeof(Type));
                            right_typed->resolvedType->kind = (val.type == VAL_I32) ? TYPE_I32 :
                                                             (val.type == VAL_I64) ? TYPE_I64 :
                                                             (val.type == VAL_F64) ? TYPE_F64 :
                                                             (val.type == VAL_BOOL) ? TYPE_BOOL : TYPE_I32;
                        }
                    } else if (expr->original->binary.right->type == NODE_IDENTIFIER) {
                        // For identifiers, look up type from symbol table
                        const char* var_name = expr->original->binary.right->identifier.name;
                        int var_reg = lookup_variable(ctx, var_name);
                        if (var_reg != -1) {
                            // Look up symbol to get type information
                            Symbol* symbol = resolve_symbol(ctx->symbols, var_name);
                            if (symbol && symbol->type) {
                                right_typed->resolvedType = symbol->type;
                            } else {
                                // Default to i32 if no type info available
                                right_typed->resolvedType = malloc(sizeof(Type));
                                if (right_typed->resolvedType) {
                                    memset(right_typed->resolvedType, 0, sizeof(Type));
                                    right_typed->resolvedType->kind = TYPE_I32;
                                }
                            }
                        } else {
                            // Variable not found, default to i32
                            right_typed->resolvedType = malloc(sizeof(Type));
                            if (right_typed->resolvedType) {
                                memset(right_typed->resolvedType, 0, sizeof(Type));
                                right_typed->resolvedType->kind = TYPE_I32;
                            }
                        }
                    } else {
                        // Default to i32 for other node types without explicit type info
                        right_typed->resolvedType = malloc(sizeof(Type));
                        if (right_typed->resolvedType) {
                            memset(right_typed->resolvedType, 0, sizeof(Type));
                            right_typed->resolvedType->kind = TYPE_I32;
                        }
                    }
                }
            }
            
            if (!left_typed || !right_typed) {
                DEBUG_CODEGEN_PRINT("Error: Failed to create typed AST nodes for binary operands");
                return -1;
            }
            
            // Ensure the binary expression itself has type information for compile_binary_op
            if (!expr->resolvedType && left_typed->resolvedType && right_typed->resolvedType) {
                expr->resolvedType = malloc(sizeof(Type));
                if (expr->resolvedType) {
                    memset(expr->resolvedType, 0, sizeof(Type));
                }
                // For arithmetic operations, use the "larger" type; for comparison operations, use bool
                const char* op = expr->original->binary.op;
                bool is_comparison = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || 
                                     strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                                     strcmp(op, "==") == 0 || strcmp(op, "!=") == 0);
                
                if (is_comparison) {
                    expr->resolvedType->kind = TYPE_BOOL;
                } else {
                    // Use the promoted type for arithmetic operations
                    TypeKind left_kind = left_typed->resolvedType->kind;
                    TypeKind right_kind = right_typed->resolvedType->kind;
                    
                    if (left_kind == right_kind) {
                        expr->resolvedType->kind = left_kind;
                    } else if ((left_kind == TYPE_I32 && right_kind == TYPE_I64) || 
                               (left_kind == TYPE_I64 && right_kind == TYPE_I32)) {
                        expr->resolvedType->kind = TYPE_I64;
                    } else if (left_kind == TYPE_F64 || right_kind == TYPE_F64) {
                        expr->resolvedType->kind = TYPE_F64;
                    } else {
                        expr->resolvedType->kind = TYPE_I32; // Default
                    }
                }
            }
            
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Compiling left operand (type %d)\n", left_typed->original->type);
            int left_reg = compile_expression(ctx, left_typed);
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Left operand returned register %d\n", left_reg);
            
            // CRITICAL FIX: If left operand is a function call (temp register) and right operand is also a function call,
            // move left result to a frame register to protect it from being corrupted during right operand evaluation
            bool left_is_temp = (left_reg >= MP_TEMP_REG_START && left_reg <= MP_TEMP_REG_END);
            bool right_is_function_call = (right_typed->original->type == NODE_CALL);
            int protected_left_reg = left_reg;
            
            if (left_is_temp && right_is_function_call) {
                // Use a dedicated parameter register (R240) to preserve left operand
                int frame_protection_reg = 240;  // R240 is preserved across function calls
                emit_move(ctx, frame_protection_reg, left_reg);
                DEBUG_CODEGEN_PRINT("NODE_BINARY: Protected left operand R%d -> R%d (param register)\n", left_reg, frame_protection_reg);

                // Free the original temp register
                compiler_free_temp(ctx->allocator, left_reg);
                protected_left_reg = frame_protection_reg;
            }
            
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Compiling right operand (type %d)\n", right_typed->original->type);
            int right_reg = compile_expression(ctx, right_typed);
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Right operand returned register %d\n", right_reg);
            
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Allocating result register");
            int result_reg = compiler_alloc_temp(ctx->allocator);
            DEBUG_CODEGEN_PRINT("NODE_BINARY: Result register is %d\n", result_reg);
            
            if (protected_left_reg == -1 || right_reg == -1 || result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate registers for binary operation (left=%d, right=%d, result=%d)\n", protected_left_reg, right_reg, result_reg);
                return -1;
            }
            
            // Call the fixed compile_binary_op with all required parameters
            compile_binary_op(ctx, expr, result_reg, protected_left_reg, right_reg);
            
            // Free operand registers if they are temporary values. Frame registers
            // represent named variables and must remain allocated after the
            // operation; freeing them corrupts variable state in subsequent
            // expressions (e.g. comparisons). Only temporary registers should be
            // released here.
            if (protected_left_reg >= MP_TEMP_REG_START && protected_left_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, protected_left_reg);
            }
            if (right_reg >= MP_TEMP_REG_START && right_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, right_reg);
            }
            
            // Clean up temporary typed nodes if we created them
            if (left_typed != expr->typed.binary.left) {
                free_typed_ast_node(left_typed);
            }
            if (right_typed != expr->typed.binary.right) {
                free_typed_ast_node(right_typed);
            }
            
            return result_reg;
        }

        case NODE_ASSIGN: {
            int reg = compile_assignment_internal(ctx, expr, true);
            return reg;
        }
        case NODE_ARRAY_ASSIGN: {
            return compile_array_assignment(ctx, expr, true);
        }
        case NODE_MEMBER_ASSIGN: {
            return compile_member_assignment(ctx, expr, true);
        }

        case NODE_ARRAY_SLICE: {
            TypedASTNode* array_node = expr->typed.arraySlice.array;
            TypedASTNode* start_node = expr->typed.arraySlice.start;
            TypedASTNode* end_node = expr->typed.arraySlice.end;

            bool free_array_node = false;
            bool free_start_node = false;
            bool free_end_node = false;

            if (!array_node && expr->original && expr->original->arraySlice.array) {
                array_node = create_typed_ast_node(expr->original->arraySlice.array);
                free_array_node = (array_node != NULL);
            }
            if (!start_node && expr->original && expr->original->arraySlice.start) {
                start_node = create_typed_ast_node(expr->original->arraySlice.start);
                free_start_node = (start_node != NULL);
            }
            if (!end_node && expr->original && expr->original->arraySlice.end) {
                end_node = create_typed_ast_node(expr->original->arraySlice.end);
                free_end_node = (end_node != NULL);
            }

            bool start_required = (expr->original && expr->original->arraySlice.start != NULL);
            bool end_required = (expr->original && expr->original->arraySlice.end != NULL);

            if (!array_node || (start_required && !start_node) ||
                (end_required && !end_node)) {
                if (free_array_node) free_typed_ast_node(array_node);
                if (free_start_node) free_typed_ast_node(start_node);
                if (free_end_node) free_typed_ast_node(end_node);
                return -1;
            }

            int array_reg = compile_expression(ctx, array_node);
            if (array_reg == -1) {
                if (free_array_node) free_typed_ast_node(array_node);
                if (free_start_node) free_typed_ast_node(start_node);
                if (free_end_node) free_typed_ast_node(end_node);
                return -1;
            }

            int start_reg = -1;
            if (start_node) {
                start_reg = compile_expression(ctx, start_node);
                if (start_reg == -1) {
                    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, array_reg);
                    }
                    if (free_array_node) free_typed_ast_node(array_node);
                    if (free_start_node) free_typed_ast_node(start_node);
                    if (free_end_node) free_typed_ast_node(end_node);
                    return -1;
                }
            } else {
                start_reg = compiler_alloc_temp(ctx->allocator);
                if (start_reg == -1) {
                    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, array_reg);
                    }
                    if (free_array_node) free_typed_ast_node(array_node);
                    if (free_start_node) free_typed_ast_node(start_node);
                    if (free_end_node) free_typed_ast_node(end_node);
                    return -1;
                }
                set_location_from_node(ctx, expr);
                emit_load_constant(ctx, start_reg, I32_VAL(0));
            }

            int end_reg = -1;
            if (end_node) {
                end_reg = compile_expression(ctx, end_node);
                if (end_reg == -1) {
                    if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, start_reg);
                    }
                    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, array_reg);
                    }
                    if (free_array_node) free_typed_ast_node(array_node);
                    if (free_start_node) free_typed_ast_node(start_node);
                    if (free_end_node) free_typed_ast_node(end_node);
                    return -1;
                }
            } else {
                end_reg = compiler_alloc_temp(ctx->allocator);
                if (end_reg == -1) {
                    if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, start_reg);
                    }
                    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, array_reg);
                    }
                    if (free_array_node) free_typed_ast_node(array_node);
                    if (free_start_node) free_typed_ast_node(start_node);
                    if (free_end_node) free_typed_ast_node(end_node);
                    return -1;
                }
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_LEN_R);
                emit_byte_to_buffer(ctx->bytecode, end_reg);
                emit_byte_to_buffer(ctx->bytecode, array_reg);
            }

            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate result register for array slice\n");
                if (end_reg >= MP_TEMP_REG_START && end_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, end_reg);
                }
                if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, start_reg);
                }
                if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, array_reg);
                }
                if (free_array_node) free_typed_ast_node(array_node);
                if (free_start_node) free_typed_ast_node(start_node);
                if (free_end_node) free_typed_ast_node(end_node);
                return -1;
            }

            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_SLICE_R);
            emit_byte_to_buffer(ctx->bytecode, result_reg);
            emit_byte_to_buffer(ctx->bytecode, array_reg);
            emit_byte_to_buffer(ctx->bytecode, start_reg);
            emit_byte_to_buffer(ctx->bytecode, end_reg);

            if (end_reg >= MP_TEMP_REG_START && end_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, end_reg);
            }
            if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, start_reg);
            }
            if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, array_reg);
            }

            if (free_array_node) free_typed_ast_node(array_node);
            if (free_start_node) free_typed_ast_node(start_node);
            if (free_end_node) free_typed_ast_node(end_node);

            return result_reg;
        }

        case NODE_IDENTIFIER: {
            const char* name = expr->original->identifier.name;
            SrcLocation location = expr->original->location;
            Symbol* symbol = resolve_symbol(ctx->symbols, name);
            if (!symbol) {
                report_undefined_variable(location, name);
                ctx->has_compilation_errors = true;
                return -1;
            }

            bool is_upvalue = false;
            int upvalue_index = -1;
            int reg = resolve_variable_or_upvalue(ctx, name, &is_upvalue, &upvalue_index);
            if (reg == -1) {
                report_scope_violation(location, name,
                                       get_variable_scope_info(name, ctx->symbols->scope_depth));
                ctx->has_compilation_errors = true;
                return -1;
            }

            if (!symbol->is_initialized) {
                report_variable_not_initialized(location, name);
                ctx->has_compilation_errors = true;
            }

            symbol->has_been_read = true;

            if (is_upvalue) {
                int temp = compiler_alloc_temp(ctx->allocator);
                if (temp == -1) {
                    DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for upvalue access");
                    return -1;
                }
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_GET_UPVALUE_R);
                emit_byte_to_buffer(ctx->bytecode, temp);
                emit_byte_to_buffer(ctx->bytecode, upvalue_index);
                return temp;
            }

            return reg;
        }
        
        case NODE_CAST: {
            DEBUG_CODEGEN_PRINT("NODE_CAST: Compiling cast expression");
            
            // Compile the expression being cast
            int source_reg = compile_expression(ctx, expr->typed.cast.expression);
            if (source_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to compile cast source expression");
                return -1;
            }
            
            // Get source type from the expression being cast
            Type* source_type = expr->typed.cast.expression->resolvedType;
            Type* target_type = expr->resolvedType; // Target type from cast
            
            if (!source_type || !target_type) {
                DEBUG_CODEGEN_PRINT("Error: Missing type information for cast (source=%p, target=%p)\n", 
                       (void*)source_type, (void*)target_type);
                // Only free if it's a temp register
                if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, source_reg);
                }
                return -1;
            }
            
            DEBUG_CODEGEN_PRINT("NODE_CAST: Casting from type %d to type %d\n", source_type->kind, target_type->kind);
            
            // If source and target types are the same, no cast needed
            if (source_type->kind == target_type->kind) {
                DEBUG_CODEGEN_PRINT("NODE_CAST: Same types, no cast needed");
                return source_reg;
            }
            
            // Always allocate a new register for cast result to avoid register conflicts
            int target_reg = compiler_alloc_temp(ctx->allocator);
            if (target_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for cast result");
                // Only free if it's a temp register
                if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, source_reg);
                }
                return -1;
            }
            
            // Emit cast instruction based on source and target type combination
            uint8_t cast_opcode = 0;
            
            // Map type combinations to cast opcodes
            if (source_type->kind == TYPE_I32 && target_type->kind == TYPE_I64) {
                cast_opcode = OP_I32_TO_I64_R;
            } else if (source_type->kind == TYPE_I32 && target_type->kind == TYPE_F64) {
                cast_opcode = OP_I32_TO_F64_R;
            } else if (source_type->kind == TYPE_I32 && target_type->kind == TYPE_U32) {
                cast_opcode = OP_I32_TO_U32_R;
            } else if (source_type->kind == TYPE_I32 && target_type->kind == TYPE_U64) {
                cast_opcode = OP_I32_TO_U64_R;
            } else if (source_type->kind == TYPE_I32 && target_type->kind == TYPE_BOOL) {
                cast_opcode = OP_I32_TO_BOOL_R;
            } else if (source_type->kind == TYPE_BOOL && target_type->kind == TYPE_I32) {
                cast_opcode = OP_BOOL_TO_I32_R;
            } else if (source_type->kind == TYPE_BOOL && target_type->kind == TYPE_I64) {
                cast_opcode = OP_BOOL_TO_I64_R;
            } else if (source_type->kind == TYPE_BOOL && target_type->kind == TYPE_U32) {
                cast_opcode = OP_BOOL_TO_U32_R;
            } else if (source_type->kind == TYPE_BOOL && target_type->kind == TYPE_U64) {
                cast_opcode = OP_BOOL_TO_U64_R;
            } else if (source_type->kind == TYPE_BOOL && target_type->kind == TYPE_F64) {
                cast_opcode = OP_BOOL_TO_F64_R;
            } else if (source_type->kind == TYPE_I64 && target_type->kind == TYPE_I32) {
                cast_opcode = OP_I64_TO_I32_R;
            } else if (source_type->kind == TYPE_I64 && target_type->kind == TYPE_F64) {
                cast_opcode = OP_I64_TO_F64_R;
            } else if (source_type->kind == TYPE_I64 && target_type->kind == TYPE_U64) {
                cast_opcode = OP_I64_TO_U64_R;
            } else if (source_type->kind == TYPE_I64 && target_type->kind == TYPE_U32) {
                cast_opcode = OP_I64_TO_U32_R;
            } else if (source_type->kind == TYPE_I64 && target_type->kind == TYPE_BOOL) {
                cast_opcode = OP_I64_TO_BOOL_R;
            } else if (source_type->kind == TYPE_F64 && target_type->kind == TYPE_I32) {
                cast_opcode = OP_F64_TO_I32_R;
            } else if (source_type->kind == TYPE_F64 && target_type->kind == TYPE_I64) {
                cast_opcode = OP_F64_TO_I64_R;
            } else if (source_type->kind == TYPE_F64 && target_type->kind == TYPE_U32) {
                cast_opcode = OP_F64_TO_U32_R;
            } else if (source_type->kind == TYPE_F64 && target_type->kind == TYPE_U64) {
                cast_opcode = OP_F64_TO_U64_R;
            } else if (source_type->kind == TYPE_F64 && target_type->kind == TYPE_BOOL) {
                cast_opcode = OP_F64_TO_BOOL_R;
            } else if (source_type->kind == TYPE_U32 && target_type->kind == TYPE_I32) {
                cast_opcode = OP_U32_TO_I32_R;
            } else if (source_type->kind == TYPE_U32 && target_type->kind == TYPE_F64) {
                cast_opcode = OP_U32_TO_F64_R;
            } else if (source_type->kind == TYPE_U32 && target_type->kind == TYPE_U64) {
                cast_opcode = OP_U32_TO_U64_R;
            } else if (source_type->kind == TYPE_U32 && target_type->kind == TYPE_I64) {
                // Use u32->u64 opcode but emit as i64 value (semantically equivalent)
                cast_opcode = OP_U32_TO_U64_R;
            } else if (source_type->kind == TYPE_U32 && target_type->kind == TYPE_BOOL) {
                cast_opcode = OP_U32_TO_BOOL_R;
            } else if (source_type->kind == TYPE_U64 && target_type->kind == TYPE_I32) {
                cast_opcode = OP_U64_TO_I32_R;
            } else if (source_type->kind == TYPE_U64 && target_type->kind == TYPE_I64) {
                cast_opcode = OP_U64_TO_I64_R;
            } else if (source_type->kind == TYPE_U64 && target_type->kind == TYPE_F64) {
                cast_opcode = OP_U64_TO_F64_R;
            } else if (source_type->kind == TYPE_U64 && target_type->kind == TYPE_U32) {
                cast_opcode = OP_U64_TO_U32_R;
            } else if (source_type->kind == TYPE_U64 && target_type->kind == TYPE_BOOL) {
                cast_opcode = OP_U64_TO_BOOL_R;
            } else if (target_type->kind == TYPE_STRING) {
                if (type_is_numeric(source_type) || source_type->kind == TYPE_BOOL) {
                    cast_opcode = OP_TO_STRING_R;
                } else {
                    DEBUG_CODEGEN_PRINT("Error: Unsupported cast from type %d to string\n",
                           source_type->kind);
                    if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, source_reg);
                    }
                    if (target_reg >= MP_TEMP_REG_START && target_reg <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, target_reg);
                    }
                    return -1;
                }
            } else {
                DEBUG_CODEGEN_PRINT("Error: Unsupported cast from type %d to type %d\n",
                       source_type->kind, target_type->kind);
                // Only free if they're temp registers
                if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, source_reg);
                }
                if (target_reg >= MP_TEMP_REG_START && target_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, target_reg);
                }
                return -1;
            }

            // Emit the cast instruction
            set_location_from_node(ctx, expr);
            emit_instruction_to_buffer(ctx->bytecode, cast_opcode, target_reg, source_reg, 0);
            DEBUG_CODEGEN_PRINT("NODE_CAST: Emitted cast opcode %d from R%d to R%d\n",
                   cast_opcode, source_reg, target_reg);
            
            // Free source register only if it's a temp register
            if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, source_reg);
            }
            
            return target_reg;
        }
        
        case NODE_TIME_STAMP: {
            // Generate timestamp() call - returns f64
            int reg = compiler_alloc_temp(ctx->allocator);
            if (reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for timestamp");
                return -1;
            }

            // Emit OP_TIME_STAMP instruction (variable-length format: opcode + register)
            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode, OP_TIME_STAMP);
            emit_byte_to_buffer(ctx->bytecode, reg);
            DEBUG_CODEGEN_PRINT("Emitted OP_TIME_STAMP R%d (returns f64)\n", reg);
            
            return reg;
        }
        
        case NODE_UNARY: {
            DEBUG_CODEGEN_PRINT("NODE_UNARY: Compiling unary expression");
            DEBUG_CODEGEN_PRINT("NODE_UNARY: expr=%p\n", (void*)expr);
            DEBUG_CODEGEN_PRINT("NODE_UNARY: expr->original=%p\n", (void*)expr->original);
            DEBUG_CODEGEN_PRINT("NODE_UNARY: expr->original->unary.operand=%p\n", (void*)(expr->original ? expr->original->unary.operand : NULL));
            
            if (!expr->original || !expr->original->unary.operand) {
                DEBUG_CODEGEN_PRINT("Error: Unary operand is NULL in original AST");
                return -1;
            }
            
            // Create a typed AST node for the operand  
            TypedASTNode* operand_typed = create_typed_ast_node(expr->original->unary.operand);
            if (!operand_typed) {
                DEBUG_CODEGEN_PRINT("Error: Failed to create typed AST for unary operand\n");  
                return -1;
            }
            
            // Copy the resolved type if available
            operand_typed->resolvedType = expr->original->unary.operand->dataType;
            
            // Compile the operand
            int operand_reg = compile_expression(ctx, operand_typed);
            if (operand_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to compile unary operand");
                free_typed_ast_node(operand_typed);
                return -1;
            }
            
            // Clean up
            free_typed_ast_node(operand_typed);
            
            // Allocate result register
            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for unary result");
                return -1;
            }
            
            // Handle different unary operators
            const char* op = expr->original->unary.op;
            if (strcmp(op, "not") == 0) {
                // Logical NOT operation - only works on boolean values
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_NOT_BOOL_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, operand_reg);
                DEBUG_CODEGEN_PRINT("Emitted OP_NOT_BOOL_R R%d, R%d (logical NOT)\n", result_reg, operand_reg);
            } else if (strcmp(op, "-") == 0) {
                // Unary minus operation - works on numeric types (i32, i64, u32, u64, f64)
                set_location_from_node(ctx, expr);
                emit_byte_to_buffer(ctx->bytecode, OP_NEG_I32_R);
                emit_byte_to_buffer(ctx->bytecode, result_reg);
                emit_byte_to_buffer(ctx->bytecode, operand_reg);
                DEBUG_CODEGEN_PRINT("Emitted OP_NEG_I32_R R%d, R%d (unary minus)\n", result_reg, operand_reg);
            } else {
                DEBUG_CODEGEN_PRINT("Error: Unsupported unary operator: %s\n", op);
                return -1;
            }
            
            // Free operand register if it's a temporary
            if (operand_reg >= MP_TEMP_REG_START && operand_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, operand_reg);
            }
            
            return result_reg;
        }
        
        case NODE_FUNCTION: {
            return compile_function_declaration(ctx, expr);
        }

        case NODE_MEMBER_ACCESS: {
            if (expr->typed.member.resolvesToModule) {
                ModuleExportKind kind = expr->typed.member.moduleExportKind;
                if (kind == MODULE_EXPORT_KIND_STRUCT || kind == MODULE_EXPORT_KIND_ENUM) {
                    // Type-level access; no runtime value to materialize
                    return -1;
                }

                const char* alias_name = expr->original->member.moduleAliasBinding;
                char generated_alias[128];
                if (!alias_name || alias_name[0] == '\0') {
                    const char* prefix = NULL;
                    if (expr->original->member.object && expr->original->member.object->type == NODE_IDENTIFIER) {
                        prefix = expr->original->member.object->identifier.name;
                    }
                    if (!prefix || prefix[0] == '\0') {
                        prefix = "module";
                    }
                    const char* member_name = expr->typed.member.member ? expr->typed.member.member : "value";
                    snprintf(generated_alias, sizeof(generated_alias), "__module_%s_%s", prefix, member_name);
                    alias_name = generated_alias;
                }

                int existing_reg = lookup_variable(ctx, alias_name);
                if (existing_reg == -1) {
                    ModuleManager* manager = vm.register_file.module_manager;
                    if (!manager) {
                        report_compile_error(E3004_IMPORT_FAILED, expr->original->location,
                                             "module manager is not initialized");
                        ctx->has_compilation_errors = true;
                        return -1;
                    }

                    const char* module_name = expr->original->member.moduleName;
                    if (!module_name) {
                        report_compile_error(E3004_IMPORT_FAILED, expr->original->location,
                                             "missing module name for namespace access");
                        ctx->has_compilation_errors = true;
                        return -1;
                    }

                    if (!import_symbol_by_name(ctx, manager, module_name,
                                              expr->typed.member.member, alias_name,
                                              expr->original->location)) {
                        return -1;
                    }

                    existing_reg = lookup_variable(ctx, alias_name);
                }

                return existing_reg;
            }

            if (expr->typed.member.resolvesToEnumVariant) {
                return compile_enum_variant_access(ctx, expr);
            }

            if (!expr->typed.member.object) {
                return -1;
            }

            if (expr->typed.member.isMethod) {
                DEBUG_CODEGEN_PRINT("Error: Method access is not yet supported in codegen\n");
                ctx->has_compilation_errors = true;
                return -1;
            }

            int field_index = resolve_struct_field_index(expr->typed.member.object->resolvedType,
                                                         expr->typed.member.member);
            if (field_index < 0) {
                if (ctx->errors) {
                    error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                                       expr->original->location,
                                       "Unknown struct field",
                                       expr->typed.member.member ? expr->typed.member.member : "<unknown>",
                                       NULL);
                }
                ctx->has_compilation_errors = true;
                return -1;
            }

            int object_reg = compile_expression(ctx, expr->typed.member.object);
            if (object_reg == -1) {
                return -1;
            }

            int index_reg = compiler_alloc_temp(ctx->allocator);
            if (index_reg == -1) {
                if (object_reg >= MP_TEMP_REG_START && object_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, object_reg);
                }
                return -1;
            }

            emit_load_constant(ctx, index_reg, I32_VAL(field_index));

            int result_reg = compiler_alloc_temp(ctx->allocator);
            if (result_reg == -1) {
                if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, index_reg);
                }
                if (object_reg >= MP_TEMP_REG_START && object_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, object_reg);
                }
                return -1;
            }

            set_location_from_node(ctx, expr);
            emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_GET_R);
            emit_byte_to_buffer(ctx->bytecode, result_reg);
            emit_byte_to_buffer(ctx->bytecode, object_reg);
            emit_byte_to_buffer(ctx->bytecode, index_reg);

            if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, index_reg);
            }
            if (object_reg >= MP_TEMP_REG_START && object_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, object_reg);
            }

            return result_reg;
        }

        case NODE_CALL: {
            DEBUG_CODEGEN_PRINT("NODE_CALL: Compiling function call");

            if (expr->typed.call.callee && expr->typed.call.callee->original &&
                expr->typed.call.callee->original->type == NODE_MEMBER_ACCESS &&
                expr->typed.call.callee->typed.member.isMethod) {
                int result = compile_struct_method_call(ctx, expr);
                if (result == -1) {
                    DEBUG_CODEGEN_PRINT("Error: Failed to compile struct method call");
                }
                return result;
            }

            if (expr->typed.call.callee && expr->typed.call.callee->original &&
                expr->typed.call.callee->original->type == NODE_MEMBER_ACCESS &&
                expr->typed.call.callee->typed.member.resolvesToEnumVariant) {
                return compile_enum_constructor_call(ctx, expr);
            }

            const char* builtin_name = NULL;
            if (expr->typed.call.callee && expr->typed.call.callee->original &&
                expr->typed.call.callee->original->type == NODE_IDENTIFIER) {
                builtin_name = expr->typed.call.callee->original->identifier.name;
            } else if (expr->original->call.callee &&
                       expr->original->call.callee->type == NODE_IDENTIFIER) {
                builtin_name = expr->original->call.callee->identifier.name;
            }

            if (builtin_name) {
                if (strcmp(builtin_name, "push") == 0) {
                    return compile_builtin_array_push(ctx, expr);
                } else if (strcmp(builtin_name, "pop") == 0) {
                    return compile_builtin_array_pop(ctx, expr);
                } else if (strcmp(builtin_name, "len") == 0) {
                    return compile_builtin_array_len(ctx, expr);
                } else if (strcmp(builtin_name, "sorted") == 0) {
                    return compile_builtin_sorted(ctx, expr);
                } else if (strcmp(builtin_name, "range") == 0) {
                    return compile_builtin_range(ctx, expr);
                } else if (strcmp(builtin_name, "input") == 0) {
                    return compile_builtin_input(ctx, expr);
                } else if (strcmp(builtin_name, "int") == 0) {
                    return compile_builtin_int(ctx, expr);
                } else if (strcmp(builtin_name, "float") == 0) {
                    return compile_builtin_float(ctx, expr);
                } else if (strcmp(builtin_name, "typeof") == 0) {
                    return compile_builtin_typeof(ctx, expr);
                } else if (strcmp(builtin_name, "istype") == 0) {
                    return compile_builtin_istype(ctx, expr);
                } else if (strcmp(builtin_name, "assert_eq") == 0) {
                    return compile_builtin_assert_eq(ctx, expr);
                }
            }

            int arg_count = expr->original->call.argCount;

            // Compile callee expression (can be function or closure)
            int callee_reg = compile_expression(ctx, expr->typed.call.callee);
            if (callee_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to compile call callee");
                return -1;
            }

            // Allocate consecutive temp registers for arguments
            int first_arg_reg = -1;
            int* arg_regs = NULL;
            
            if (arg_count > 0) {
                arg_regs = malloc(sizeof(int) * arg_count);
                if (!arg_regs) {
                    DEBUG_CODEGEN_PRINT("Error: Failed to allocate memory for argument registers");
                    return -1;
                }

                int consecutive_base = compiler_alloc_consecutive_temps(ctx->allocator, arg_count);
                if (consecutive_base != -1) {
                    first_arg_reg = consecutive_base;
                    for (int i = 0; i < arg_count; i++) {
                        arg_regs[i] = consecutive_base + i;
                    }
                } else {
                    // Fallback: allocate individually (older behavior)
                    for (int i = 0; i < arg_count; i++) {
                        int reg = compiler_alloc_temp(ctx->allocator);
                        if (reg == -1) {
                            DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for argument %d", i);
                            free(arg_regs);
                            return -1;
                        }
                        arg_regs[i] = reg;
                        if (i == 0) first_arg_reg = reg;
                    }
                }
            }
            
            // FIXED: First evaluate ALL arguments into temporary storage to avoid register corruption
            int* temp_arg_regs = NULL;
            if (arg_count > 0) {
                temp_arg_regs = malloc(sizeof(int) * arg_count);
                if (!temp_arg_regs) {
                    DEBUG_CODEGEN_PRINT("Error: Failed to allocate temporary argument storage");
                    free(arg_regs);
                    return -1;
                }
                
                // First pass: Compile all arguments into temporary registers
                // This prevents parameter register corruption during argument evaluation
                for (int i = 0; i < arg_count; i++) {
                    // Use the already-typed argument from the call node
                    TypedASTNode* arg_typed = expr->typed.call.args[i];
                    if (!arg_typed) {
                        DEBUG_CODEGEN_PRINT("Error: Missing typed argument %d", i);
                        free(arg_regs);
                        free(temp_arg_regs);
                        return -1;
                    }
                    
                    // Compile argument into a temp register
                    int temp_arg_reg = compile_expression(ctx, arg_typed);
                    
                    if (temp_arg_reg == -1) {
                        DEBUG_CODEGEN_PRINT("Error: Failed to compile argument %d", i);
                        free(arg_regs);
                        free(temp_arg_regs);
                        return -1;
                    }
                    
                    temp_arg_regs[i] = temp_arg_reg;
                    DEBUG_CODEGEN_PRINT("NODE_CALL: Compiled argument %d into temporary R%d", i, temp_arg_reg);
                }
                
                // Second pass: Move all compiled arguments to consecutive registers
                for (int i = 0; i < arg_count; i++) {
                    emit_move(ctx, arg_regs[i], temp_arg_regs[i]);
                    DEBUG_CODEGEN_PRINT("NODE_CALL: Moved argument %d from R%d to consecutive R%d", i, temp_arg_regs[i], arg_regs[i]);
                    
                    // Free the temporary register if it's different from our allocated one
                    if (temp_arg_regs[i] != arg_regs[i] && temp_arg_regs[i] >= MP_TEMP_REG_START && temp_arg_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, temp_arg_regs[i]);
                    }
                }
                
                free(temp_arg_regs);
            }
            
            // Allocate register for return value
            int return_reg = compiler_alloc_temp(ctx->allocator);
            if (return_reg == -1) {
                DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for function return value");
                return -1;
            }

            // Emit OP_CALL_R instruction
            int actual_first_arg = (arg_count > 0) ? first_arg_reg : 0;
            emit_instruction_to_buffer(ctx->bytecode, OP_CALL_R, callee_reg, actual_first_arg, arg_count);
            emit_byte_to_buffer(ctx->bytecode, return_reg);
            DEBUG_CODEGEN_PRINT("NODE_CALL: Emitted OP_CALL_R callee=R%d, first_arg=R%d, args=%d, result=R%d",
                   callee_reg, actual_first_arg, arg_count, return_reg);

            // Free argument registers since they're temps
            if (arg_regs) {
                for (int i = 0; i < arg_count; i++) {
                    if (arg_regs[i] >= MP_TEMP_REG_START && arg_regs[i] <= MP_TEMP_REG_END) {
                        compiler_free_temp(ctx->allocator, arg_regs[i]);
                    }
                }
                free(arg_regs);
            }

            // Free callee register if temporary
            if (callee_reg >= MP_TEMP_REG_START && callee_reg <= MP_TEMP_REG_END) {
                compiler_free_temp(ctx->allocator, callee_reg);
            }

            return return_reg;
        }
        
        default:
            DEBUG_CODEGEN_PRINT("Error: Unsupported expression type: %d\n", expr->original->type);
            return -1;
    }
}

void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg) {
    if (!ctx || !literal || target_reg < 0) return;

    Value value = literal->original->literal.value;
    set_location_from_node(ctx, literal);
    emit_load_constant(ctx, target_reg, value);
}

void compile_binary_op(CompilerContext* ctx, TypedASTNode* binary, int target_reg, int left_reg, int right_reg) {
    if (!ctx || !binary || target_reg < 0 || left_reg < 0 || right_reg < 0) return;
    
    // Get the operator and operand types
    const char* op = binary->original->binary.op;

    // Original AST nodes used for fallback type inference when the typed AST
    // information is unavailable (e.g. the type checker failed earlier).
    const ASTNode* left_ast = binary->original ? binary->original->binary.left : NULL;
    const ASTNode* right_ast = binary->original ? binary->original->binary.right : NULL;

    // Get operand types from the typed AST nodes and fall back to reasonable
    // defaults when inference failed. We still want to emit bytecode even if the
    // type analyzer could not resolve the types (they may be treated as dynamic
    // values at runtime).
    Type* left_type = binary->typed.binary.left ? binary->typed.binary.left->resolvedType : NULL;
    Type* right_type = binary->typed.binary.right ? binary->typed.binary.right->resolvedType : NULL;

    Type left_fallback = {.kind = fallback_type_kind_from_ast(left_ast)};
    Type right_fallback = {.kind = fallback_type_kind_from_ast(right_ast)};

    if (!left_type || left_type->kind == TYPE_ERROR || left_type->kind == TYPE_UNKNOWN) {
        left_type = &left_fallback;
    }
    if (!right_type || right_type->kind == TYPE_ERROR || right_type->kind == TYPE_UNKNOWN) {
        right_type = &right_fallback;
    }

    DEBUG_CODEGEN_PRINT("Binary operation: %s, left_type=%d, right_type=%d\n", op, left_type->kind, right_type->kind);

    // Check if this is a comparison operation
    bool is_comparison = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                         strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                         strcmp(op, "==") == 0 || strcmp(op, "!=") == 0);

    // Determine the result type and handle type coercion
    Type* result_type = binary->resolvedType;
    bool result_type_valid = result_type &&
                             result_type->kind != TYPE_ERROR &&
                             result_type->kind != TYPE_UNKNOWN;
    Type result_fallback = {.kind = is_comparison ? TYPE_BOOL : left_type->kind};
    if (!result_type_valid) {
        result_type = &result_fallback;
    }
    int coerced_left_reg = left_reg;
    int coerced_right_reg = right_reg;

    if (is_comparison && result_type_valid) {
        // Retain the original resolved type when it is trustworthy.
        result_type = binary->resolvedType;
    }
    
    // Type coercion rules: promote to the "larger" type
    if (left_type->kind != right_type->kind) {
        DEBUG_CODEGEN_PRINT("Type mismatch detected: %d vs %d, applying coercion\n", left_type->kind, right_type->kind);
        
        // Determine the promoted type following simpler, safer rules
        TypeKind promoted_type = TYPE_I32; // Default fallback
        
        // FIXED: Simpler promotion rules to avoid problematic casts
        if ((left_type->kind == TYPE_I32 && right_type->kind == TYPE_I64) ||
            (left_type->kind == TYPE_I64 && right_type->kind == TYPE_I32)) {
            promoted_type = TYPE_I64;
        } else if ((left_type->kind == TYPE_U32 && right_type->kind == TYPE_U64) ||
                   (left_type->kind == TYPE_U64 && right_type->kind == TYPE_U32)) {
            promoted_type = TYPE_U64;
        } else if ((left_type->kind == TYPE_I32 && right_type->kind == TYPE_U32) ||
                   (left_type->kind == TYPE_U32 && right_type->kind == TYPE_I32)) {
            // FIXED: For u32 + i32, promote to u32 to avoid complex casts
            promoted_type = TYPE_U32;
        } else if (left_type->kind == TYPE_F64 || right_type->kind == TYPE_F64) {
            promoted_type = TYPE_F64;
        } else {
            // Default: use the larger type
            if (left_type->kind > right_type->kind) {
                promoted_type = left_type->kind;
            } else {
                promoted_type = right_type->kind;
            }
        }
        
        DEBUG_CODEGEN_PRINT("Promoting to type: %d\n", promoted_type);
        
        // Insert cast instruction for left operand if needed
        if (left_type->kind != promoted_type) {
            int cast_reg = compiler_alloc_temp(ctx->allocator);
            DEBUG_CODEGEN_PRINT("Casting left operand from %d to %d (R%d -> R%d)\n", 
                   left_type->kind, promoted_type, left_reg, cast_reg);
            
            // Emit appropriate cast instruction
            uint8_t cast_opcode = get_cast_opcode(left_type->kind, promoted_type);
            if (cast_opcode != OP_HALT) {
                emit_instruction_to_buffer(ctx->bytecode, cast_opcode, cast_reg, left_reg, 0);
                coerced_left_reg = cast_reg;
            }
        }
        
        // Insert cast instruction for right operand if needed
        if (right_type->kind != promoted_type) {
            int cast_reg = compiler_alloc_temp(ctx->allocator);
            DEBUG_CODEGEN_PRINT("Casting right operand from %d to %d (R%d -> R%d)\n", 
                   right_type->kind, promoted_type, right_reg, cast_reg);
            
            // Emit appropriate cast instruction
            uint8_t cast_opcode = get_cast_opcode(right_type->kind, promoted_type);
            if (cast_opcode != OP_HALT) {
                emit_instruction_to_buffer(ctx->bytecode, cast_opcode, cast_reg, right_reg, 0);
                coerced_right_reg = cast_reg;
            }
        }
        
        // Update the operation type to the promoted type
        Type promoted_type_obj = {.kind = promoted_type};
        result_type = &promoted_type_obj;
    }
    
    // Use the operand type (not the result type) for opcode selection
    Type* opcode_type = result_type;
    if (is_comparison) {
        // For comparisons, use the (promoted) operand type
        opcode_type = left_type->kind == right_type->kind ? left_type : result_type;
    }
    
    DEBUG_CODEGEN_PRINT("Emitting binary operation: %s (target=R%d, left=R%d, right=R%d, type=%d)%s\n",
           op, target_reg, coerced_left_reg, coerced_right_reg, opcode_type->kind,
           is_comparison ? " [COMPARISON]" : " [ARITHMETIC]");

    // Emit type-specific binary instruction (arithmetic or comparison)
    set_location_from_node(ctx, binary);
    emit_binary_op(ctx, op, opcode_type, target_reg, coerced_left_reg, coerced_right_reg);
    
    // Free any temporary cast registers
    if (coerced_left_reg != left_reg && coerced_left_reg >= MP_TEMP_REG_START && coerced_left_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, coerced_left_reg);
    }
    if (coerced_right_reg != right_reg && coerced_right_reg >= MP_TEMP_REG_START && coerced_right_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, coerced_right_reg);
    }
}

