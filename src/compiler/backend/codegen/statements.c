#include "compiler/codegen/statements.h"
#include "compiler/codegen/expressions.h"
#include "compiler/codegen/functions.h"
#include "compiler/codegen/modules.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/register_allocator.h"
#include "compiler/symbol_table.h"
#include "compiler/scope_stack.h"
#include "compiler/error_reporter.h"
#include "config/config.h"
#include "type/type.h"
#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "errors/features/variable_errors.h"
#include "errors/features/control_flow_errors.h"
#include "internal/error_reporting.h"
#include "debug/debug_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static bool node_type_is_expression(NodeType type) {
    switch (type) {
        case NODE_IDENTIFIER:
        case NODE_LITERAL:
        case NODE_ARRAY_LITERAL:
        case NODE_ARRAY_FILL:
        case NODE_ARRAY_SLICE:
        case NODE_INDEX_ACCESS:
        case NODE_BINARY:
        case NODE_TERNARY:
        case NODE_UNARY:
        case NODE_CALL:
        case NODE_CAST:
        case NODE_STRUCT_LITERAL:
        case NODE_MEMBER_ACCESS:
        case NODE_ENUM_MATCH_TEST:
        case NODE_ENUM_PAYLOAD:
        case NODE_MATCH_EXPRESSION:
        case NODE_TIME_STAMP:
        case NODE_TYPE:
            return true;
        default:
            return false;
    }
}

static const Type* get_effective_type(const TypedASTNode* node) {
    if (!node) {
        return NULL;
    }
    if (node->resolvedType) {
        return node->resolvedType;
    }
    if (node->original && node->original->dataType) {
        return node->original->dataType;
    }
    return NULL;
}

typedef enum {
    FUSED_LOOP_KIND_NONE = 0,
    FUSED_LOOP_KIND_WHILE,
    FUSED_LOOP_KIND_FOR_RANGE,
} FusedLoopKind;

typedef struct {
    FusedLoopKind kind;
    bool pattern_matched;
    bool can_fuse;
    bool inclusive;
    const char* loop_var_name;
    TypedASTNode* loop_var_node;
    TypedASTNode* limit_node;
    TypedASTNode* step_node;
    TypedASTNode* increment_stmt;
    TypedASTNode* body_node;
    bool body_is_block;
    int body_statement_count;
    bool has_increment;
    int limit_reg;
    bool limit_reg_is_temp;
    bool use_adjusted_limit;
    int adjusted_limit_reg;
    bool adjusted_limit_is_temp;
    bool limit_reg_is_primed;
    bool adjusted_limit_is_primed;
    int step_reg;
    bool step_reg_is_temp;
    bool step_is_one;
    bool step_known_positive;
    bool step_known_negative;
} FusedCounterLoopInfo;

static void init_fused_counter_loop_info(FusedCounterLoopInfo* info) {
    if (!info) {
        return;
    }
    memset(info, 0, sizeof(*info));
    info->kind = FUSED_LOOP_KIND_NONE;
    info->pattern_matched = false;
    info->limit_reg = -1;
    info->adjusted_limit_reg = -1;
    info->step_reg = -1;
    info->limit_reg_is_primed = false;
    info->adjusted_limit_is_primed = false;
    info->loop_var_node = NULL;
    info->limit_node = NULL;
    info->step_node = NULL;
    info->increment_stmt = NULL;
    info->body_node = NULL;
}

static bool node_matches_identifier(const TypedASTNode* node, const char* name) {
    if (!node || !node->original || node->original->type != NODE_IDENTIFIER || !name) {
        return false;
    }
    const char* candidate = node->original->identifier.name;
    return candidate && strcmp(candidate, name) == 0;
}

static bool try_prepare_fused_counter_loop(CompilerContext* ctx,
                                           TypedASTNode* loop_node,
                                           FusedCounterLoopInfo* info) {
    if (!info) {
        return false;
    }
    init_fused_counter_loop_info(info);

    if (!ctx || !loop_node || !loop_node->original) {
        return true;
    }

    NodeType node_type = loop_node->original->type;
    if (node_type == NODE_WHILE) {
        info->kind = FUSED_LOOP_KIND_WHILE;
        TypedASTNode* condition = loop_node->typed.whileStmt.condition;
        if (!condition || !condition->original || condition->original->type != NODE_BINARY) {
            return true;
        }

        const char* op = condition->original->binary.op;
        if (!op || (strcmp(op, "<") != 0 && strcmp(op, "<=") != 0)) {
            return true;
        }

        TypedASTNode* left = condition->typed.binary.left;
        TypedASTNode* right = condition->typed.binary.right;
        if (!left || !right) {
            return true;
        }

        const Type* left_type = get_effective_type(left);
        const Type* right_type = get_effective_type(right);
        if (!left_type || left_type->kind != TYPE_I32 ||
            !right_type || right_type->kind != TYPE_I32) {
            return true;
        }

        if (!left->original || left->original->type != NODE_IDENTIFIER) {
            return true;
        }

        const char* loop_var_name = left->original->identifier.name;
        if (!loop_var_name) {
            return true;
        }

        TypedASTNode* body = loop_node->typed.whileStmt.body;
        if (!body) {
            return true;
        }

        bool body_is_block = body->original && body->original->type == NODE_BLOCK;
        int body_count = 0;
        TypedASTNode* increment_stmt = NULL;
        if (body_is_block) {
            body_count = body->typed.block.count;
            if (body_count <= 0) {
                return true;
            }
            increment_stmt = body->typed.block.statements[body_count - 1];
        } else {
            body_count = body ? 1 : 0;
            increment_stmt = body;
        }

        if (!increment_stmt || !increment_stmt->original ||
            increment_stmt->original->type != NODE_ASSIGN ||
            !increment_stmt->typed.assign.name ||
            strcmp(increment_stmt->typed.assign.name, loop_var_name) != 0) {
            return true;
        }

        TypedASTNode* value = increment_stmt->typed.assign.value;
        if (!value || !value->original || value->original->type != NODE_BINARY) {
            return true;
        }

        const char* inc_op = value->original->binary.op;
        if (!inc_op || strcmp(inc_op, "+") != 0) {
            return true;
        }

        TypedASTNode* inc_left = value->typed.binary.left;
        TypedASTNode* inc_right = value->typed.binary.right;
        if (!inc_left || !inc_right) {
            return true;
        }

        int32_t inc_constant = 0;
        bool matches_increment = false;
        if (node_matches_identifier(inc_left, loop_var_name) &&
            evaluate_constant_i32(inc_right, &inc_constant) && inc_constant == 1) {
            matches_increment = true;
        } else if (node_matches_identifier(inc_right, loop_var_name) &&
                   evaluate_constant_i32(inc_left, &inc_constant) && inc_constant == 1) {
            matches_increment = true;
        }

        if (!matches_increment) {
            return true;
        }

        info->pattern_matched = true;
        info->can_fuse = true;
        info->inclusive = (strcmp(op, "<=") == 0);
        info->loop_var_name = loop_var_name;
        info->loop_var_node = left;
        info->limit_node = right;
        info->increment_stmt = increment_stmt;
        info->body_node = body;
        info->body_is_block = body_is_block;
        info->body_statement_count = body_count;
        info->has_increment = true;
        info->step_is_one = true;
        info->step_known_positive = true;
        info->step_known_negative = false;

        int limit_reg = compile_expression(ctx, right);
        if (limit_reg == -1) {
            return false;
        }
        ensure_i32_typed_register(ctx, limit_reg, right);
        info->limit_reg_is_primed = true;
        info->limit_reg = limit_reg;
        info->limit_reg_is_temp = (limit_reg >= MP_TEMP_REG_START && limit_reg <= MP_TEMP_REG_END);

        if (info->inclusive) {
            int temp_reg = compiler_alloc_temp(ctx->allocator);
            if (temp_reg == -1) {
                if (info->limit_reg_is_temp) {
                    compiler_free_temp(ctx->allocator, limit_reg);
                    info->limit_reg = -1;
                }
                return false;
            }
            set_location_from_node(ctx, loop_node);
            emit_byte_to_buffer(ctx->bytecode, OP_ADD_I32_IMM);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)temp_reg);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)limit_reg);
            int32_t one = 1;
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)(one & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 8) & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 16) & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 24) & 0xFF));
            info->use_adjusted_limit = true;
            info->adjusted_limit_reg = temp_reg;
            info->adjusted_limit_is_temp = true;
        }

        return true;
    }

    if (node_type == NODE_FOR_RANGE) {
        info->kind = FUSED_LOOP_KIND_FOR_RANGE;
        const char* loop_var_name = loop_node->typed.forRange.varName;
        if (!loop_var_name && loop_node->original) {
            loop_var_name = loop_node->original->forRange.varName;
        }
        info->loop_var_name = loop_var_name;

        TypedASTNode* end_node = loop_node->typed.forRange.end;
        if (!end_node) {
            return false;
        }

        int limit_reg = compile_expression(ctx, end_node);
        if (limit_reg == -1) {
            return false;
        }
        ensure_i32_typed_register(ctx, limit_reg, end_node);
        info->limit_reg_is_primed = true;

        info->pattern_matched = true;
        info->limit_node = end_node;
        info->limit_reg = limit_reg;
        info->limit_reg_is_temp = (limit_reg >= MP_TEMP_REG_START && limit_reg <= MP_TEMP_REG_END);
        info->inclusive = loop_node->typed.forRange.inclusive;

        TypedASTNode* step_node = loop_node->typed.forRange.step;
        info->step_node = step_node;
        int32_t step_constant = 0;
        if (step_node) {
            int step_reg = compile_expression(ctx, step_node);
            if (step_reg == -1) {
                if (info->limit_reg_is_temp) {
                    compiler_free_temp(ctx->allocator, limit_reg);
                    info->limit_reg = -1;
                }
                return false;
            }
            ensure_i32_typed_register(ctx, step_reg, step_node);
            info->step_reg = step_reg;
            info->step_reg_is_temp = (step_reg >= MP_TEMP_REG_START && step_reg <= MP_TEMP_REG_END);
            if (evaluate_constant_i32(step_node, &step_constant)) {
                if (step_constant >= 0) {
                    info->step_known_positive = true;
                }
                if (step_constant < 0) {
                    info->step_known_negative = true;
                }
                if (step_constant == 1) {
                    info->step_is_one = true;
                }
            }
        } else {
            int step_reg = compiler_alloc_temp(ctx->allocator);
            if (step_reg == -1) {
                if (info->limit_reg_is_temp) {
                    compiler_free_temp(ctx->allocator, limit_reg);
                    info->limit_reg = -1;
                }
                return false;
            }
            set_location_from_node(ctx, loop_node);
            emit_load_constant(ctx, step_reg, I32_VAL(1));
            info->step_reg = step_reg;
            info->step_reg_is_temp = true;
            info->step_known_positive = true;
            info->step_is_one = true;
            step_constant = 1;
        }

        if (!step_node || step_constant >= 0) {
            info->step_known_positive = true;
        }
        if (step_constant < 0) {
            info->step_known_negative = true;
        }
        if (step_constant == 1) {
            info->step_is_one = true;
        }

        info->can_fuse = (info->step_known_positive && info->step_is_one);

        if (info->can_fuse && info->inclusive) {
            int temp_reg = compiler_alloc_temp(ctx->allocator);
            if (temp_reg == -1) {
                if (info->step_reg_is_temp && info->step_reg >= MP_TEMP_REG_START &&
                    info->step_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, info->step_reg);
                    info->step_reg = -1;
                }
                if (info->limit_reg_is_temp && info->limit_reg >= MP_TEMP_REG_START &&
                    info->limit_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, info->limit_reg);
                    info->limit_reg = -1;
                }
                return false;
            }
            set_location_from_node(ctx, loop_node);
            emit_byte_to_buffer(ctx->bytecode, OP_ADD_I32_IMM);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)temp_reg);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)limit_reg);
            int32_t one = 1;
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)(one & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 8) & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 16) & 0xFF));
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)((one >> 24) & 0xFF));
            info->use_adjusted_limit = true;
            info->adjusted_limit_reg = temp_reg;
            info->adjusted_limit_is_temp = true;
        }

        return true;
    }

    return true;
}

static bool expression_node_has_value(const TypedASTNode* node) {
    const Type* type = get_effective_type(node);
    if (!type) {
        return true;
    }
    switch (type->kind) {
        case TYPE_VOID:
        case TYPE_ERROR:
            return false;
        default:
            return true;
    }
}

static void record_control_flow_error(CompilerContext* ctx,
                                      ErrorCode code,
                                      SrcLocation location,
                                      const char* message,
                                      const char* help) {
    if (!ctx || !ctx->errors) {
        return;
    }
    const char* note = NULL;
    char note_buffer[128];

    if (ctx->scopes) {
        int loop_depth = scope_stack_loop_depth(ctx->scopes);
        if (loop_depth <= 0) {
            snprintf(note_buffer, sizeof(note_buffer),
                     "Compiler scope stack reports no active loops at this point.");
            note = note_buffer;
        } else {
            ScopeFrame* active_loop = scope_stack_current_loop(ctx->scopes);
            if (active_loop) {
                snprintf(note_buffer, sizeof(note_buffer),
                         "Innermost loop bytecode span: start=%d, continue=%d, end=%d.",
                         active_loop->start_offset,
                         active_loop->continue_offset,
                         active_loop->end_offset);
                note = note_buffer;
            }
        }
    }

    error_reporter_add(ctx->errors, code, SEVERITY_ERROR, location, message, help, note);
}

static ScopeFrame* enter_loop_context(CompilerContext* ctx, int loop_start) {
    if (!ctx || !ctx->scopes) {
        return NULL;
    }

    ScopeFrame* frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LOOP);
    if (!frame) {
        return NULL;
    }

    control_flow_enter_loop_context();

    frame->start_offset = loop_start;
    frame->end_offset = -1;
    frame->continue_offset = loop_start;
    frame->prev_loop_id = ctx->current_loop_id;
    frame->loop_id = ctx->next_loop_id++;
    if (ctx->next_loop_id == 0) {
        ctx->next_loop_id = 1;
    }
    frame->prev_loop_start = ctx->current_loop_start;
    frame->prev_loop_end = ctx->current_loop_end;
    frame->prev_loop_continue = ctx->current_loop_continue;
    frame->saved_break_statements = ctx->break_statements;
    frame->saved_break_count = ctx->break_count;
    frame->saved_break_capacity = ctx->break_capacity;
    frame->saved_continue_statements = ctx->continue_statements;
    frame->saved_continue_count = ctx->continue_count;
    frame->saved_continue_capacity = ctx->continue_capacity;

    frame->loop_break_statements = NULL;
    frame->loop_break_count = 0;
    frame->loop_break_capacity = 0;
    frame->loop_continue_statements = NULL;
    frame->loop_continue_count = 0;
    frame->loop_continue_capacity = 0;
    frame->label = NULL;

    ctx->current_loop_start = loop_start;
    ctx->current_loop_end = loop_start;
    ctx->current_loop_continue = loop_start;
    ctx->current_loop_id = frame->loop_id;

    ctx->break_statements = NULL;
    ctx->break_count = 0;
    ctx->break_capacity = 0;
    ctx->continue_statements = NULL;
    ctx->continue_count = 0;
    ctx->continue_capacity = 0;

    return frame;
}

static void update_loop_continue_target(CompilerContext* ctx, ScopeFrame* frame, int continue_target) {
    if (!ctx) {
        return;
    }
    ctx->current_loop_continue = continue_target;
    if (frame) {
        frame->continue_offset = continue_target;
    }
}

static void leave_loop_context(CompilerContext* ctx, ScopeFrame* frame, int end_offset) {
    if (!ctx) {
        return;
    }

    if (frame && end_offset >= 0) {
        frame->end_offset = end_offset;
    }

    if (ctx->break_statements && (!frame || ctx->break_statements != frame->saved_break_statements)) {
        free(ctx->break_statements);
    }
    if (ctx->continue_statements && (!frame || ctx->continue_statements != frame->saved_continue_statements)) {
        free(ctx->continue_statements);
    }

    if (frame) {
        ctx->break_statements = frame->saved_break_statements;
        ctx->break_count = frame->saved_break_count;
        ctx->break_capacity = frame->saved_break_capacity;

        ctx->continue_statements = frame->saved_continue_statements;
        ctx->continue_count = frame->saved_continue_count;
        ctx->continue_capacity = frame->saved_continue_capacity;

        frame->loop_break_statements = NULL;
        frame->loop_break_count = 0;
        frame->loop_break_capacity = 0;
        frame->loop_continue_statements = NULL;
        frame->loop_continue_count = 0;
        frame->loop_continue_capacity = 0;
        frame->label = NULL;

        ctx->current_loop_start = frame->prev_loop_start;
        ctx->current_loop_end = frame->prev_loop_end;
        ctx->current_loop_continue = frame->prev_loop_continue;
        ctx->current_loop_id = frame->prev_loop_id;

        if (ctx->scopes) {
            scope_stack_pop(ctx->scopes);
        }
    } else {
        ctx->break_statements = NULL;
        ctx->break_count = 0;
        ctx->break_capacity = 0;
        ctx->continue_statements = NULL;
        ctx->continue_count = 0;
        ctx->continue_capacity = 0;
        ctx->current_loop_start = -1;
        ctx->current_loop_end = -1;
        ctx->current_loop_continue = -1;
        ctx->current_loop_id = 0;
    }

    control_flow_leave_loop_context();
}

static void release_typed_hint(CompilerContext* ctx, int* hint_reg) {
    if (!ctx || !ctx->allocator || !hint_reg) {
        return;
    }
    if (*hint_reg >= 0) {
        compiler_set_typed_residency_hint(ctx->allocator, *hint_reg, false);
        *hint_reg = -1;
    }
}

static void update_saved_break_pointer(CompilerContext* ctx, int* old_ptr, int* new_ptr) {
    if (!ctx || !ctx->scopes || old_ptr == new_ptr) {
        return;
    }

    ScopeStack* stack = ctx->scopes;
    for (int i = 0; i < stack->count; ++i) {
        ScopeFrame* sf = &stack->frames[i];
        if (sf->saved_break_statements == old_ptr) {
            sf->saved_break_statements = new_ptr;
        }
    }
}

static void update_saved_break_metadata(CompilerContext* ctx, int* ptr, int count, int capacity) {
    if (!ctx || !ctx->scopes || !ptr) {
        return;
    }

    ScopeStack* stack = ctx->scopes;
    for (int i = 0; i < stack->count; ++i) {
        ScopeFrame* sf = &stack->frames[i];
        if (sf->saved_break_statements == ptr) {
            sf->saved_break_count = count;
            sf->saved_break_capacity = capacity;
        }
    }
}

static void add_break_statement_to_frame(CompilerContext* ctx, ScopeFrame* frame, int patch_index) {
    if (!ctx || !frame) {
        return;
    }

    int* patches = frame->loop_break_statements;
    int count = frame->loop_break_count;
    int capacity = frame->loop_break_capacity;

    if (count >= capacity) {
        int new_capacity = capacity == 0 ? 4 : capacity * 2;
        int* new_array = malloc((size_t)new_capacity * sizeof(int));
        if (!new_array) {
            ctx->has_compilation_errors = true;
            return;
        }
        if (patches && count > 0) {
            memcpy(new_array, patches, (size_t)count * sizeof(int));
        }
        update_saved_break_pointer(ctx, patches, new_array);
        free(patches);
        patches = new_array;
        frame->loop_break_statements = new_array;
        frame->loop_break_capacity = new_capacity;
        capacity = new_capacity;
    }

    patches[count++] = patch_index;
    frame->loop_break_count = count;
    frame->loop_break_statements = patches;

    update_saved_break_metadata(ctx, patches, count, frame->loop_break_capacity);

    ScopeFrame* current = scope_stack_current_loop(ctx->scopes);
    if (current == frame) {
        ctx->break_statements = patches;
        ctx->break_count = count;
        ctx->break_capacity = frame->loop_break_capacity;
    }
}

static void update_saved_continue_pointer(CompilerContext* ctx, int* old_ptr, int* new_ptr) {
    if (!ctx || !ctx->scopes || old_ptr == new_ptr) {
        return;
    }

    ScopeStack* stack = ctx->scopes;
    for (int i = 0; i < stack->count; ++i) {
        ScopeFrame* sf = &stack->frames[i];
        if (sf->saved_continue_statements == old_ptr) {
            sf->saved_continue_statements = new_ptr;
        }
    }
}

static void update_saved_continue_metadata(CompilerContext* ctx, int* ptr, int count, int capacity) {
    if (!ctx || !ctx->scopes || !ptr) {
        return;
    }

    ScopeStack* stack = ctx->scopes;
    for (int i = 0; i < stack->count; ++i) {
        ScopeFrame* sf = &stack->frames[i];
        if (sf->saved_continue_statements == ptr) {
            sf->saved_continue_count = count;
            sf->saved_continue_capacity = capacity;
        }
    }
}

static void add_continue_statement_to_frame(CompilerContext* ctx, ScopeFrame* frame, int patch_index) {
    if (!ctx || !frame) {
        return;
    }

    int* patches = frame->loop_continue_statements;
    int count = frame->loop_continue_count;
    int capacity = frame->loop_continue_capacity;

    if (count >= capacity) {
        int new_capacity = capacity == 0 ? 4 : capacity * 2;
        int* new_array = malloc((size_t)new_capacity * sizeof(int));
        if (!new_array) {
            ctx->has_compilation_errors = true;
            return;
        }
        if (patches && count > 0) {
            memcpy(new_array, patches, (size_t)count * sizeof(int));
        }
        update_saved_continue_pointer(ctx, patches, new_array);
        free(patches);
        patches = new_array;
        frame->loop_continue_statements = new_array;
        frame->loop_continue_capacity = new_capacity;
        capacity = new_capacity;
    }

    patches[count++] = patch_index;
    frame->loop_continue_count = count;
    frame->loop_continue_statements = patches;

    update_saved_continue_metadata(ctx, patches, count, frame->loop_continue_capacity);

    ScopeFrame* current = scope_stack_current_loop(ctx->scopes);
    if (current == frame) {
        ctx->continue_statements = patches;
        ctx->continue_count = count;
        ctx->continue_capacity = frame->loop_continue_capacity;
    }
}

static void compile_import_statement(CompilerContext* ctx, TypedASTNode* stmt) {
    if (!ctx || !stmt || !stmt->original) {
        return;
    }

    ModuleManager* manager = vm.register_file.module_manager;
    const char* module_name = stmt->original->import.moduleName;
    SrcLocation location = stmt->original->location;

    if (!manager) {
        report_compile_error(E3004_IMPORT_FAILED, location, "module manager is not initialized");
        ctx->has_compilation_errors = true;
        return;
    }

    if (!module_name) {
        report_compile_error(E3004_IMPORT_FAILED, location, "expected module name for use statement");
        ctx->has_compilation_errors = true;
        return;
    }

    RegisterModule* module_entry = find_module(manager, module_name);
    if (!module_entry) {
        report_compile_error(E3003_MODULE_NOT_FOUND, location,
                             "module '%s' is not loaded", module_name);
        ctx->has_compilation_errors = true;
        return;
    }

    if (stmt->original->import.importModule) {
        return;
    }

    if (stmt->original->import.importAll) {
        bool imported_any = false;
        for (uint16_t i = 0; i < module_entry->exports.export_count; i++) {
            const char* symbol_name = module_entry->exports.exported_names[i];
            if (!symbol_name) {
                continue;
            }
            ModuleExportKind kind = module_entry->exports.exported_kinds[i];
            uint16_t reg = module_entry->exports.exported_registers[i];
            Type* exported_type = NULL;
            if (module_entry->exports.exported_types && i < module_entry->exports.export_count) {
                exported_type = module_entry->exports.exported_types[i];
            }
            if (finalize_import_symbol(ctx, module_name, symbol_name, NULL, kind, reg,
                                       exported_type, location)) {
                imported_any = true;
            }
        }

        if (!imported_any) {
            report_compile_error(E3004_IMPORT_FAILED, location,
                                 "module '%s' has no usable globals, functions, or types", module_name);
            ctx->has_compilation_errors = true;
        }
        return;
    }

    for (int i = 0; i < stmt->original->import.symbolCount; i++) {
        ImportSymbol* symbol = &stmt->original->import.symbols[i];
        if (!symbol->name) {
            continue;
        }
        import_symbol_by_name(ctx, manager, module_name, symbol->name, symbol->alias, location);
    }
}

// ===== STATEMENT COMPILATION =====

static void compile_expression_statement(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr) {
        return;
    }

    int result_reg = compile_expression(ctx, expr);
    if (result_reg == -1) {
        return;
    }

    bool should_print = !ctx->compiling_function && repl_mode_active() &&
                        expression_node_has_value(expr);
    if (should_print) {
        set_location_from_node(ctx, expr);
        emit_byte_to_buffer(ctx->bytecode, OP_PRINT_R);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)result_reg);
    }

    if (result_reg >= MP_TEMP_REG_START && result_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, result_reg);
    }
}

void compile_statement(CompilerContext* ctx, TypedASTNode* stmt) {
    if (!ctx || !stmt) return;

    NodeType node_type = stmt->original ? stmt->original->type : NODE_PROGRAM;

    if (node_type_is_expression(node_type)) {
        compile_expression_statement(ctx, stmt);
        return;
    }

    DEBUG_CODEGEN_PRINT("Compiling statement type %d\n", node_type);

    switch (node_type) {
        case NODE_ASSIGN:
            compile_assignment(ctx, stmt);
            break;
        case NODE_ARRAY_ASSIGN:
            compile_array_assignment(ctx, stmt, false);
            break;
        case NODE_MEMBER_ASSIGN:
            compile_member_assignment(ctx, stmt, false);
            break;

        case NODE_VAR_DECL:
            if (!ctx->compiling_function && stmt->original->varDecl.isPublic &&
                stmt->original->varDecl.isGlobal && stmt->original->varDecl.name) {
                Type* export_type = NULL;
                if (stmt->typed.varDecl.initializer && stmt->typed.varDecl.initializer->resolvedType) {
                    export_type = stmt->typed.varDecl.initializer->resolvedType;
                } else if (stmt->resolvedType) {
                    export_type = stmt->resolvedType;
                }
                record_module_export(ctx, stmt->original->varDecl.name, MODULE_EXPORT_KIND_GLOBAL,
                                     export_type);
            }
            compile_variable_declaration(ctx, stmt);
            break;
            
        case NODE_PRINT:
            compile_print_statement(ctx, stmt);
            break;
            
        case NODE_IF:
            compile_if_statement(ctx, stmt);
            break;
        case NODE_BLOCK: {
            bool create_scope = true;
            if (stmt->original && stmt->original->type == NODE_BLOCK) {
                create_scope = stmt->original->block.createsScope;
            }
            compile_block_with_scope(ctx, stmt, create_scope);
            break;
        }
            
        case NODE_WHILE:
            compile_while_statement(ctx, stmt);
            break;

        case NODE_TRY:
            compile_try_statement(ctx, stmt);
            break;

        case NODE_BREAK:
            compile_break_statement(ctx, stmt);
            break;

        case NODE_CONTINUE:
            compile_continue_statement(ctx, stmt);
            break;

        case NODE_PASS:
            // No bytecode emitted for pass statements
            break;

        case NODE_FOR_RANGE:
            compile_for_range_statement(ctx, stmt);
            break;
            
        case NODE_FOR_ITER:
            compile_for_iter_statement(ctx, stmt);
            break;
            
        case NODE_FUNCTION:
            if (!ctx->compiling_function && stmt->original->function.isPublic &&
                !stmt->original->function.isMethod && stmt->original->function.name) {
                record_module_export(ctx, stmt->original->function.name, MODULE_EXPORT_KIND_FUNCTION,
                                     stmt->resolvedType);
            }
            compile_function_declaration(ctx, stmt);
            break;

        case NODE_IMPORT:
            compile_import_statement(ctx, stmt);
            break;

        case NODE_RETURN:
            compile_return_statement(ctx, stmt);
            break;

        case NODE_ENUM_MATCH_CHECK:
            // Compile-time exhaustiveness checks generate this node; no runtime emission required.
            break;
        case NODE_STRUCT_DECL:
            if (!ctx->compiling_function && stmt->original->structDecl.isPublic &&
                stmt->original->structDecl.name) {
                Type* struct_type = findStructType(stmt->original->structDecl.name);
                record_module_export(ctx, stmt->original->structDecl.name, MODULE_EXPORT_KIND_STRUCT,
                                     struct_type);
            }
            break;
        case NODE_ENUM_DECL:
            if (!ctx->compiling_function && stmt->original->type == NODE_ENUM_DECL &&
                stmt->original->enumDecl.isPublic && stmt->original->enumDecl.name) {
                Type* enum_type = findEnumType(stmt->original->enumDecl.name);
                record_module_export(ctx, stmt->original->enumDecl.name, MODULE_EXPORT_KIND_ENUM,
                                     enum_type);
            }
            break;
        case NODE_IMPL_BLOCK:
            // Emit bytecode for methods inside impl blocks
            if (stmt->original->type == NODE_IMPL_BLOCK &&
                stmt->typed.implBlock.methodCount > 0) {
                for (int i = 0; i < stmt->typed.implBlock.methodCount; i++) {
                    if (stmt->typed.implBlock.methods[i]) {
                        compile_function_declaration(ctx, stmt->typed.implBlock.methods[i]);
                    }
                }
            }
            break;

        default:
            DEBUG_CODEGEN_PRINT("Warning: Unsupported statement type: %d\n", node_type);
            break;
    }
}

void compile_variable_declaration(CompilerContext* ctx, TypedASTNode* var_decl) {
    if (!ctx || !var_decl) return;
    
    // Get variable information from AST
    const char* var_name = var_decl->original->varDecl.name;
    bool is_mutable = var_decl->original->varDecl.isMutable;
    
    DEBUG_CODEGEN_PRINT("Compiling variable declaration: %s (mutable=%s)\n", 
           var_name, is_mutable ? "true" : "false");
    
    SrcLocation decl_location = var_decl->original->location;

    Symbol* existing = resolve_symbol_local_only(ctx->symbols, var_name);
    if (existing) {
        bool reported = false;
        if (ctx && ctx->errors) {
            if (existing->declaration_location.line > 0) {
                reported = error_reporter_add_feature_error(ctx->errors, E1011_VARIABLE_REDEFINITION,
                                                           decl_location,
                                                           "Variable '%s' is already defined on line %d",
                                                           var_name, existing->declaration_location.line);
            } else {
                reported = error_reporter_add_feature_error(ctx->errors, E1011_VARIABLE_REDEFINITION,
                                                           decl_location,
                                                           "Variable '%s' is already defined in this scope",
                                                           var_name);
            }
        }
        if (!reported) {
            report_variable_redefinition(decl_location, var_name,
                                         existing->declaration_location.line);
        }
        ctx->has_compilation_errors = true;
        if (var_decl->typed.varDecl.initializer) {
            compile_expression(ctx, var_decl->typed.varDecl.initializer);
        }
        return;
    }

    // Compile the initializer expression if it exists
    int value_reg = -1;
    if (var_decl->typed.varDecl.initializer) {
        // Use the proper typed AST initializer node, not a temporary one
        value_reg = compile_expression(ctx, var_decl->typed.varDecl.initializer);
        if (value_reg == -1) {
            DEBUG_CODEGEN_PRINT("Error: Failed to compile variable initializer");
            return;
        }
    }
    
    // Allocate register based on scope
    bool wants_global = var_decl->original->varDecl.isGlobal;
    bool use_global_register = !ctx->compiling_function || wants_global;

    int var_reg;
    if (use_global_register) {
        var_reg = compiler_alloc_global(ctx->allocator);
        if (var_reg == -1) {
            var_reg = compiler_alloc_frame(ctx->allocator);
        }
    } else {
        var_reg = compiler_alloc_frame(ctx->allocator);
    }
    if (var_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for variable %s\n", var_name);
        if (value_reg != -1) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        return;
    }

    // Register the variable in symbol table
    Type* variable_type = NULL;
    if (var_decl->typed.varDecl.initializer &&
        var_decl->typed.varDecl.initializer->resolvedType) {
        variable_type = var_decl->typed.varDecl.initializer->resolvedType;
    } else if (var_decl->typed.varDecl.typeAnnotation &&
               var_decl->typed.varDecl.typeAnnotation->resolvedType) {
        variable_type = var_decl->typed.varDecl.typeAnnotation->resolvedType;
    } else if (var_decl->original && var_decl->original->varDecl.typeAnnotation &&
               var_decl->original->varDecl.typeAnnotation->dataType) {
        variable_type = var_decl->original->varDecl.typeAnnotation->dataType;
    } else if (var_decl->resolvedType) {
        variable_type = var_decl->resolvedType;
    }

    Symbol* symbol = register_variable(ctx, ctx->symbols, var_name, var_reg,
                                       variable_type,
                                       is_mutable, is_mutable, decl_location, value_reg != -1);
    if (!symbol) {
        compiler_free_register(ctx->allocator, var_reg);
        if (value_reg != -1) {
            compiler_free_temp(ctx->allocator, value_reg);
        }
        return;
    }

    if (!ctx->compiling_function && ctx->is_module && var_name &&
        var_decl->original->varDecl.isPublic && var_decl->original->varDecl.isGlobal) {
        set_module_export_metadata(ctx, var_name, var_reg, var_decl->resolvedType);
    }

    // Move the initial value to the variable register if we have one
    if (value_reg != -1) {
        set_location_from_node(ctx, var_decl);
        emit_move(ctx, var_reg, value_reg);
        compiler_free_temp(ctx->allocator, value_reg);
        symbol->last_assignment_location = decl_location;
        symbol->is_initialized = true;
    }

    DEBUG_CODEGEN_PRINT("Declared variable %s -> R%d\n", var_name, var_reg);
}

int compile_array_assignment(CompilerContext* ctx, TypedASTNode* assign,
                                    bool as_expression) {
    if (!ctx || !assign) {
        return -1;
    }

    TypedASTNode* target = assign->typed.arrayAssign.target;
    TypedASTNode* value_node = assign->typed.arrayAssign.value;
    if (!target || !value_node || !target->typed.indexAccess.array ||
        !target->typed.indexAccess.index) {
        return -1;
    }

    int array_reg = compile_expression(ctx, target->typed.indexAccess.array);
    if (array_reg == -1) {
        return -1;
    }

    int index_reg = compile_expression(ctx, target->typed.indexAccess.index);
    if (index_reg == -1) {
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        return -1;
    }

    int value_reg = compile_expression(ctx, value_node);
    if (value_reg == -1) {
        if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, index_reg);
        }
        if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, array_reg);
        }
        return -1;
    }

    set_location_from_node(ctx, assign);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_SET_R);
    emit_byte_to_buffer(ctx->bytecode, array_reg);
    emit_byte_to_buffer(ctx->bytecode, index_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, index_reg);
    }
    if (array_reg >= MP_TEMP_REG_START && array_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, array_reg);
    }

    bool value_is_temp = value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END;
    int result_reg = value_reg;

    if (!as_expression && value_is_temp) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    return result_reg;
}

int compile_member_assignment(CompilerContext* ctx, TypedASTNode* assign,
                                     bool as_expression) {
    if (!ctx || !assign || assign->original->type != NODE_MEMBER_ASSIGN) {
        return -1;
    }

    TypedASTNode* target = assign->typed.memberAssign.target;
    TypedASTNode* value_node = assign->typed.memberAssign.value;
    if (!target || !value_node || !target->typed.member.object) {
        return -1;
    }

    if (target->typed.member.isMethod) {
        if (ctx->errors) {
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               assign->original->location,
                               "Cannot assign to method reference",
                               "Only struct fields can appear on the left-hand side",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    int field_index = resolve_struct_field_index(target->typed.member.object->resolvedType,
                                                 target->typed.member.member);
    if (field_index < 0) {
        if (ctx->errors) {
            error_reporter_add(ctx->errors, map_error_type_to_code(ERROR_TYPE), SEVERITY_ERROR,
                               assign->original->location,
                               "Unknown struct field",
                               target->typed.member.member ? target->typed.member.member : "<unknown>",
                               NULL);
        }
        ctx->has_compilation_errors = true;
        return -1;
    }

    int object_reg = compile_expression(ctx, target->typed.member.object);
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

    int value_reg = compile_expression(ctx, value_node);
    if (value_reg == -1) {
        if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, index_reg);
        }
        if (object_reg >= MP_TEMP_REG_START && object_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, object_reg);
        }
        return -1;
    }

    set_location_from_node(ctx, assign);
    emit_byte_to_buffer(ctx->bytecode, OP_ARRAY_SET_R);
    emit_byte_to_buffer(ctx->bytecode, object_reg);
    emit_byte_to_buffer(ctx->bytecode, index_reg);
    emit_byte_to_buffer(ctx->bytecode, value_reg);

    if (index_reg >= MP_TEMP_REG_START && index_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, index_reg);
    }
    if (object_reg >= MP_TEMP_REG_START && object_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, object_reg);
    }

    bool value_is_temp = value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END;
    if (!as_expression && value_is_temp) {
        compiler_free_temp(ctx->allocator, value_reg);
    }

    return value_reg;
}

int compile_assignment_internal(CompilerContext* ctx, TypedASTNode* assign,
                                       bool as_expression) {
    if (!ctx || !assign) return -1;

    const char* var_name = assign->typed.assign.name;
    SrcLocation location = assign->original->location;
    Symbol* symbol = resolve_symbol(ctx->symbols, var_name);
    if (!symbol) {
        int value_reg = compile_expression(ctx, assign->typed.assign.value);
        if (value_reg == -1) return -1;

        int var_reg = -1;
        if (ctx->compiling_function) {
            var_reg = compiler_alloc_frame(ctx->allocator);
        } else {
            var_reg = compiler_alloc_global(ctx->allocator);
            if (var_reg == -1) {
                var_reg = compiler_alloc_frame(ctx->allocator);
            }
        }

        if (var_reg == -1) {
            compiler_free_temp(ctx->allocator, value_reg);
            return -1;
        }

        bool is_in_loop = (ctx->current_loop_start != -1);
        bool should_be_mutable = is_in_loop || ctx->branch_depth > 0 || ctx->compiling_function;

        SymbolTable* target_scope = ctx->symbols;
        if (ctx->branch_depth > 0 && target_scope) {
            SymbolTable* candidate = target_scope;
            int remaining = ctx->branch_depth;
            while (remaining > 0 && candidate && candidate->parent &&
                   candidate->scope_depth > ctx->function_scope_depth) {
                candidate = candidate->parent;
                remaining--;
            }
            if (candidate) {
                target_scope = candidate;
            }
        }

        Type* value_type = NULL;
        if (assign->typed.assign.value) {
            value_type = assign->typed.assign.value->resolvedType;
            if (!value_type && assign->typed.assign.value->original) {
                value_type = assign->typed.assign.value->original->dataType;
            }
        }

        if (!register_variable(ctx, target_scope, var_name, var_reg,
                               value_type ? value_type : assign->resolvedType,
                               should_be_mutable, false,
                               location, true)) {
            compiler_free_register(ctx->allocator, var_reg);
            compiler_free_temp(ctx->allocator, value_reg);
            return -1;
        }

        set_location_from_node(ctx, assign);
        emit_move(ctx, var_reg, value_reg);
        compiler_free_temp(ctx->allocator, value_reg);
        return var_reg;
    }

    bool is_upvalue = false;
    int upvalue_index = -1;
    int resolved_reg = resolve_variable_or_upvalue(ctx, var_name, &is_upvalue, &upvalue_index);
    if (resolved_reg == -1 && !is_upvalue) {
        report_scope_violation(location, var_name,
                               get_variable_scope_info(var_name, ctx->symbols->scope_depth));
        ctx->has_compilation_errors = true;
        compile_expression(ctx, assign->typed.assign.value);
        return -1;
    }

    if (!symbol->is_mutable) {
        report_immutable_variable_assignment(location, var_name);
        ctx->has_compilation_errors = true;
        return -1;
    }

    if (is_upvalue && symbol && !symbol->declared_mutable) {
        report_immutable_variable_assignment(location, var_name);
        ctx->has_compilation_errors = true;
        return -1;
    }

    int var_reg_direct = -1;
    if (!is_upvalue) {
        var_reg_direct = resolved_reg;
        if (var_reg_direct < 0 && symbol) {
            var_reg_direct = symbol->reg_allocation ? symbol->reg_allocation->logical_id
                                                    : symbol->legacy_register_id;
        }
    }

    bool emitted_fast_inc = false;
    if (!as_expression && !is_upvalue && var_reg_direct >= 0 && symbol &&
        assign->resolvedType) {
        TypeKind inc_type = assign->resolvedType->kind;
        uint8_t inc_opcode = 0;
        switch (inc_type) {
            case TYPE_I32:
                inc_opcode = OP_INC_I32_CHECKED;
                break;
            case TYPE_I64:
                inc_opcode = OP_INC_I64_CHECKED;
                break;
            case TYPE_U32:
                inc_opcode = OP_INC_U32_CHECKED;
                break;
            case TYPE_U64:
                inc_opcode = OP_INC_U64_CHECKED;
                break;
            default:
                break;
        }

        if (inc_opcode != 0) {
            TypedASTNode* value_node = assign->typed.assign.value;
            if (value_node && value_node->original &&
                value_node->original->type == NODE_BINARY &&
                value_node->original->binary.op &&
                strcmp(value_node->original->binary.op, "+") == 0 &&
                value_node->resolvedType &&
                value_node->resolvedType->kind == inc_type) {
                TypedASTNode* left = value_node->typed.binary.left;
                TypedASTNode* right = value_node->typed.binary.right;
                int32_t increment = 0;
                bool matches_pattern = false;
                if (left && left->original &&
                    left->original->type == NODE_IDENTIFIER &&
                    left->original->identifier.name &&
                    strcmp(left->original->identifier.name, var_name) == 0 &&
                    evaluate_constant_i32(right, &increment) && increment == 1) {
                    matches_pattern = true;
                } else if (right && right->original &&
                           right->original->type == NODE_IDENTIFIER &&
                           right->original->identifier.name &&
                           strcmp(right->original->identifier.name, var_name) == 0 &&
                           evaluate_constant_i32(left, &increment) && increment == 1) {
                    matches_pattern = true;
                }

                if (matches_pattern) {
                    set_location_from_node(ctx, assign);
                    emit_byte_to_buffer(ctx->bytecode, inc_opcode);
                    emit_byte_to_buffer(ctx->bytecode, (uint8_t)var_reg_direct);
                    mark_symbol_arithmetic_heavy(symbol);
                    emitted_fast_inc = true;
                }
            }
        }
    }

    if (emitted_fast_inc) {
        symbol->is_initialized = true;
        symbol->last_assignment_location = location;
        return var_reg_direct;
    }

    int value_reg = compile_expression(ctx, assign->typed.assign.value);
    if (value_reg == -1) return -1;
    bool value_is_temp = value_reg >= MP_TEMP_REG_START && value_reg <= MP_TEMP_REG_END;

    int result_reg = -1;

    if (is_upvalue) {
        if (upvalue_index < 0) {
            report_scope_violation(location, var_name,
                                   get_variable_scope_info(var_name, ctx->symbols->scope_depth));
            ctx->has_compilation_errors = true;
            if (value_is_temp) {
                compiler_free_temp(ctx->allocator, value_reg);
            }
            return -1;
        }
        set_location_from_node(ctx, assign);
        emit_byte_to_buffer(ctx->bytecode, OP_SET_UPVALUE_R);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)upvalue_index);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)value_reg);
        result_reg = value_reg;
    } else {
        int var_reg = var_reg_direct;
        if (var_reg < 0) {
            var_reg = symbol->reg_allocation ? symbol->reg_allocation->logical_id
                                             : symbol->legacy_register_id;
        }
        set_location_from_node(ctx, assign);
        emit_move(ctx, var_reg, value_reg);
        result_reg = var_reg;
    }

    if (value_is_temp && !(as_expression && is_upvalue)) {
        compiler_free_temp(ctx->allocator, value_reg);
    }
    symbol->is_initialized = true;
    symbol->last_assignment_location = location;

    return result_reg;
}

void compile_assignment(CompilerContext* ctx, TypedASTNode* assign) {
    compile_assignment_internal(ctx, assign, false);
}

void compile_print_statement(CompilerContext* ctx, TypedASTNode* print) {
    if (!ctx || !print) return;
    
    // Use the VM's builtin print implementation through OP_PRINT_R
    // This integrates with handle_print() which calls builtin_print()
    
    if (print->typed.print.count == 0) {
        // Print with no arguments - use register 0 (standard behavior)
        // OP_PRINT_R format: opcode + register (2 bytes total)
        set_location_from_node(ctx, print);
        emit_byte_to_buffer(ctx->bytecode, OP_PRINT_R);
        emit_byte_to_buffer(ctx->bytecode, 0);
        DEBUG_CODEGEN_PRINT("Emitted OP_PRINT_R R0 (no arguments)");
    } else if (print->typed.print.count == 1) {
        // Single expression print - compile expression and emit print
        TypedASTNode* expr = print->typed.print.values[0];
        int reg = compile_expression(ctx, expr);

        if (reg != -1) {
            // Use OP_PRINT_R which calls handle_print() -> builtin_print()
            // OP_PRINT_R format: opcode + register (2 bytes total)
            set_location_from_node(ctx, print);
            emit_byte_to_buffer(ctx->bytecode, OP_PRINT_R);
            emit_byte_to_buffer(ctx->bytecode, reg);
            DEBUG_CODEGEN_PRINT("Emitted OP_PRINT_R R%d (single expression)\n", reg);
            
            // Free the temporary register
            compiler_free_temp(ctx->allocator, reg);
        }
    } else {
        // Multiple expressions - need consecutive registers for OP_PRINT_MULTI_R
        // FIXED: Allocate consecutive registers FIRST to prevent register conflicts
        int first_consecutive_reg = compiler_alloc_temp(ctx->allocator);
        if (first_consecutive_reg == -1) {
            DEBUG_CODEGEN_PRINT("Error: Failed to allocate consecutive registers for print");
            return;
        }
        
        // Reserve additional consecutive registers
        for (int i = 1; i < print->typed.print.count; i++) {
            int next_reg = compiler_alloc_temp(ctx->allocator);
            if (next_reg != first_consecutive_reg + i) {
                DEBUG_CODEGEN_PRINT("Warning: Non-consecutive register allocated: R%d (expected R%d)\n", 
                       next_reg, first_consecutive_reg + i);
            }
        }
        
        // Now compile expressions directly into the consecutive registers
        for (int i = 0; i < print->typed.print.count; i++) {
            TypedASTNode* expr = print->typed.print.values[i];
            int target_reg = first_consecutive_reg + i;
            
            // Compile expression and move to target register if different
            int expr_reg = compile_expression(ctx, expr);
            if (expr_reg != -1 && expr_reg != target_reg) {
                set_location_from_node(ctx, expr);
                emit_move(ctx, target_reg, expr_reg);

                // Free the original temp register
                if (expr_reg >= MP_TEMP_REG_START && expr_reg <= MP_TEMP_REG_END) {
                    compiler_free_temp(ctx->allocator, expr_reg);
                }
            }
        }

        // Emit the print instruction with consecutive registers
        set_location_from_node(ctx, print);
        emit_instruction_to_buffer(ctx->bytecode, OP_PRINT_MULTI_R,
                                 first_consecutive_reg, print->typed.print.count, 1); // 1 = newline
        DEBUG_CODEGEN_PRINT("Emitted OP_PRINT_MULTI_R R%d, count=%d (consecutive registers)\n",
               first_consecutive_reg, print->typed.print.count);
        
        // Free the consecutive temp registers
        for (int i = 0; i < print->typed.print.count; i++) {
            compiler_free_temp(ctx->allocator, first_consecutive_reg + i);
        }
    }
}

// ===== CONTROL FLOW COMPILATION =====

void compile_if_statement(CompilerContext* ctx, TypedASTNode* if_stmt) {
    if (!ctx || !if_stmt) return;

    DEBUG_CODEGEN_PRINT("Compiling if statement");
    
    // Compile condition expression
    int condition_reg = compile_expression(ctx, if_stmt->typed.ifStmt.condition);
    if (condition_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to compile if condition");
        return;
    }
    
    // Emit conditional jump - if condition is false, jump to else/end
    // OP_JUMP_IF_NOT_R format: opcode + condition_reg + 2-byte offset (4 bytes total for patching)
    set_location_from_node(ctx, if_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_R);
    emit_byte_to_buffer(ctx->bytecode, condition_reg);
    int else_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_R);
    if (else_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate else jump placeholder\n");
        ctx->has_compilation_errors = true;
        return;
    }
    DEBUG_CODEGEN_PRINT("Emitted OP_JUMP_IF_NOT_R R%d (placeholder index %d)\n",
           condition_reg, else_patch);
    
    // Free condition register
    if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, condition_reg);
    }
    
    // Compile then branch with new scope
    ctx->branch_depth++;
    compile_block_with_scope(ctx, if_stmt->typed.ifStmt.thenBranch, true);
    ctx->branch_depth--;
    
    // If there's an else branch, emit unconditional jump to skip it
    int end_patch = -1;
    if (if_stmt->typed.ifStmt.elseBranch) {
        set_location_from_node(ctx, if_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP_SHORT);
        end_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_SHORT);
        if (end_patch < 0) {
            DEBUG_CODEGEN_PRINT("Error: Failed to allocate end jump placeholder\n");
            ctx->has_compilation_errors = true;
            return;
        }
        DEBUG_CODEGEN_PRINT("Emitted OP_JUMP_SHORT (placeholder index %d)\n", end_patch);
    }

    // Patch the else jump to current position
    int else_target = ctx->bytecode->count;
    if (!patch_jump(ctx->bytecode, else_patch, else_target)) {
        DEBUG_CODEGEN_PRINT("Error: Failed to patch else jump to target %d\n", else_target);
        ctx->has_compilation_errors = true;
        return;
    }
    DEBUG_CODEGEN_PRINT("Patched else jump to %d\n", else_target);
    
    // Compile else branch if present
    if (if_stmt->typed.ifStmt.elseBranch) {
        ctx->branch_depth++;
        compile_block_with_scope(ctx, if_stmt->typed.ifStmt.elseBranch, true);
        ctx->branch_depth--;
        
        int end_target = ctx->bytecode->count;
        if (!patch_jump(ctx->bytecode, end_patch, end_target)) {
            DEBUG_CODEGEN_PRINT("Error: Failed to patch end jump to target %d\n", end_target);
            ctx->has_compilation_errors = true;
            return;
        }
        DEBUG_CODEGEN_PRINT("Patched end jump to %d\n", end_target);
    }
    
    DEBUG_CODEGEN_PRINT("If statement compilation completed");
}

void compile_try_statement(CompilerContext* ctx, TypedASTNode* try_stmt) {
    if (!ctx || !try_stmt) {
        return;
    }

    DEBUG_CODEGEN_PRINT("Compiling try/catch statement");

    bool has_catch_block = try_stmt->typed.tryStmt.catchBlock != NULL;
    bool has_catch_var = try_stmt->typed.tryStmt.catchVarName != NULL;

    int catch_reg = -1;
    bool catch_reg_allocated = false;
    bool catch_reg_bound = false;
    uint8_t catch_operand = 0xFF; // Sentinel indicating no catch register

    if (has_catch_var) {
        catch_reg = compiler_alloc_frame(ctx->allocator);
        if (catch_reg == -1) {
            DEBUG_CODEGEN_PRINT("Error: Failed to allocate register for catch variable");
            ctx->has_compilation_errors = true;
            return;
        }
        catch_reg_allocated = true;
        catch_operand = (uint8_t)catch_reg;
    }

    set_location_from_node(ctx, try_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_TRY_BEGIN);
    emit_byte_to_buffer(ctx->bytecode, catch_operand);
    int handler_patch = emit_jump_placeholder(ctx->bytecode, OP_TRY_BEGIN);
    if (handler_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate jump placeholder for catch handler");
        ctx->has_compilation_errors = true;
        if (catch_reg_allocated && !catch_reg_bound) {
            compiler_free_register(ctx->allocator, catch_reg);
        }
        return;
    }

    if (try_stmt->typed.tryStmt.tryBlock) {
        compile_block_with_scope(ctx, try_stmt->typed.tryStmt.tryBlock, true);
    }

    set_location_from_node(ctx, try_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_TRY_END);

    set_location_from_node(ctx, try_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
    int end_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP);
    if (end_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate jump placeholder for try end");
        ctx->has_compilation_errors = true;
        if (catch_reg_allocated && !catch_reg_bound) {
            compiler_free_register(ctx->allocator, catch_reg);
        }
        return;
    }

    int catch_start = ctx->bytecode ? ctx->bytecode->count : 0;
    if (!patch_jump(ctx->bytecode, handler_patch, catch_start)) {
        DEBUG_CODEGEN_PRINT("Error: Failed to patch catch handler jump to %d\n", catch_start);
        ctx->has_compilation_errors = true;
        if (catch_reg_allocated && !catch_reg_bound) {
            compiler_free_register(ctx->allocator, catch_reg);
        }
        return;
    }

    SymbolTable* saved_scope = ctx->symbols;
    ScopeFrame* lexical_frame = NULL;
    int lexical_frame_index = -1;

    if (has_catch_block) {
        ctx->symbols = create_symbol_table(saved_scope);
        if (!ctx->symbols) {
            DEBUG_CODEGEN_PRINT("Error: Failed to create catch scope symbol table");
            ctx->symbols = saved_scope;
            ctx->has_compilation_errors = true;
            if (catch_reg_allocated && !catch_reg_bound) {
                compiler_free_register(ctx->allocator, catch_reg);
            }
            return;
        }

        if (ctx->allocator) {
            compiler_enter_scope(ctx->allocator);
        }

        if (ctx->scopes) {
            lexical_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
            if (lexical_frame) {
                lexical_frame->symbols = ctx->symbols;
                lexical_frame->start_offset = catch_start;
                lexical_frame->end_offset = catch_start;
                lexical_frame_index = lexical_frame->lexical_depth;
            }
        }

        if (has_catch_var) {
            if (!register_variable(ctx, ctx->symbols, try_stmt->typed.tryStmt.catchVarName,
                                   catch_reg, getPrimitiveType(TYPE_ERROR), true, true,
                                   try_stmt->original->location, true)) {
                DEBUG_CODEGEN_PRINT("Error: Failed to register catch variable '%s'",
                       try_stmt->typed.tryStmt.catchVarName);
                if (ctx->allocator) {
                    compiler_exit_scope(ctx->allocator);
                }
                free_symbol_table(ctx->symbols);
                ctx->symbols = saved_scope;
                ctx->has_compilation_errors = true;
                if (catch_reg_allocated && !catch_reg_bound) {
                    compiler_free_register(ctx->allocator, catch_reg);
                }
                if (lexical_frame && ctx->scopes) {
                    scope_stack_pop(ctx->scopes);
                }
                return;
            }
            catch_reg_bound = true;
        }

        if (try_stmt->typed.tryStmt.catchBlock) {
            compile_block_with_scope(ctx, try_stmt->typed.tryStmt.catchBlock, false);
        }

        DEBUG_CODEGEN_PRINT("Exiting catch scope");
        if (ctx->symbols) {
            for (int i = 0; i < ctx->symbols->capacity; i++) {
                Symbol* symbol = ctx->symbols->symbols[i];
                while (symbol) {
                    if (symbol->legacy_register_id >= MP_FRAME_REG_START &&
                        symbol->legacy_register_id <= MP_FRAME_REG_END) {
                        compiler_free_register(ctx->allocator, symbol->legacy_register_id);
                    }
                    symbol = symbol->next;
                }
            }
        }

        if (lexical_frame) {
            ScopeFrame* refreshed = get_scope_frame_by_index(ctx, lexical_frame_index);
            if (refreshed) {
                refreshed->end_offset = ctx->bytecode ? ctx->bytecode->count : catch_start;
            }
            if (ctx->scopes) {
                scope_stack_pop(ctx->scopes);
            }
        }

        if (ctx->allocator) {
            compiler_exit_scope(ctx->allocator);
        }

        free_symbol_table(ctx->symbols);
        ctx->symbols = saved_scope;
    } else if (catch_reg_allocated && !catch_reg_bound) {
        compiler_free_register(ctx->allocator, catch_reg);
        catch_reg_allocated = false;
    }

    if (!patch_jump(ctx->bytecode, end_patch, ctx->bytecode->count)) {
        DEBUG_CODEGEN_PRINT("Error: Failed to patch end jump for try statement");
        ctx->has_compilation_errors = true;
    }
}

// Helper function to patch all break statements to jump to end
static void patch_break_statements(CompilerContext* ctx, int end_target) {
    if (!ctx) {
        return;
    }

    for (int i = 0; i < ctx->break_count; i++) {
        int patch_index = ctx->break_statements[i];
        if (!patch_jump(ctx->bytecode, patch_index, end_target)) {
            DEBUG_CODEGEN_PRINT("Error: Failed to patch break jump (index %d) to %d\n",
                   patch_index, end_target);
            ctx->has_compilation_errors = true;
        } else {
            DEBUG_CODEGEN_PRINT("Patched break jump index %d to %d\n", patch_index, end_target);
        }
    }
    // Clear break statements for this loop
    ctx->break_count = 0;

    ScopeFrame* frame = ctx->scopes ? scope_stack_current_loop(ctx->scopes) : NULL;
    if (frame) {
        frame->loop_break_count = 0;
        update_saved_break_metadata(ctx, frame->loop_break_statements,
                                    frame->loop_break_count,
                                    frame->loop_break_capacity);
    }
}

// Helper function to patch all continue statements to jump to continue target
static void patch_continue_statements(CompilerContext* ctx, int continue_target) {
    if (!ctx) {
        return;
    }

    for (int i = 0; i < ctx->continue_count; i++) {
        int patch_index = ctx->continue_statements[i];
        if (!patch_jump(ctx->bytecode, patch_index, continue_target)) {
            DEBUG_CODEGEN_PRINT("Error: Failed to patch continue jump (index %d) to %d\n",
                   patch_index, continue_target);
            ctx->has_compilation_errors = true;
        } else {
            DEBUG_CODEGEN_PRINT("Patched continue jump index %d to %d\n", patch_index, continue_target);
        }
    }
    // Clear continue statements for this loop
    ctx->continue_count = 0;

    ScopeFrame* frame = ctx->scopes ? scope_stack_current_loop(ctx->scopes) : NULL;
    if (frame) {
        frame->loop_continue_count = 0;
        update_saved_continue_metadata(ctx, frame->loop_continue_statements,
                                       frame->loop_continue_count,
                                       frame->loop_continue_capacity);
    }
}

void compile_while_statement(CompilerContext* ctx, TypedASTNode* while_stmt) {
    if (!ctx || !while_stmt) return;

    DEBUG_CODEGEN_PRINT("Compiling while statement");

    TypedASTNode* while_body = while_stmt->typed.whileStmt.body;
    TypedASTNode* condition_node = while_stmt->typed.whileStmt.condition;
    int initial_bytecode_count = ctx->bytecode ? ctx->bytecode->count : 0;
    int initial_patch_count = ctx->bytecode ? ctx->bytecode->patch_count : 0;
    FusedCounterLoopInfo fused_info;
    if (!try_prepare_fused_counter_loop(ctx, while_stmt, &fused_info)) {
        ctx->has_compilation_errors = true;
        return;
    }

    bool use_fused_inc = fused_info.can_fuse;
    const char* fused_index_name = fused_info.loop_var_name;
    TypedASTNode* fused_limit_node = fused_info.limit_node;
    bool fused_body_is_block = fused_info.body_is_block;
    int fused_block_count = fused_info.body_statement_count;

    Symbol* fused_symbol = NULL;
    int fused_loop_reg = -1;
    int fused_limit_reg = fused_info.limit_reg;
    int fused_limit_temp_reg = fused_info.use_adjusted_limit ? fused_info.adjusted_limit_reg : fused_limit_reg;
    bool fused_limit_is_temp = fused_info.limit_reg_is_temp;
    bool fused_limit_temp_is_temp = fused_info.use_adjusted_limit ? fused_info.adjusted_limit_is_temp : fused_limit_is_temp;

    if (use_fused_inc) {
        if (!fused_index_name || !fused_limit_node) {
            use_fused_inc = false;
        }
    }

    if (use_fused_inc) {
        const Type* limit_type = get_effective_type(fused_limit_node);
        if (!limit_type || limit_type->kind != TYPE_I32) {
            use_fused_inc = false;
        }
    }

    if (use_fused_inc) {
        bool is_upvalue = false;
        int upvalue_index = -1;
        fused_symbol = resolve_symbol(ctx->symbols, fused_index_name);
        fused_loop_reg = resolve_variable_or_upvalue(ctx, fused_index_name, &is_upvalue, &upvalue_index);
        if (!fused_symbol || !fused_symbol->is_mutable || !fused_symbol->type ||
            fused_symbol->type->kind != TYPE_I32 || fused_loop_reg < 0 || is_upvalue) {
            use_fused_inc = false;
        }
    }

    if (!use_fused_inc) {
        if (ctx->bytecode) {
            ctx->bytecode->count = initial_bytecode_count;
            if (ctx->bytecode->patch_count > initial_patch_count) {
                ctx->bytecode->patch_count = initial_patch_count;
            }
        }
        if (fused_info.use_adjusted_limit && fused_info.adjusted_limit_is_temp &&
            fused_info.adjusted_limit_reg >= 0) {
            compiler_free_temp(ctx->allocator, fused_info.adjusted_limit_reg);
        }
        if (fused_limit_is_temp && fused_limit_reg >= 0) {
            compiler_free_temp(ctx->allocator, fused_limit_reg);
        }
    }

    if (use_fused_inc) {
        if (condition_node && condition_node->typed.binary.left) {
            ensure_i32_typed_register(ctx, fused_loop_reg, condition_node->typed.binary.left);
        }

        int typed_hint_loop_reg = -1;
        int typed_hint_limit_reg = -1;

        if (ctx->allocator && fused_loop_reg >= 0) {
            compiler_set_typed_residency_hint(ctx->allocator, fused_loop_reg, true);
            typed_hint_loop_reg = fused_loop_reg;
        }

        int loop_start_fused = ctx->bytecode->count;
        ScopeFrame* loop_frame_fused = enter_loop_context(ctx, loop_start_fused);
        int loop_frame_index = loop_frame_fused ? loop_frame_fused->lexical_depth : -1;
        if (!loop_frame_fused) {
            DEBUG_CODEGEN_PRINT("Error: Failed to enter loop context");
            ctx->has_compilation_errors = true;
            if (fused_info.use_adjusted_limit && fused_limit_temp_is_temp &&
                fused_limit_temp_reg >= 0) {
                compiler_free_temp(ctx->allocator, fused_limit_temp_reg);
            }
            if (fused_limit_is_temp && fused_limit_reg >= 0) {
                compiler_free_temp(ctx->allocator, fused_limit_reg);
            }
            release_typed_hint(ctx, &typed_hint_loop_reg);
            release_typed_hint(ctx, &typed_hint_limit_reg);
            return;
        }

        if (while_stmt->original && while_stmt->original->type == NODE_WHILE) {
            loop_frame_fused->label = while_stmt->original->whileStmt.label;
        }

        DEBUG_CODEGEN_PRINT("While loop start at offset %d\n", loop_start_fused);

        int fused_limit_guard_reg = fused_info.use_adjusted_limit ? fused_limit_temp_reg : fused_limit_reg;
        if (fused_limit_guard_reg >= 0) {
            bool guard_already_primed = false;
            if (fused_info.use_adjusted_limit && fused_limit_guard_reg == fused_limit_temp_reg) {
                guard_already_primed = fused_info.adjusted_limit_is_primed;
            } else if (fused_limit_guard_reg == fused_limit_reg) {
                guard_already_primed = fused_info.limit_reg_is_primed;
            }
            if (!guard_already_primed) {
                ensure_i32_typed_register(ctx, fused_limit_guard_reg,
                                          fused_info.use_adjusted_limit ? fused_limit_node : fused_limit_node);
                if (fused_info.use_adjusted_limit && fused_limit_guard_reg == fused_limit_temp_reg) {
                    fused_info.adjusted_limit_is_primed = true;
                } else if (fused_limit_guard_reg == fused_limit_reg) {
                    fused_info.limit_reg_is_primed = true;
                }
            }
        }
        if (ctx->allocator && fused_limit_guard_reg >= 0) {
            compiler_set_typed_residency_hint(ctx->allocator, fused_limit_guard_reg, true);
            typed_hint_limit_reg = fused_limit_guard_reg;
        }

        set_location_from_node(ctx, while_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_I32_TYPED);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)fused_loop_reg);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)(fused_limit_temp_is_temp ? fused_limit_temp_reg : fused_limit_reg));
        int end_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_I32_TYPED);
        if (end_patch < 0) {
            DEBUG_CODEGEN_PRINT("Error: Failed to allocate while-loop end placeholder\n");
            ctx->has_compilation_errors = true;
            leave_loop_context(ctx, loop_frame_fused, ctx->bytecode->count);
            if (fused_info.use_adjusted_limit && fused_limit_temp_is_temp &&
                fused_limit_temp_reg >= 0) {
                compiler_free_temp(ctx->allocator, fused_limit_temp_reg);
            }
            if (fused_limit_is_temp && fused_limit_reg >= 0) {
                compiler_free_temp(ctx->allocator, fused_limit_reg);
            }
            release_typed_hint(ctx, &typed_hint_loop_reg);
            release_typed_hint(ctx, &typed_hint_limit_reg);
            return;
        }
        if (fused_body_is_block && fused_block_count > 0 && while_body && while_body->original &&
            while_body->original->type == NODE_BLOCK) {
            int limit = fused_block_count - 1;
            if (limit < 0) {
                limit = 0;
            }
            for (int i = 0; i < limit; i++) {
                TypedASTNode* stmt = while_body->typed.block.statements[i];
                if (stmt) {
                    compile_statement(ctx, stmt);
                }
            }
        } else if (!fused_body_is_block && fused_info.has_increment == false && while_body) {
            // Non-block bodies without an increment should still be compiled normally.
            compile_statement(ctx, while_body);
        }

        if (loop_frame_index >= 0) {
            loop_frame_fused = get_scope_frame_by_index(ctx, loop_frame_index);
        }

        set_location_from_node(ctx, while_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_INC_CMP_JMP);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)fused_loop_reg);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)(fused_limit_temp_is_temp ? fused_limit_temp_reg : fused_limit_reg));
        int back_off = loop_start_fused - (ctx->bytecode->count + 2);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)(back_off & 0xFF));
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)((back_off >> 8) & 0xFF));

        int end_target = ctx->bytecode->count;
        ctx->current_loop_end = end_target;
        if (loop_frame_fused) {
            loop_frame_fused->end_offset = end_target;
        }
        patch_break_statements(ctx, end_target);

        if (!patch_jump(ctx->bytecode, end_patch, end_target)) {
            DEBUG_CODEGEN_PRINT("Error: Failed to patch while-loop end jump to %d\n", end_target);
            ctx->has_compilation_errors = true;
            leave_loop_context(ctx, loop_frame_fused, end_target);
            if (fused_limit_temp_is_temp) {
                compiler_free_temp(ctx->allocator, fused_limit_temp_reg);
            }
            if (fused_limit_is_temp) {
                compiler_free_temp(ctx->allocator, fused_limit_reg);
            }
            release_typed_hint(ctx, &typed_hint_loop_reg);
            release_typed_hint(ctx, &typed_hint_limit_reg);
            return;
        }
        DEBUG_CODEGEN_PRINT("Patched end jump to %d\n", end_target);

        leave_loop_context(ctx, loop_frame_fused, end_target);

        release_typed_hint(ctx, &typed_hint_loop_reg);
        release_typed_hint(ctx, &typed_hint_limit_reg);
        if (fused_info.use_adjusted_limit && fused_limit_temp_is_temp &&
            fused_limit_temp_reg >= 0) {
            compiler_free_temp(ctx->allocator, fused_limit_temp_reg);
        }
        if (fused_limit_is_temp && fused_limit_reg >= 0) {
            compiler_free_temp(ctx->allocator, fused_limit_reg);
        }

        if (fused_symbol) {
            mark_symbol_as_loop_variable(fused_symbol);
            mark_symbol_arithmetic_heavy(fused_symbol);
        }

        DEBUG_CODEGEN_PRINT("While statement compilation completed (fused inc path)");
        return;
    }

    int loop_start = ctx->bytecode->count;
    ScopeFrame* loop_frame = enter_loop_context(ctx, loop_start);
    int loop_frame_index = loop_frame ? loop_frame->lexical_depth : -1;
    if (!loop_frame) {
        DEBUG_CODEGEN_PRINT("Error: Failed to enter loop context");
        ctx->has_compilation_errors = true;
        return;
    }

    if (while_stmt->original && while_stmt->original->type == NODE_WHILE) {
        loop_frame->label = while_stmt->original->whileStmt.label;
    }

    uint16_t loop_id = ctx->current_loop_id;

    DEBUG_CODEGEN_PRINT("While loop start at offset %d (loop_id=%u)\n", loop_start, loop_id);

    int condition_reg = compile_expression(ctx, while_stmt->typed.whileStmt.condition);
    if (condition_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to compile while condition");
        ctx->has_compilation_errors = true;
        leave_loop_context(ctx, loop_frame, loop_start);
        return;
    }

    set_location_from_node(ctx, while_stmt);
    int end_patch = -1;
    emit_byte_to_buffer(ctx->bytecode, OP_BRANCH_TYPED);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)((loop_id >> 8) & 0xFF));
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)(loop_id & 0xFF));
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)condition_reg);
    end_patch = emit_jump_placeholder(ctx->bytecode, OP_BRANCH_TYPED);
    if (end_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate while-loop end placeholder\n");
        ctx->has_compilation_errors = true;
        if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, condition_reg);
        }
        leave_loop_context(ctx, loop_frame, ctx->bytecode->count);
        return;
    }
    DEBUG_CODEGEN_PRINT("Emitted OP_BRANCH_TYPED loop=%u R%d (placeholder index %d)\n",
           loop_id, condition_reg, end_patch);

    if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, condition_reg);
    }

    compile_block_with_scope(ctx, while_body, false);

    if (loop_frame_index >= 0) {
        loop_frame = get_scope_frame_by_index(ctx, loop_frame_index);
    }

    int back_jump_distance = (ctx->bytecode->count + 2) - loop_start;
    if (back_jump_distance >= 0 && back_jump_distance <= 255) {
        set_location_from_node(ctx, while_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_LOOP_SHORT);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)back_jump_distance);
        DEBUG_CODEGEN_PRINT("Emitted OP_LOOP_SHORT with offset %d (back to start)\n", back_jump_distance);
    } else {
        int back_jump_offset = loop_start - (ctx->bytecode->count + 3);
        set_location_from_node(ctx, while_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
        emit_byte_to_buffer(ctx->bytecode, (back_jump_offset >> 8) & 0xFF);
        emit_byte_to_buffer(ctx->bytecode, back_jump_offset & 0xFF);
        DEBUG_CODEGEN_PRINT("Emitted OP_JUMP with offset %d (back to start)\n", back_jump_offset);
    }

    int end_target = ctx->bytecode->count;
    ctx->current_loop_end = end_target;
    if (loop_frame) {
        loop_frame->end_offset = end_target;
    }

    patch_break_statements(ctx, end_target);

    if (!patch_jump(ctx->bytecode, end_patch, end_target)) {
        DEBUG_CODEGEN_PRINT("Error: Failed to patch while-loop end jump to %d\n", end_target);
        ctx->has_compilation_errors = true;
        leave_loop_context(ctx, loop_frame, end_target);
        return;
    }
    DEBUG_CODEGEN_PRINT("Patched end jump to %d\n", end_target);

    leave_loop_context(ctx, loop_frame, end_target);
    DEBUG_CODEGEN_PRINT("While statement compilation completed");
}


void compile_for_range_statement(CompilerContext* ctx, TypedASTNode* for_stmt) {
    if (!ctx || !for_stmt) {
        return;
    }

    DEBUG_CODEGEN_PRINT("Compiling for range statement");

    SymbolTable* old_scope = ctx->symbols;
    bool created_scope = false;
    ctx->symbols = create_symbol_table(old_scope);
    if (!ctx->symbols) {
        ctx->symbols = old_scope;
        ctx->has_compilation_errors = true;
        return;
    }
    created_scope = true;

    if (ctx->allocator) {
        compiler_enter_scope(ctx->allocator);
    }

    ScopeFrame* scope_frame = NULL;
    int scope_frame_index = -1;
    if (ctx->scopes) {
        scope_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
        if (scope_frame) {
            scope_frame->symbols = ctx->symbols;
            scope_frame->start_offset = ctx->bytecode ? ctx->bytecode->count : 0;
            scope_frame->end_offset = scope_frame->start_offset;
            scope_frame_index = scope_frame->lexical_depth;
        }
    }

    ScopeFrame* loop_frame = NULL;
    int loop_frame_index = -1;
    bool success = false;

    int start_reg = -1;
    int end_reg = -1;
    int step_reg = -1;
    int loop_var_reg = -1;
    int condition_reg = -1;
    int condition_neg_reg = -1;
    int step_nonneg_reg = -1;
    int zero_reg = -1;
    int limit_temp_reg = -1; // temp for inclusive fused limit (end+1)
    int typed_hint_loop_reg = -1;
    int typed_hint_limit_reg = -1;
    bool end_reg_is_temp = false;
    bool step_reg_was_temp = false;
    bool limit_temp_reg_is_temp = false;

    const char* loop_var_name = NULL;
    if (for_stmt->original && for_stmt->original->forRange.varName) {
        loop_var_name = for_stmt->original->forRange.varName;
    } else if (for_stmt->typed.forRange.varName) {
        loop_var_name = for_stmt->typed.forRange.varName;
    }

    if (!loop_var_name) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    TypedASTNode* start_node = for_stmt->typed.forRange.start;
    TypedASTNode* end_node = for_stmt->typed.forRange.end;

    if (!start_node || !end_node) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    start_reg = compile_expression(ctx, start_node);
    if (start_reg == -1) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    FusedCounterLoopInfo fused_info;
    if (!try_prepare_fused_counter_loop(ctx, for_stmt, &fused_info)) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    if (fused_info.loop_var_name) {
        loop_var_name = fused_info.loop_var_name;
    }

    end_reg = fused_info.limit_reg;
    end_reg_is_temp = fused_info.limit_reg_is_temp;
    if (end_reg < 0) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    step_reg = fused_info.step_reg;
    step_reg_was_temp = fused_info.step_reg_is_temp;
    if (step_reg < 0) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    bool step_known_positive = fused_info.step_known_positive;
    bool step_known_negative = fused_info.step_known_negative;

    limit_temp_reg = fused_info.use_adjusted_limit ? fused_info.adjusted_limit_reg : -1;
    limit_temp_reg_is_temp = fused_info.use_adjusted_limit ? fused_info.adjusted_limit_is_temp : false;
    bool can_fuse_inc_cmp = fused_info.can_fuse;

    if (!step_known_positive && !step_known_negative) {
        zero_reg = compiler_alloc_temp(ctx->allocator);
        if (zero_reg == -1) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }
        set_location_from_node(ctx, for_stmt);
        emit_load_constant(ctx, zero_reg, I32_VAL(0));

        step_nonneg_reg = compiler_alloc_temp(ctx->allocator);
        if (step_nonneg_reg == -1) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }
        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_GE_I32_R);
        emit_byte_to_buffer(ctx->bytecode, step_nonneg_reg);
        emit_byte_to_buffer(ctx->bytecode, step_reg);
        emit_byte_to_buffer(ctx->bytecode, zero_reg);

        if (zero_reg >= MP_TEMP_REG_START && zero_reg <= MP_TEMP_REG_END) {
            compiler_free_temp(ctx->allocator, zero_reg);
        }
        zero_reg = -1;
    }

    loop_var_reg = compiler_alloc_frame(ctx->allocator);
    if (loop_var_reg == -1) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    Symbol* loop_symbol = register_variable(ctx, ctx->symbols, loop_var_name,
                                            loop_var_reg,
                                            getPrimitiveType(TYPE_I32), true, true,
                                            for_stmt->original->location, true);
    if (!loop_symbol) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    if (ctx->allocator) {
        compiler_set_typed_residency_hint(ctx->allocator, loop_var_reg, true);
        typed_hint_loop_reg = loop_var_reg;
    }
    mark_symbol_as_loop_variable(loop_symbol);
    mark_symbol_arithmetic_heavy(loop_symbol);

    set_location_from_node(ctx, for_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_MOVE_I32);
    emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
    emit_byte_to_buffer(ctx->bytecode, start_reg);

    if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, start_reg);
        start_reg = -1;
    }

    int loop_start = ctx->bytecode ? ctx->bytecode->count : 0;
    loop_frame = enter_loop_context(ctx, loop_start);
    if (!loop_frame) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    loop_frame_index = loop_frame->lexical_depth;
    if (loop_frame) {
        loop_frame->label = for_stmt->typed.forRange.label;
    }
    uint16_t loop_id = ctx->current_loop_id;
    (void)loop_id;
    ctx->current_loop_continue = -1;
    loop_frame->continue_offset = -1;

    condition_reg = compiler_alloc_temp(ctx->allocator);
    if (condition_reg == -1) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    // If we can use fused INC_CMP_JMP, prefer the precomputed adjusted limit when available
    int limit_reg_used = end_reg;
    if (can_fuse_inc_cmp && limit_temp_reg >= 0) {
        limit_reg_used = limit_temp_reg;
    }

    if (can_fuse_inc_cmp && limit_reg_used >= 0) {
        bool guard_already_primed = false;
        if (fused_info.use_adjusted_limit && limit_reg_used == limit_temp_reg) {
            guard_already_primed = fused_info.adjusted_limit_is_primed;
        } else if (limit_reg_used == fused_info.limit_reg) {
            guard_already_primed = fused_info.limit_reg_is_primed;
        }
        if (!guard_already_primed) {
            ensure_i32_typed_register(ctx, limit_reg_used, fused_info.limit_node);
            if (fused_info.use_adjusted_limit && limit_reg_used == limit_temp_reg) {
                fused_info.adjusted_limit_is_primed = true;
            } else if (limit_reg_used == fused_info.limit_reg) {
                fused_info.limit_reg_is_primed = true;
            }
        }
    }

    if (ctx->allocator && limit_reg_used >= 0) {
        compiler_set_typed_residency_hint(ctx->allocator, limit_reg_used, true);
        typed_hint_limit_reg = limit_reg_used;
    }

    int guard_patch = -1;
    set_location_from_node(ctx, for_stmt);
    if (can_fuse_inc_cmp) {
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_I32_TYPED);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)limit_reg_used);
        guard_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_I32_TYPED);
        if (guard_patch < 0) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }
    } else {
        if (for_stmt->typed.forRange.inclusive) {
            emit_byte_to_buffer(ctx->bytecode, OP_LE_I32_TYPED);
        } else {
            emit_byte_to_buffer(ctx->bytecode, OP_LT_I32_TYPED);
        }
        emit_byte_to_buffer(ctx->bytecode, condition_reg);
        emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)end_reg);
    }

    if (!step_known_positive) {
        condition_neg_reg = compiler_alloc_temp(ctx->allocator);
        if (condition_neg_reg == -1) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        set_location_from_node(ctx, for_stmt);
        if (for_stmt->typed.forRange.inclusive) {
            emit_byte_to_buffer(ctx->bytecode, OP_GE_I32_TYPED);
        } else {
            emit_byte_to_buffer(ctx->bytecode, OP_GT_I32_TYPED);
        }
        emit_byte_to_buffer(ctx->bytecode, condition_neg_reg);
        emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, end_reg);
    }

    int select_neg_patch = -1;
    int skip_neg_patch = -1;

    if (step_known_negative) {
        set_location_from_node(ctx, for_stmt);
        emit_move(ctx, condition_reg, condition_neg_reg);
    } else if (!step_known_positive) {
        if (step_nonneg_reg == -1) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP_IF_NOT_R);
        emit_byte_to_buffer(ctx->bytecode, step_nonneg_reg);
        select_neg_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP_IF_NOT_R);
        if (select_neg_patch < 0) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
        skip_neg_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP);
        if (skip_neg_patch < 0) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        if (!patch_jump(ctx->bytecode, select_neg_patch, ctx->bytecode->count)) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }

        set_location_from_node(ctx, for_stmt);
        emit_move(ctx, condition_reg, condition_neg_reg);

        if (!patch_jump(ctx->bytecode, skip_neg_patch, ctx->bytecode->count)) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }
    }

    int end_patch = -1;
    if (can_fuse_inc_cmp) {
        end_patch = guard_patch;
    } else {
        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_BRANCH_TYPED);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)((loop_id >> 8) & 0xFF));
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)(loop_id & 0xFF));
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)condition_reg);
        end_patch = emit_jump_placeholder(ctx->bytecode, OP_BRANCH_TYPED);
        if (end_patch < 0) {
            ctx->has_compilation_errors = true;
            goto cleanup;
        }
    }

    compile_block_with_scope(ctx, for_stmt->typed.forRange.body, true);

    if (loop_frame_index >= 0) {
        loop_frame = get_scope_frame_by_index(ctx, loop_frame_index);
    }

    int continue_target = ctx->bytecode->count;
    update_loop_continue_target(ctx, loop_frame, continue_target);

    if (can_fuse_inc_cmp) {
        // Continue statements should jump here to execute fused inc+cmp+jmp
        patch_continue_statements(ctx, continue_target);

        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_INC_CMP_JMP);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)limit_reg_used);
        // back offset is relative to address after the 2-byte offset we emit now
        int back_off = loop_start - (ctx->bytecode->count + 2);
        // OP_INC_CMP_JMP reads offset as native int16 (little-endian on our targets)
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)(back_off & 0xFF));
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)((back_off >> 8) & 0xFF));
    } else {
        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_ADD_I32_TYPED);
        emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
        emit_byte_to_buffer(ctx->bytecode, step_reg);

        patch_continue_statements(ctx, continue_target);

        int back_jump_distance = (ctx->bytecode->count + 2) - loop_start;
        if (back_jump_distance >= 0 && back_jump_distance <= 255) {
            set_location_from_node(ctx, for_stmt);
            emit_byte_to_buffer(ctx->bytecode, OP_LOOP_SHORT);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)back_jump_distance);
        } else {
            int back_jump_offset = loop_start - (ctx->bytecode->count + 3);
            set_location_from_node(ctx, for_stmt);
            emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
            emit_byte_to_buffer(ctx->bytecode, (back_jump_offset >> 8) & 0xFF);
            emit_byte_to_buffer(ctx->bytecode, back_jump_offset & 0xFF);
        }
    }

    int end_target = ctx->bytecode->count;
    ctx->current_loop_end = end_target;

    if (!patch_jump(ctx->bytecode, end_patch, end_target)) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    patch_break_statements(ctx, end_target);

    leave_loop_context(ctx, loop_frame, end_target);
    loop_frame = NULL;
    loop_frame_index = -1;
    success = true;

cleanup:
    if (loop_frame) {
        ScopeFrame* refreshed = get_scope_frame_by_index(ctx, loop_frame_index);
        leave_loop_context(ctx, refreshed,
                           ctx->bytecode ? ctx->bytecode->count : 0);
        loop_frame = NULL;
        loop_frame_index = -1;
    }

    release_typed_hint(ctx, &typed_hint_loop_reg);
    release_typed_hint(ctx, &typed_hint_limit_reg);

    if (condition_reg >= MP_TEMP_REG_START && condition_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, condition_reg);
        condition_reg = -1;
    }
    if (condition_neg_reg >= MP_TEMP_REG_START && condition_neg_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, condition_neg_reg);
        condition_neg_reg = -1;
    }
    if (step_nonneg_reg >= MP_TEMP_REG_START && step_nonneg_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, step_nonneg_reg);
        step_nonneg_reg = -1;
    }
    if (zero_reg >= MP_TEMP_REG_START && zero_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, zero_reg);
        zero_reg = -1;
    }
    if (start_reg >= MP_TEMP_REG_START && start_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, start_reg);
        start_reg = -1;
    }
    if (end_reg_is_temp && end_reg >= 0) {
        compiler_free_temp(ctx->allocator, end_reg);
        end_reg = -1;
    }
    if (limit_temp_reg_is_temp && limit_temp_reg >= 0) {
        compiler_free_temp(ctx->allocator, limit_temp_reg);
        limit_temp_reg = -1;
    }
    if (step_reg_was_temp && step_reg >= 0) {
        compiler_free_temp(ctx->allocator, step_reg);
        step_reg = -1;
    }

    if (created_scope && ctx->symbols) {
        for (int i = 0; i < ctx->symbols->capacity; i++) {
            Symbol* symbol = ctx->symbols->symbols[i];
            while (symbol) {
                if (symbol->legacy_register_id >= MP_FRAME_REG_START &&
                    symbol->legacy_register_id <= MP_FRAME_REG_END) {
                    compiler_free_register(ctx->allocator, symbol->legacy_register_id);
                }
                symbol = symbol->next;
            }
        }
    }

    if (scope_frame) {
        ScopeFrame* refreshed = get_scope_frame_by_index(ctx, scope_frame_index);
        if (refreshed) {
            refreshed->end_offset = ctx->bytecode ? ctx->bytecode->count : refreshed->start_offset;
        }
        if (ctx->scopes) {
            scope_stack_pop(ctx->scopes);
        }
        scope_frame = NULL;
        scope_frame_index = -1;
    }

    if (created_scope && ctx->allocator) {
        compiler_exit_scope(ctx->allocator);
    }

    if (created_scope && ctx->symbols) {
        free_symbol_table(ctx->symbols);
    }
    if (created_scope) {
        ctx->symbols = old_scope;
    }

    if (success) {
        DEBUG_CODEGEN_PRINT("For range statement compilation completed");
    } else {
        DEBUG_CODEGEN_PRINT("For range statement aborted");
    }
}

void compile_for_iter_statement(CompilerContext* ctx, TypedASTNode* for_stmt) {
    if (!ctx || !for_stmt) return;

    DEBUG_CODEGEN_PRINT("Compiling for iteration statement");

    ScopeFrame* loop_frame = NULL;
    int loop_frame_index = -1;
    bool success = false;
    int iterable_reg = -1;
    int iter_reg = -1;
    int loop_var_reg = -1;
    int has_value_reg = -1;
    int typed_hint_iter_reg = -1;
    int typed_hint_loop_reg = -1;

    // Compile iterable expression
    iterable_reg = compile_expression(ctx, for_stmt->typed.forIter.iterable);
    if (iterable_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to compile iterable expression");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    // Allocate iterator register
    iter_reg = compiler_alloc_temp(ctx->allocator);
    if (iter_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate iterator register");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    if (ctx->allocator) {
        compiler_set_typed_residency_hint(ctx->allocator, iter_reg, true);
        typed_hint_iter_reg = iter_reg;
    }

    // Get iterator from iterable
    set_location_from_node(ctx, for_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_GET_ITER_R);
    emit_byte_to_buffer(ctx->bytecode, iter_reg);
    emit_byte_to_buffer(ctx->bytecode, iterable_reg);
    
    // Allocate loop variable register and store in symbol table
    loop_var_reg = compiler_alloc_frame(ctx->allocator);
    if (loop_var_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate loop variable register");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    
    // Register the loop variable in symbol table (loop variables are implicitly mutable)
    Symbol* loop_symbol = register_variable(ctx, ctx->symbols,
                                            for_stmt->typed.forIter.varName,
                                            loop_var_reg,
                                            getPrimitiveType(TYPE_I32), true, true,
                                            for_stmt->original->location, true);
    if (!loop_symbol) {
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    if (ctx->allocator) {
        compiler_set_typed_residency_hint(ctx->allocator, loop_var_reg, true);
        typed_hint_loop_reg = loop_var_reg;
    }

    mark_symbol_as_loop_variable(loop_symbol);
    
    // Allocate has_value register for iterator status
    has_value_reg = compiler_alloc_temp(ctx->allocator);
    if (has_value_reg == -1) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate has_value register");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    
    // Set up loop labels
    int loop_start = ctx->bytecode->count;
    loop_frame = enter_loop_context(ctx, loop_start);
    if (!loop_frame) {
        DEBUG_CODEGEN_PRINT("Error: Failed to enter for-iter loop context");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    loop_frame_index = loop_frame->lexical_depth;
    uint16_t loop_id = ctx->current_loop_id;

    DEBUG_CODEGEN_PRINT("For iteration loop start at offset %d (loop_id=%u)\n", loop_start, loop_id);

    if (loop_frame) {
        loop_frame->label = for_stmt->typed.forIter.label;
    }

    // Get next value from iterator
    set_location_from_node(ctx, for_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_ITER_NEXT_R);
    emit_byte_to_buffer(ctx->bytecode, loop_var_reg);
    emit_byte_to_buffer(ctx->bytecode, iter_reg);
    emit_byte_to_buffer(ctx->bytecode, has_value_reg);

    // Emit conditional jump - if has_value is false, jump to end of loop
    set_location_from_node(ctx, for_stmt);
    int end_patch = -1;
    emit_byte_to_buffer(ctx->bytecode, OP_BRANCH_TYPED);
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)((loop_id >> 8) & 0xFF));
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)(loop_id & 0xFF));
    emit_byte_to_buffer(ctx->bytecode, (uint8_t)has_value_reg);
    end_patch = emit_jump_placeholder(ctx->bytecode, OP_BRANCH_TYPED);
    if (end_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate iterator loop end placeholder\n");
        ctx->has_compilation_errors = true;
        goto cleanup;
    }

    DEBUG_CODEGEN_PRINT("Emitted OP_BRANCH_TYPED loop=%u R%d (placeholder index %d)\n",
           loop_id, has_value_reg, end_patch);
    
    // Compile loop body with scope (like while loops do)
    compile_block_with_scope(ctx, for_stmt->typed.forIter.body, true);

    if (loop_frame_index >= 0) {
        loop_frame = get_scope_frame_by_index(ctx, loop_frame_index);
    }
    
    // Emit unconditional jump back to loop start
    int back_jump_distance = (ctx->bytecode->count + 2) - loop_start;
    if (back_jump_distance >= 0 && back_jump_distance <= 255) {
        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_LOOP_SHORT);
        emit_byte_to_buffer(ctx->bytecode, (uint8_t)back_jump_distance);
        DEBUG_CODEGEN_PRINT("Emitted OP_LOOP_SHORT with offset %d (back to start)\n", back_jump_distance);
    } else {
        int back_jump_offset = loop_start - (ctx->bytecode->count + 3);
        set_location_from_node(ctx, for_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
        emit_byte_to_buffer(ctx->bytecode, (back_jump_offset >> 8) & 0xFF);
        emit_byte_to_buffer(ctx->bytecode, back_jump_offset & 0xFF);
        DEBUG_CODEGEN_PRINT("Emitted OP_JUMP with offset %d (back to start)\n", back_jump_offset);
    }
    
    // Patch the conditional jump to current position  
    int end_target = ctx->bytecode->count;
    ctx->current_loop_end = end_target;
    
    if (!patch_jump(ctx->bytecode, end_patch, end_target)) {
        DEBUG_CODEGEN_PRINT("Error: Failed to patch iterator loop end jump to %d\n", end_target);
        ctx->has_compilation_errors = true;
        goto cleanup;
    }
    DEBUG_CODEGEN_PRINT("Patched conditional jump to %d\n", end_target);

    // Patch all break statements to jump to end of loop (do this LAST)
    patch_break_statements(ctx, end_target);

    leave_loop_context(ctx, loop_frame, end_target);
    loop_frame = NULL;
    loop_frame_index = -1;
    success = true;

cleanup:
    if (loop_frame) {
        ScopeFrame* refreshed = get_scope_frame_by_index(ctx, loop_frame_index);
        leave_loop_context(ctx, refreshed,
                           ctx->bytecode ? ctx->bytecode->count : loop_start);
        loop_frame = NULL;
        loop_frame_index = -1;
    }

    release_typed_hint(ctx, &typed_hint_iter_reg);
    release_typed_hint(ctx, &typed_hint_loop_reg);

    if (iterable_reg >= MP_TEMP_REG_START && iterable_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, iterable_reg);
        iterable_reg = -1;
    }
    if (iter_reg >= MP_TEMP_REG_START && iter_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, iter_reg);
        iter_reg = -1;
    }
    if (has_value_reg >= MP_TEMP_REG_START && has_value_reg <= MP_TEMP_REG_END) {
        compiler_free_temp(ctx->allocator, has_value_reg);
        has_value_reg = -1;
    }

    if (loop_var_reg >= MP_FRAME_REG_START && loop_var_reg <= MP_FRAME_REG_END) {
        compiler_free_register(ctx->allocator, loop_var_reg);
        loop_var_reg = -1;
    }

    if (success) {
        DEBUG_CODEGEN_PRINT("For iteration statement compilation completed");
    } else {
        DEBUG_CODEGEN_PRINT("For iteration statement aborted");
    }
}

void compile_break_statement(CompilerContext* ctx, TypedASTNode* break_stmt) {
    if (!ctx || !break_stmt) return;
    
    DEBUG_CODEGEN_PRINT("Compiling break statement");

    // Check if we're inside a loop (current_loop_end != -1 means we're in a loop)
    if (ctx->current_loop_end == -1) {
        DEBUG_CODEGEN_PRINT("Error: break statement outside of loop");
        ctx->has_compilation_errors = true;
        SrcLocation location = break_stmt->original ? break_stmt->original->location : (SrcLocation){NULL, 0, 0};
        record_control_flow_error(ctx,
                                  E1401_BREAK_OUTSIDE_LOOP,
                                  location,
                                  "'break' can only be used inside a loop",
                                  "Move this 'break' into a loop body such as while or for.");
        report_break_outside_loop(location);
        return;
    }
    
    const char* label = NULL;
    if (break_stmt->original && break_stmt->original->type == NODE_BREAK) {
        label = break_stmt->original->breakStmt.label;
    }

    ScopeFrame* target_frame = NULL;
    if (label && ctx->scopes) {
        target_frame = scope_stack_find_loop_by_label(ctx->scopes, label);
        if (!target_frame) {
            DEBUG_CODEGEN_PRINT("Error: labeled break target '%s' not found\n", label);
            ctx->has_compilation_errors = true;
            SrcLocation location = break_stmt->original ? break_stmt->original->location : (SrcLocation){NULL, 0, 0};
            report_labeled_break_not_found(location, label);
            return;
        }
    } else {
        target_frame = ctx->scopes ? scope_stack_current_loop(ctx->scopes) : NULL;
    }

    if (!target_frame) {
        DEBUG_CODEGEN_PRINT("Error: Unable to resolve break target frame\n");
        ctx->has_compilation_errors = true;
        return;
    }

    // Emit a break jump and track it for later patching
    // OP_JUMP format: opcode + 2-byte offset (3 bytes total)
    set_location_from_node(ctx, break_stmt);
    emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
    int break_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP);
    if (break_patch < 0) {
        DEBUG_CODEGEN_PRINT("Error: Failed to allocate break jump placeholder\n");
        ctx->has_compilation_errors = true;
        return;
    }

    add_break_statement_to_frame(ctx, target_frame, break_patch);
    if (label) {
        DEBUG_CODEGEN_PRINT("Emitted OP_JUMP for labeled break '%s' (placeholder index %d)\n",
               label, break_patch);
    } else {
        DEBUG_CODEGEN_PRINT("Emitted OP_JUMP for break statement (placeholder index %d)\n", break_patch);
    }

    DEBUG_CODEGEN_PRINT("Break statement compilation completed");
}

void compile_continue_statement(CompilerContext* ctx, TypedASTNode* continue_stmt) {
    if (!ctx || !continue_stmt) return;
    
    DEBUG_CODEGEN_PRINT("Compiling continue statement");
    
    // Check if we're inside a loop
    if (ctx->current_loop_start == -1) {
        DEBUG_CODEGEN_PRINT("Error: continue statement outside of loop");
        ctx->has_compilation_errors = true;
        SrcLocation location = continue_stmt->original ? continue_stmt->original->location : (SrcLocation){NULL, 0, 0};
        record_control_flow_error(ctx,
                                  E1402_CONTINUE_OUTSIDE_LOOP,
                                  location,
                                  "'continue' can only be used inside a loop",
                                  "Move this 'continue' into a loop body such as while or for.");
        report_continue_outside_loop(location);
        return;
    }
    
    const char* label = NULL;
    if (continue_stmt->original && continue_stmt->original->type == NODE_CONTINUE) {
        label = continue_stmt->original->continueStmt.label;
    }

    ScopeFrame* target_frame = NULL;
    if (label && ctx->scopes) {
        target_frame = scope_stack_find_loop_by_label(ctx->scopes, label);
        if (!target_frame) {
            DEBUG_CODEGEN_PRINT("Error: labeled continue target '%s' not found\n", label);
            ctx->has_compilation_errors = true;
            SrcLocation location = continue_stmt->original ? continue_stmt->original->location : (SrcLocation){NULL, 0, 0};
            report_labeled_continue_not_found(location, label);
            return;
        }
    } else {
        target_frame = ctx->scopes ? scope_stack_current_loop(ctx->scopes) : NULL;
    }

    if (!target_frame) {
        DEBUG_CODEGEN_PRINT("Error: Unable to resolve continue target frame\n");
        ctx->has_compilation_errors = true;
        return;
    }

    bool use_patch = true;
    if (target_frame->continue_offset >= 0 && target_frame->continue_offset == target_frame->start_offset) {
        use_patch = false;
    }

    if (use_patch) {
        DEBUG_CODEGEN_PRINT("Continue statement using patching system%s\n",
               label ? " (labeled)" : "");
        set_location_from_node(ctx, continue_stmt);
        emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
        int continue_patch = emit_jump_placeholder(ctx->bytecode, OP_JUMP);
        if (continue_patch < 0) {
            DEBUG_CODEGEN_PRINT("Error: Failed to allocate continue jump placeholder\n");
            ctx->has_compilation_errors = true;
            return;
        }
        add_continue_statement_to_frame(ctx, target_frame, continue_patch);
        if (label) {
            DEBUG_CODEGEN_PRINT("Emitted OP_JUMP for labeled continue '%s' (placeholder index %d)\n",
                   label, continue_patch);
        } else {
            DEBUG_CODEGEN_PRINT("Emitted OP_JUMP for continue statement (placeholder index %d)\n",
                   continue_patch);
        }
    } else {
        DEBUG_CODEGEN_PRINT("Continue targeting loop start%s\n", label ? " (labeled)" : "");
        int continue_target = target_frame->start_offset;
        int back_jump_distance = (ctx->bytecode->count + 2) - continue_target;

        if (back_jump_distance >= 0 && back_jump_distance <= 255) {
            set_location_from_node(ctx, continue_stmt);
            emit_byte_to_buffer(ctx->bytecode, OP_LOOP_SHORT);
            emit_byte_to_buffer(ctx->bytecode, (uint8_t)back_jump_distance);
            DEBUG_CODEGEN_PRINT("Emitted OP_LOOP_SHORT for continue with distance %d\n", back_jump_distance);
        } else {
            int back_jump_offset = continue_target - (ctx->bytecode->count + 3);
            set_location_from_node(ctx, continue_stmt);
            emit_byte_to_buffer(ctx->bytecode, OP_JUMP);
            emit_byte_to_buffer(ctx->bytecode, (back_jump_offset >> 8) & 0xFF);
            emit_byte_to_buffer(ctx->bytecode, back_jump_offset & 0xFF);
            DEBUG_CODEGEN_PRINT("Emitted OP_JUMP for continue with offset %d\n", back_jump_offset);
        }
    }

    DEBUG_CODEGEN_PRINT("Continue statement compilation completed");
}

void compile_block_with_scope(CompilerContext* ctx, TypedASTNode* block, bool create_scope) {
    if (!ctx || !block) return;

    SymbolTable* old_scope = ctx->symbols;
    ScopeFrame* lexical_frame = NULL;
    int lexical_frame_index = -1;
    if (create_scope) {
        DEBUG_CODEGEN_PRINT("Entering new scope (depth %d)\n", ctx->symbols->scope_depth + 1);

        ctx->symbols = create_symbol_table(old_scope);
        if (!ctx->symbols) {
            DEBUG_CODEGEN_PRINT("Error: Failed to create new scope");
            ctx->symbols = old_scope;
            return;
        }

        if (ctx->allocator) {
            compiler_enter_scope(ctx->allocator);
        }

        if (ctx->scopes) {
            lexical_frame = scope_stack_push(ctx->scopes, SCOPE_KIND_LEXICAL);
            if (lexical_frame) {
                lexical_frame->symbols = ctx->symbols;
                lexical_frame->start_offset = ctx->bytecode ? ctx->bytecode->count : 0;
                lexical_frame->end_offset = lexical_frame->start_offset;
                lexical_frame_index = lexical_frame->lexical_depth;
            }
        }
    } else {
        DEBUG_CODEGEN_PRINT("Compiling block without introducing new scope (depth %d)\n",
               ctx->symbols ? ctx->symbols->scope_depth : -1);
    }

    // Compile the block content
    if (block->original->type == NODE_BLOCK) {
        for (int i = 0; i < block->typed.block.count; i++) {
            TypedASTNode* stmt = block->typed.block.statements[i];
            if (stmt) {
                compile_statement(ctx, stmt);
            }
        }
    } else {
        compile_statement(ctx, block);
    }

    if (create_scope) {
        DEBUG_CODEGEN_PRINT("Exiting scope (depth %d)\n", ctx->symbols->scope_depth);
        DEBUG_CODEGEN_PRINT("Freeing block-local variable registers");
        for (int i = 0; i < ctx->symbols->capacity; i++) {
            Symbol* symbol = ctx->symbols->symbols[i];
            while (symbol) {
                if (symbol->legacy_register_id >= MP_FRAME_REG_START &&
                    symbol->legacy_register_id <= MP_FRAME_REG_END) {
                    DEBUG_CODEGEN_PRINT("Freeing frame register R%d for variable '%s'",
                           symbol->legacy_register_id, symbol->name);
                    compiler_free_register(ctx->allocator, symbol->legacy_register_id);
                }
                symbol = symbol->next;
            }
        }

        if (lexical_frame) {
            ScopeFrame* refreshed = get_scope_frame_by_index(ctx, lexical_frame_index);
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

        free_symbol_table(ctx->symbols);
        ctx->symbols = old_scope;
    }
}

// ====== FUNCTION COMPILATION MANAGEMENT ======

// Register a compiled function and store its chunk
int register_function(CompilerContext* ctx, const char* name, int arity, BytecodeBuffer* chunk) {
    if (!ctx || !name) return -1;
    
    // Ensure function_chunks and function_arities arrays have capacity
    if (ctx->function_count >= ctx->function_capacity) {
        int new_capacity = ctx->function_capacity == 0 ? 8 : ctx->function_capacity * 2;
        ctx->function_chunks = realloc(ctx->function_chunks, sizeof(BytecodeBuffer*) * new_capacity);
        ctx->function_arities = realloc(ctx->function_arities, sizeof(int) * new_capacity);
        if (!ctx->function_chunks || !ctx->function_arities) return -1;
        ctx->function_capacity = new_capacity;
    }
    
    // Store the function chunk and arity (chunk can be NULL for pre-registration)
    int function_index = ctx->function_count++;
    ctx->function_chunks[function_index] = chunk;
    ctx->function_arities[function_index] = arity;
    
    DEBUG_CODEGEN_PRINT("Registered function '%s' with index %d (arity %d)\\n", name, function_index, arity);
    return function_index;
}

void update_function_bytecode(CompilerContext* ctx, int function_index, BytecodeBuffer* chunk) {
    if (!ctx || function_index < 0 || function_index >= ctx->function_count || !chunk) {
        DEBUG_CODEGEN_PRINT("Error: Invalid function update (index=%d, count=%d)\\n", function_index, ctx->function_count);
        return;
    }
    
    // Update the bytecode for the already registered function
    ctx->function_chunks[function_index] = chunk;
    DEBUG_CODEGEN_PRINT("Updated function index %d with compiled bytecode\\n", function_index);
}

// Get the bytecode chunk for a compiled function
BytecodeBuffer* get_function_chunk(CompilerContext* ctx, int function_index) {
    if (!ctx || function_index < 0 || function_index >= ctx->function_count) return NULL;
    return ctx->function_chunks[function_index];
}

// Copy compiled functions to the VM's function array
