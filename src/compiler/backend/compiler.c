//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/backend/compiler.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2023 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Coordinates the backend compilation pipeline, wiring analysis, optimization, and code generation stages.

// compiler.c - Multi-pass compiler pipeline coordinator
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "compiler/typed_ast_visualizer.h"
#include "compiler/optimization/optimizer.h"
#include "compiler/codegen/codegen.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/symbol_table.h"
#include "compiler/scope_stack.h"
#include "compiler/error_reporter.h"
#include "config/config.h"
#include "errors/features/control_flow_errors.h"
#include "internal/error_reporting.h"
#include "runtime/memory.h"
#include "type/type.h"
#include "vm/vm_string_ops.h"
#include "debug/debug_config.h"
#include "internal/strutil.h"
#include "vm/module_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reserve_existing_module_globals(CompilerContext* ctx) {
    if (!ctx || !ctx->allocator) {
        return;
    }

    extern VM vm;
    ModuleManager* manager = vm.register_file.module_manager;
    if (!manager) {
        return;
    }

    RegisterModule* module = manager->modules;
    while (module) {
        if (module->exports.exported_registers) {
            for (uint16_t i = 0; i < module->exports.export_count; ++i) {
                uint16_t reg = module->exports.exported_registers[i];
                if (reg != MODULE_EXPORT_NO_REGISTER) {
                    compiler_reserve_global(ctx->allocator, reg);
                }
            }
        }
        module = module->next;
    }
}

static void register_existing_module_symbols(CompilerContext* ctx, const char* module_name) {
    if (!ctx || !ctx->is_module || !ctx->symbols) {
        return;
    }

    if (!module_name || module_name[0] == '\0') {
        if (vm.filePath && strcmp(vm.filePath, "<repl>") == 0) {
            module_name = "__repl__";
        } else {
            return;
        }
    }

    ModuleManager* manager = vm.register_file.module_manager;
    if (!manager) {
        return;
    }

    RegisterModule* module = find_module(manager, module_name);
    if (!module || !module->exports.exported_names) {
        return;
    }

    for (uint16_t i = 0; i < module->exports.export_count; ++i) {
        const char* name = module->exports.exported_names[i];
        if (!name || resolve_symbol_local_only(ctx->symbols, name)) {
            continue;
        }

        uint16_t reg = MODULE_EXPORT_NO_REGISTER;
        if (module->exports.exported_registers && i < module->exports.export_count) {
            reg = module->exports.exported_registers[i];
        }
        if (reg == MODULE_EXPORT_NO_REGISTER) {
            continue;
        }

        Type* exported_type = NULL;
        if (module->exports.exported_types && i < module->exports.export_count) {
            exported_type = module->exports.exported_types[i];
        }

        register_variable(ctx, ctx->symbols, name, reg,
                          exported_type ? exported_type : getPrimitiveType(TYPE_ANY),
                          true, true, (SrcLocation){NULL, 0, 0}, true);
    }
}

// ===== MULTI-PASS COMPILER PIPELINE COORDINATOR =====
// Orchestrates the entire compilation process:
// TypedAST → Optimization → CodeGeneration → Bytecode

// BytecodeBuffer implementation
BytecodeBuffer* init_bytecode_buffer(void) {
    BytecodeBuffer* buffer = malloc(sizeof(BytecodeBuffer));
    if (!buffer) return NULL;

    // Ensure all fields are initialized before any allocation so cleanup paths
    // can safely free partially constructed buffers.
    buffer->instructions = NULL;
    buffer->source_lines = NULL;
    buffer->source_columns = NULL;
    buffer->source_files = NULL;
    buffer->patches = NULL;
    buffer->count = 0;
    buffer->capacity = 256;  // Initial capacity
    buffer->patch_count = 0;
    buffer->patch_capacity = 0;

    buffer->instructions = malloc(buffer->capacity);
    buffer->source_lines = malloc(buffer->capacity * sizeof(int));
    buffer->source_columns = malloc(buffer->capacity * sizeof(int));
    buffer->source_files = malloc(buffer->capacity * sizeof(const char*));

    if (!buffer->instructions || !buffer->source_lines || !buffer->source_columns ||
        !buffer->source_files) {
        free_bytecode_buffer(buffer);
        return NULL;
    }

    buffer->current_location.file = NULL;
    buffer->current_location.line = -1;
    buffer->current_location.column = -1;
    buffer->has_current_location = false;

    return buffer;
}

void free_bytecode_buffer(BytecodeBuffer* buffer) {
    if (!buffer) return;

    free(buffer->instructions);
    free(buffer->source_lines);
    free(buffer->source_columns);
    free(buffer->source_files);
    free(buffer->patches);
    free(buffer);
}

// ===== CONSTANT POOL IMPLEMENTATION =====

ConstantPool* init_constant_pool(void) {
    ConstantPool* pool = malloc(sizeof(ConstantPool));
    if (!pool) return NULL;
    
    pool->capacity = 16;  // Initial capacity
    pool->count = 0;
    pool->values = malloc(pool->capacity * sizeof(Value));
    
    if (!pool->values) {
        free(pool);
        return NULL;
    }
    
    DEBUG_CODEGEN_PRINT("Created constant pool (capacity=%d)\n", pool->capacity);
    return pool;
}

void free_constant_pool(ConstantPool* pool) {
    if (!pool) return;
    
    free(pool->values);
    free(pool);
    
    DEBUG_CODEGEN_PRINT("Freed constant pool\n");
}

int add_constant(ConstantPool* pool, Value value) {
    if (!pool) return -1;
    
    // Check if constant already exists (for deduplication)
    for (int i = 0; i < pool->count; i++) {
        Value existing = pool->values[i];
        if (existing.type == value.type) {
            // Simple equality check for basic types
            if (value.type == VAL_I32 && AS_I32(existing) == AS_I32(value)) {
                DEBUG_CODEGEN_PRINT("Reusing existing i32 constant %d at index %d\n", AS_I32(value), i);
                return i;
            }
            if (value.type == VAL_STRING && AS_STRING(existing) == AS_STRING(value)) {
                DEBUG_CODEGEN_PRINT("Reusing existing string constant at index %d\n", i);
                return i;
            }
        }
    }
    
    // Resize if needed
    if (pool->count >= pool->capacity) {
        pool->capacity *= 2;
        pool->values = realloc(pool->values, pool->capacity * sizeof(Value));
        if (!pool->values) return -1;
        DEBUG_CODEGEN_PRINT("Resized to capacity %d\n", pool->capacity);
    }
    
    // Add new constant
    pool->values[pool->count] = value;
    int index = pool->count;
    pool->count++;
    
    if (value.type == VAL_I32) {
        DEBUG_CODEGEN_PRINT("Added i32 constant %d at index %d\n", AS_I32(value), index);
    } else if (value.type == VAL_STRING) {
        DEBUG_CODEGEN_PRINT("Added string constant \"%s\" at index %d\n",
                           string_get_chars(AS_STRING(value)), index);
    } else {
        DEBUG_CODEGEN_PRINT("Added constant (type=%d) at index %d\n", value.type, index);
    }
    
    return index;
}

Value get_constant(ConstantPool* pool, int index) {
    if (!pool || index < 0 || index >= pool->count) {
        // Return a default value for invalid indices
        Value nil = {.type = VAL_BOOL, .as.boolean = false};
        return nil;
    }
    
    return pool->values[index];
}

void emit_byte_to_buffer(BytecodeBuffer* buffer, uint8_t byte) {
    if (!buffer) return;

    // Resize if needed
    if (buffer->count >= buffer->capacity) {
        buffer->capacity *= 2;
        buffer->instructions = realloc(buffer->instructions, buffer->capacity);
        buffer->source_lines = realloc(buffer->source_lines, buffer->capacity * sizeof(int));
        buffer->source_columns = realloc(buffer->source_columns, buffer->capacity * sizeof(int));
        buffer->source_files = realloc(buffer->source_files, buffer->capacity * sizeof(const char*));
    }

    buffer->instructions[buffer->count] = byte;
    if (buffer->has_current_location) {
        buffer->source_lines[buffer->count] = buffer->current_location.line;
        buffer->source_columns[buffer->count] = buffer->current_location.column;
        if (buffer->source_files) {
            buffer->source_files[buffer->count] = buffer->current_location.file;
        }
    } else {
        buffer->source_lines[buffer->count] = -1;
        buffer->source_columns[buffer->count] = -1;
        if (buffer->source_files) {
            buffer->source_files[buffer->count] = NULL;
        }
    }
    buffer->count++;
}

void bytecode_set_location(BytecodeBuffer* buffer, SrcLocation location) {
    if (!buffer) return;
    buffer->current_location = location;
    buffer->has_current_location = true;
}

void bytecode_set_synthetic_location(BytecodeBuffer* buffer) {
    if (!buffer) return;
    buffer->current_location.file = NULL;
    buffer->current_location.line = -1;
    buffer->current_location.column = -1;
    buffer->has_current_location = true;
}

void emit_word_to_buffer(BytecodeBuffer* buffer, uint16_t word) {
    // Emit 16-bit value as high byte, low byte (matches constant pool pattern)
    emit_byte_to_buffer(buffer, (word >> 8) & 0xFF);  // High byte
    emit_byte_to_buffer(buffer, word & 0xFF);         // Low byte
}

void emit_instruction_to_buffer(BytecodeBuffer* buffer, uint8_t opcode, uint8_t reg1, uint8_t reg2, uint8_t reg3) {
    emit_byte_to_buffer(buffer, opcode);
    emit_byte_to_buffer(buffer, reg1);
    emit_byte_to_buffer(buffer, reg2);
    emit_byte_to_buffer(buffer, reg3);
}

static inline int determine_prefix_size(uint8_t opcode) {
    switch (opcode) {
        case OP_JUMP_IF_NOT_I32_TYPED:
            return 3;  // opcode + left_reg + right_reg
        case OP_BRANCH_TYPED:
            return 4;  // opcode + loop_id_hi + loop_id_lo + predicate register
        case OP_JUMP_IF_NOT_R:
        case OP_JUMP_IF_R:
        case OP_TRY_BEGIN:
            return 2;  // opcode + condition register
        case OP_JUMP_IF_NOT_SHORT:
            return 2;  // opcode + condition register
        default:
            return 1;  // opcode only
    }
}

static inline int determine_operand_size(uint8_t opcode) {
    switch (opcode) {
        case OP_JUMP_SHORT:
        case OP_JUMP_BACK_SHORT:
        case OP_JUMP_IF_NOT_SHORT:
        case OP_LOOP_SHORT:
            return 1;
        case OP_TRY_BEGIN:
            return 2;
        default:
            return 2;
    }
}

int emit_jump_placeholder(BytecodeBuffer* buffer, uint8_t jump_opcode) {
    if (!buffer) {
        return -1;
    }

    int operand_size = determine_operand_size(jump_opcode);
    int prefix_size = determine_prefix_size(jump_opcode);

    int operand_offset = buffer->count;
    for (int i = 0; i < operand_size; i++) {
        emit_byte_to_buffer(buffer, 0);
    }

    if (buffer->patch_count >= buffer->patch_capacity) {
        int new_capacity = buffer->patch_capacity == 0 ? 8 : buffer->patch_capacity * 2;
        JumpPatch* new_patches = realloc(buffer->patches, new_capacity * sizeof(JumpPatch));
        if (!new_patches) {
            return -1;
        }
        buffer->patches = new_patches;
        buffer->patch_capacity = new_capacity;
    }

    JumpPatch* patch = &buffer->patches[buffer->patch_count];
    patch->opcode = jump_opcode;
    patch->operand_size = operand_size;
    patch->operand_offset = operand_offset;
    patch->instruction_offset = operand_offset - prefix_size;
    if (patch->instruction_offset < 0) {
        patch->instruction_offset = 0;
    }
    patch->target_label = -1;

    return buffer->patch_count++;
}

static inline void write_u16(BytecodeBuffer* buffer, int offset, uint16_t value) {
    buffer->instructions[offset] = (uint8_t)((value >> 8) & 0xFF);
    buffer->instructions[offset + 1] = (uint8_t)(value & 0xFF);
}

bool patch_jump(BytecodeBuffer* buffer, int patch_index, int target_offset) {
    if (!buffer || patch_index < 0 || patch_index >= buffer->patch_count) {
        return false;
    }

    JumpPatch* patch = &buffer->patches[patch_index];
    if (patch->operand_size <= 0) {
        return false;
    }

    int next_ip = patch->operand_offset + patch->operand_size;
    int32_t relative = 0;

    switch (patch->opcode) {
        case OP_JUMP_IF_NOT_R:
        case OP_JUMP_IF_R:
        case OP_TRY_BEGIN:
        case OP_JUMP_IF_NOT_I32_TYPED:
        case OP_BRANCH_TYPED:
        {
            relative = target_offset - next_ip;
            if (relative < 0 || relative > 0xFFFF) {
                return false;
            }
            write_u16(buffer, patch->operand_offset, (uint16_t)relative);
            break;
        }
        case OP_JUMP_IF_NOT_SHORT:
        {
            relative = target_offset - next_ip;
            if (relative < 0 || relative > 0xFF) {
                return false;
            }
            buffer->instructions[patch->operand_offset] = (uint8_t)relative;
            break;
        }
        case OP_JUMP_SHORT:
        {
            relative = target_offset - next_ip;
            if (relative < 0 || relative > 0xFF) {
                return false;
            }
            buffer->instructions[patch->operand_offset] = (uint8_t)relative;
            break;
        }
        case OP_JUMP_BACK_SHORT:
        case OP_LOOP_SHORT:
        {
            relative = next_ip - target_offset;
            if (relative < 0 || relative > 0xFF) {
                return false;
            }
            buffer->instructions[patch->operand_offset] = (uint8_t)relative;
            break;
        }
        case OP_LOOP:
        {
            relative = next_ip - target_offset;
            if (relative < 0 || relative > 0xFFFF) {
                return false;
            }
            write_u16(buffer, patch->operand_offset, (uint16_t)relative);
            break;
        }
        case OP_JUMP:
        {
            relative = target_offset - next_ip;
            if (relative >= 0) {
                if (relative > 0xFFFF) {
                    return false;
                }
                write_u16(buffer, patch->operand_offset, (uint16_t)relative);
            } else {
                int32_t distance = next_ip - target_offset;
                if (distance < 0 || distance > 0xFFFF) {
                    return false;
                }
                buffer->instructions[patch->instruction_offset] = OP_LOOP;
                patch->opcode = OP_LOOP;
                write_u16(buffer, patch->operand_offset, (uint16_t)distance);
            }
            break;
        }
        default:
        {
            // Treat unknown opcodes as forward 16-bit jumps
            relative = target_offset - next_ip;
            if (relative < 0 || relative > 0xFFFF) {
                return false;
            }
            write_u16(buffer, patch->operand_offset, (uint16_t)relative);
            break;
        }
    }

    patch->target_label = target_offset;
    return true;
}

// CompilerContext implementation
CompilerContext* init_compiler_context(TypedASTNode* typed_ast) {
    if (!typed_ast) return NULL;
    
    CompilerContext* ctx = malloc(sizeof(CompilerContext));
    if (!ctx) return NULL;
    
    // Initialize all fields
    ctx->input_ast = typed_ast;
    ctx->optimized_ast = NULL;
    
    // Initialize register allocation - DUAL SYSTEM
    ctx->allocator = compiler_create_allocator();            // Unified dual system
    ctx->next_temp_register = MP_TEMP_REG_START;
    ctx->next_local_register = MP_FRAME_REG_START;
    ctx->next_global_register = MP_GLOBAL_REG_START;
    
    // Initialize bytecode output
    ctx->bytecode = init_bytecode_buffer();
    
    // Initialize constant pool
    ctx->constants = init_constant_pool();
    
    // Initialize debugging
    ctx->enable_visualization = false;  // Default off
    ctx->dump_bytecode = false;
    ctx->debug_output = stdout;
    
    // Initialize symbol table for variable tracking
    ctx->symbols = create_symbol_table(NULL);  // Global scope
    ctx->scopes = scope_stack_create();
    control_flow_register_scope_stack(ctx->scopes);
    ctx->errors = error_reporter_create();
    ctx->has_compilation_errors = false;  // Initialize error tracking
    ctx->compiling_function = false;      // Initialize function compilation flag
    ctx->function_scope_depth = ctx->symbols ? ctx->symbols->scope_depth : 0; // Track function root depth
    ctx->opt_ctx = NULL;      // Will implement in Phase 2
    
    // Initialize loop control context
    ctx->current_loop_start = -1;     // No current loop
    ctx->current_loop_end = -1;       // No current loop
    ctx->current_loop_continue = -1;  // No current loop
    ctx->current_loop_id = 0;
    ctx->next_loop_id = 1;
    ctx->break_statements = NULL;     // No break statements yet
    ctx->break_count = 0;
    ctx->break_capacity = 0;
    ctx->continue_statements = NULL;  // No continue statements yet
    ctx->continue_count = 0;
    ctx->continue_capacity = 0;

    ctx->branch_depth = 0;            // Not inside any conditional branches
    
    // Initialize function compilation context (NEW)
    ctx->current_function_index = -1;    // Global scope initially
    ctx->function_chunks = NULL;         // No function chunks yet
    ctx->function_arities = NULL;        // No function arities yet
    ctx->function_count = 0;
    ctx->function_capacity = 0;

    // Initialize closure context
    ctx->upvalues = NULL;
    ctx->upvalue_count = 0;
    ctx->upvalue_capacity = 0;

    // Module export tracking
    ctx->is_module = false;
    ctx->module_exports = NULL;
    ctx->module_export_count = 0;
    ctx->module_export_capacity = 0;
    ctx->module_imports = NULL;
    ctx->module_import_count = 0;
    ctx->module_import_capacity = 0;

    if (!ctx->allocator || !ctx->bytecode || !ctx->constants ||
        !ctx->symbols || !ctx->scopes || !ctx->errors) {
        free_compiler_context(ctx);
        return NULL;
    }
    
    return ctx;
}

bool compile_to_bytecode(CompilerContext* ctx) {
    if (!ctx || !ctx->input_ast) return false;

    if (ctx->errors) {
        error_reporter_reset(ctx->errors);
    }
    ctx->has_compilation_errors = false;

    DEBUG_CODEGEN_PRINT("Starting compilation pipeline...\n");
    
    // Phase 1: Visualization (if enabled)
    DEBUG_CODEGEN_PRINT("Phase 1: Visualization...\n");
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== INPUT TYPED AST ===\n");
        visualize_typed_ast(ctx->input_ast, ctx->debug_output);
    }
    DEBUG_CODEGEN_PRINT("Phase 1: Visualization completed\n");
    
    // Phase 2: Optimization Pass
    DEBUG_CODEGEN_PRINT("Phase 2: About to start optimization pass...\n");
    if (!run_optimization_pass(ctx)) {
        DEBUG_CODEGEN_PRINT("Optimization pass failed\n");
        return false;
    }
    DEBUG_CODEGEN_PRINT("Phase 2: Optimization pass completed\n");
    
    // Phase 3: Code Generation Pass  
    DEBUG_CODEGEN_PRINT("Phase 3: About to start code generation pass...\n");
    if (!run_codegen_pass(ctx)) {
        DEBUG_CODEGEN_PRINT("Code generation pass failed\n");
        return false;
    }
    DEBUG_CODEGEN_PRINT("Phase 3: Code generation pass completed\n");
    
    DEBUG_CODEGEN_PRINT("Compilation completed successfully, generated %d instructions\n", 
           ctx->bytecode->count);
    return true;
}

bool run_optimization_pass(CompilerContext* ctx) {
    DEBUG_OPTIMIZER_PRINT("🚀 Running optimization pass...\n");
    
    // Initialize optimization context
    OptimizationContext* opt_ctx = init_optimization_context();
    if (!opt_ctx) {
        DEBUG_OPTIMIZER_PRINT("❌ Failed to initialize optimization context\n");
        return false;
    }
    
    // Run actual optimizations on the TypedAST
    ctx->optimized_ast = optimize_typed_ast(ctx->input_ast, opt_ctx);
    
    if (!ctx->optimized_ast) {
        DEBUG_OPTIMIZER_PRINT("❌ Optimization failed\n");
        free_optimization_context(opt_ctx);
        return false;
    }
    
    // 🎯 CRITICAL: Visualization after optimization to show differences
    if (ctx->enable_visualization) {
        fprintf(ctx->debug_output, "\n=== OPTIMIZED TYPED AST ===\n");
        visualize_typed_ast(ctx->optimized_ast, ctx->debug_output);
        fprintf(ctx->debug_output, "\n");
    }
    
    // Store the optimization context for potential use in codegen
    ctx->opt_ctx = opt_ctx;
    
    DEBUG_OPTIMIZER_PRINT("✅ Optimization pass completed with real optimizations!\n");
    return true;
}

bool run_codegen_pass(CompilerContext* ctx) {
    DEBUG_CODEGEN_PRINT("Running production-grade code generation pass...\n");
    
    // Use the new production-ready code generator
    bool success = generate_bytecode_from_ast(ctx);
    
    if (success) {
        DEBUG_CODEGEN_PRINT("✅ Code generation completed, %d instructions generated\n", 
               ctx->bytecode->count);
        
        // Copy compiled functions to the VM
        if (ctx->function_count > 0) {
            finalize_functions_to_vm(ctx);
        }
        
        // Add bytecode visualization if enabled
        if (ctx->dump_bytecode) {
            FILE* out = ctx->debug_output ? ctx->debug_output : stdout;
            fprintf(out, "\n=== BYTECODE DUMP ===\n");
            fprintf(out, "Instructions: %d\n", ctx->bytecode->count);

            // Decode and display each instruction
            for (int i = 0; i < ctx->bytecode->count; i += 4) {
                if (i + 3 < ctx->bytecode->count) {
                    uint8_t opcode = ctx->bytecode->instructions[i];
                    uint8_t reg1 = ctx->bytecode->instructions[i + 1];
                    uint8_t reg2 = ctx->bytecode->instructions[i + 2];
                    uint8_t reg3 = ctx->bytecode->instructions[i + 3];

                    fprintf(out, "%04d: %02X", i, opcode);

                    // Decode common opcodes for better readability
                    if (opcode == 0xAB) { // OP_LOAD_I32_CONST
                        int32_t value = (reg2 << 8) | reg3;
                        fprintf(out, " (OP_LOAD_I32_CONST) reg=R%d, value=%d", reg1, value);
                    } else if (opcode == 0xAE) { // OP_MOVE_I32
                        fprintf(out, " (OP_MOVE_I32) dst=R%d, src=R%d", reg1, reg2);
                    } else if (opcode == 0x78) { // OP_PRINT_R
                        fprintf(out, " (OP_PRINT_R) reg=R%d", reg1);
                    } else if (opcode == 0xC4) { // OP_HALT
                        fprintf(out, " (OP_HALT)");
                    } else {
                        fprintf(out, " (OPCODE_%02X) R%d, R%d, R%d", opcode, reg1, reg2, reg3);
                    }

                    // Suppress unused variable warnings
                    (void)reg1;

                    fprintf(out, "\n");
                }
            }

            fprintf(out, "=== END BYTECODE ===\n\n");

            // Add optimized bytecode version
            fprintf(out, "=== OPTIMIZED BYTECODE ===\n");
            fprintf(out, "Register-optimized instruction sequence:\n");

            // Decode and display optimized instructions with cleaner formatting
            for (int i = 0; i < ctx->bytecode->count; i += 4) {
                if (i + 3 < ctx->bytecode->count) {
                    uint8_t opcode = ctx->bytecode->instructions[i];
                    uint8_t reg1 = ctx->bytecode->instructions[i + 1];
                    uint8_t reg2 = ctx->bytecode->instructions[i + 2];
                    uint8_t reg3 = ctx->bytecode->instructions[i + 3];

                    // Clean, optimized format showing register reuse and specialization
                    if (opcode == 0xAB) { // OP_LOAD_I32_CONST
                        int32_t value = (reg2 << 8) | reg3;
                        fprintf(out, "  LOAD_CONST  R%-3d ← %d\n", reg1, value);
                    } else if (opcode == 0xAE) { // OP_MOVE_I32
                        fprintf(out, "  MOVE        R%-3d ← R%d\n", reg1, reg2);
                    } else if (opcode == 0x78) { // OP_PRINT_R
                        fprintf(out, "  PRINT       R%-3d\n", reg1);
                    } else if (opcode == 0xC4) { // OP_HALT
                        fprintf(out, "  HALT\n");
                    } else {
                        fprintf(out, "  OP_%02X       R%-3d, R%-3d, R%-3d\n", opcode, reg1, reg2, reg3);
                    }

                    // Suppress unused variable warnings
                    (void)reg1;
                }
            }

            fprintf(out, "=== END OPTIMIZED BYTECODE ===\n\n");
            fprintf(out, "🚀 Register Allocation Summary:\n");
            fprintf(out, "   - Temp registers (R192-R239): Used for intermediate values\n");
            fprintf(out, "   - Frame registers (R64-R191): Used for variables\n");
            fprintf(out, "   - Specialized opcodes: OP_LOAD_I32_CONST, OP_MOVE_I32\n\n");
        }
    } else {
        DEBUG_CODEGEN_PRINT("❌ Code generation failed\n");
    }
    
    return success;
}

void free_compiler_context(CompilerContext* ctx) {
    if (!ctx) return;
    
    compiler_destroy_allocator(ctx->allocator);
    free_bytecode_buffer(ctx->bytecode);
    free_constant_pool(ctx->constants);
    
    // Free symbol table
    if (ctx->symbols) {
        free_symbol_table(ctx->symbols);
    }

    if (ctx->scopes) {
        control_flow_unregister_scope_stack(ctx->scopes);
        scope_stack_destroy(ctx->scopes);
    }

    if (ctx->errors) {
        error_reporter_destroy(ctx->errors);
    }
    
    // Free optimization context if it was created
    if (ctx->opt_ctx) {
        free_optimization_context(ctx->opt_ctx);
    }
    
    // Free break statement tracking
    if (ctx->break_statements) {
        free(ctx->break_statements);
    }
    
    // Free continue statement tracking
    if (ctx->continue_statements) {
        free(ctx->continue_statements);
    }
    
    // Free function compilation resources (NEW)
    if (ctx->function_chunks) {
        for (int i = 0; i < ctx->function_count; i++) {
            if (ctx->function_chunks[i]) {
                free_bytecode_buffer(ctx->function_chunks[i]);
            }
        }
        free(ctx->function_chunks);
    }
    
    // Free function arities array
    if (ctx->function_arities) {
        free(ctx->function_arities);
    }

    if (ctx->upvalues) {
        free(ctx->upvalues);
    }

    if (ctx->module_exports) {
        for (int i = 0; i < ctx->module_export_count; i++) {
            free(ctx->module_exports[i].name);
            if (ctx->module_exports[i].type) {
                module_free_export_type(ctx->module_exports[i].type);
                ctx->module_exports[i].type = NULL;
            }
        }
        free(ctx->module_exports);
    }

    if (ctx->module_imports) {
        for (int i = 0; i < ctx->module_import_count; i++) {
            free(ctx->module_imports[i].module_name);
            free(ctx->module_imports[i].symbol_name);
            free(ctx->module_imports[i].alias_name);
        }
        free(ctx->module_imports);
    }

    // Note: Don't free input_ast - it's owned by caller

    free(ctx);
}

// ===== LEGACY COMPATIBILITY STUBS =====
// These functions are still called by vm.c and should be removed in the future

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source) {
    // Legacy compatibility - minimal initialization
    if (compiler) {
        compiler->chunk = chunk;
        compiler->fileName = fileName;
        compiler->source = source;
        compiler->nextRegister = 0;
        compiler->isModule = false;
        compiler->exportCount = 0;
        compiler->importCount = 0;
        for (int i = 0; i < UINT8_COUNT; ++i) {
            compiler->exports[i].name = NULL;
            compiler->exports[i].kind = MODULE_EXPORT_KIND_GLOBAL;
            compiler->exports[i].register_index = -1;
            compiler->exports[i].type = NULL;
            compiler->imports[i].module_name = NULL;
            compiler->imports[i].symbol_name = NULL;
            compiler->imports[i].alias_name = NULL;
            compiler->imports[i].kind = MODULE_EXPORT_KIND_GLOBAL;
            compiler->imports[i].register_index = -1;
        }
    }
}

void freeCompiler(Compiler* compiler) {
    if (!compiler) {
        return;
    }
    compiler_reset_exports(compiler);
}

static void report_compiler_diagnostics(const CompilerContext* ctx) {
    if (!ctx || !ctx->errors) {
        return;
    }

    size_t diagnostic_count = error_reporter_count(ctx->errors);
    const CompilerDiagnostic* diagnostics = error_reporter_diagnostics(ctx->errors);
    if (diagnostic_count == 0 || !diagnostics) {
        return;
    }

    for (size_t i = 0; i < diagnostic_count; ++i) {
        const CompilerDiagnostic* diagnostic = &diagnostics[i];
        SrcLocation location = diagnostic->location;
        if (!location.file && ctx->input_ast && ctx->input_ast->original) {
            location = ctx->input_ast->original->location;
        }

        int caret_start = location.column > 0 ? location.column - 1 : 0;
        int caret_end = location.column > 0 ? location.column : 0;

        const char* category = get_error_category(diagnostic->code);
        const char* title = get_error_title(diagnostic->code);
        const char* help = diagnostic->help ? diagnostic->help : get_error_help(diagnostic->code);
        const char* note = diagnostic->note ? diagnostic->note : get_error_note(diagnostic->code);

        const char* message = diagnostic->message ? diagnostic->message : title;

        EnhancedError error = {
            .code = diagnostic->code,
            .severity = diagnostic->severity,
            .category = category,
            .title = title,
            .message = message,
            .help = help,
            .note = note,
            .location = location,
            .source_line = NULL,
            .caret_start = caret_start,
            .caret_end = caret_end,
        };

        report_enhanced_error(&error);
    }
}

static bool copy_compiled_bytecode(Compiler* legacy_compiler, CompilerContext* ctx) {
    if (!legacy_compiler || !legacy_compiler->chunk || !ctx || !ctx->bytecode) {
        return false;
    }

    Chunk* chunk = legacy_compiler->chunk;
    BytecodeBuffer* bytecode = ctx->bytecode;

    // Reset any previous chunk contents so stale bytecode cannot escape on failures
    freeChunk(chunk);

    chunk->count = bytecode->count;
    chunk->capacity = bytecode->count;

    if (bytecode->count > 0) {
        chunk->code = (uint8_t*)reallocate(NULL, 0, (size_t)bytecode->count);
        chunk->lines = (int*)reallocate(NULL, 0, sizeof(int) * (size_t)bytecode->count);
        chunk->columns = (int*)reallocate(NULL, 0, sizeof(int) * (size_t)bytecode->count);
        chunk->files = (const char**)reallocate(NULL, 0, sizeof(const char*) * (size_t)bytecode->count);

        memcpy(chunk->code, bytecode->instructions, (size_t)bytecode->count);

        if (bytecode->source_lines) {
            memcpy(chunk->lines, bytecode->source_lines, sizeof(int) * (size_t)bytecode->count);
        } else {
            for (int i = 0; i < bytecode->count; ++i) {
                chunk->lines[i] = -1;
            }
        }

        if (bytecode->source_columns) {
            memcpy(chunk->columns, bytecode->source_columns, sizeof(int) * (size_t)bytecode->count);
        } else {
            for (int i = 0; i < bytecode->count; ++i) {
                chunk->columns[i] = -1;
            }
        }

        if (chunk->files && bytecode->source_files) {
            memcpy(chunk->files, bytecode->source_files, sizeof(const char*) * (size_t)bytecode->count);
        } else if (chunk->files) {
            for (int i = 0; i < bytecode->count; ++i) {
                chunk->files[i] = NULL;
            }
        }
    } else {
        chunk->code = NULL;
        chunk->lines = NULL;
        chunk->columns = NULL;
        chunk->files = NULL;
    }

    // Copy constants into the VM chunk using the existing helper so GC accounting remains correct
    if (ctx->constants && ctx->constants->count > 0) {
        for (int i = 0; i < ctx->constants->count; ++i) {
            addConstant(chunk, ctx->constants->values[i]);
        }
    }

    // Copy module export metadata for module compilations
    if (legacy_compiler) {
        compiler_reset_exports(legacy_compiler);
        legacy_compiler->isModule = ctx->is_module;

        if (ctx->module_export_count > 0) {
            int limit = ctx->module_export_count < UINT8_COUNT ? ctx->module_export_count : UINT8_COUNT;
            for (int i = 0; i < limit; ++i) {
                legacy_compiler->exports[i].kind = ctx->module_exports[i].kind;
                if (ctx->module_exports[i].name) {
                    legacy_compiler->exports[i].name = orus_strdup(ctx->module_exports[i].name);
                } else {
                    legacy_compiler->exports[i].name = NULL;
                }
                legacy_compiler->exports[i].register_index = ctx->module_exports[i].register_index;
                legacy_compiler->exports[i].type = ctx->module_exports[i].type;
                ctx->module_exports[i].type = NULL;
                if (!legacy_compiler->exports[i].name && ctx->module_exports[i].name) {
                    legacy_compiler->exportCount = i;
                    break;
                }
                legacy_compiler->exportCount = i + 1;
            }
        }

        if (ctx->module_import_count > 0) {
            int limit = ctx->module_import_count < UINT8_COUNT ? ctx->module_import_count : UINT8_COUNT;
            for (int i = 0; i < limit; ++i) {
                legacy_compiler->imports[i].kind = ctx->module_imports[i].kind;
                if (ctx->module_imports[i].module_name) {
                    legacy_compiler->imports[i].module_name = orus_strdup(ctx->module_imports[i].module_name);
                } else {
                    legacy_compiler->imports[i].module_name = NULL;
                }
                if (ctx->module_imports[i].symbol_name) {
                    legacy_compiler->imports[i].symbol_name = orus_strdup(ctx->module_imports[i].symbol_name);
                } else {
                    legacy_compiler->imports[i].symbol_name = NULL;
                }
                if (ctx->module_imports[i].alias_name) {
                    legacy_compiler->imports[i].alias_name = orus_strdup(ctx->module_imports[i].alias_name);
                } else {
                    legacy_compiler->imports[i].alias_name = NULL;
                }
                legacy_compiler->imports[i].register_index = ctx->module_imports[i].register_index;
                if ((ctx->module_imports[i].module_name && !legacy_compiler->imports[i].module_name) ||
                    (ctx->module_imports[i].symbol_name && !legacy_compiler->imports[i].symbol_name) ||
                    (ctx->module_imports[i].alias_name && !legacy_compiler->imports[i].alias_name)) {
                    legacy_compiler->importCount = i;
                    break;
                }
                legacy_compiler->importCount = i + 1;
            }
        }
    }

    return true;
}

bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule) {
    (void)isModule;

    if (!ast || !compiler || !compiler->chunk) {
        return false;
    }

    const OrusConfig* config = config_get_global();
    bool show_typed_ast = config && config->show_typed_ast;
    bool show_bytecode = config && config->show_bytecode;

    // Ensure the target chunk starts from a clean slate even if compilation fails
    freeChunk(compiler->chunk);

    init_type_inference();

    TypeEnv* type_env = type_env_new(NULL);
    if (!type_env) {
        report_compile_error(E9003_COMPILER_BUG, ast->location,
                             "failed to allocate type environment for compilation");
        cleanup_type_inference();
        return false;
    }

    TypedASTNode* typed_ast = generate_typed_ast(ast, type_env);
    if (!typed_ast) {
        cleanup_type_inference();
        return false;
    }

    if (show_typed_ast && compiler->source) {
        printf("\n=== TYPED AST VISUALIZATION ===\n");
        if (compiler->fileName) {
            printf("Source: %s\n", compiler->fileName);
        }
        printf("================================\n");
        if (terminal_supports_color()) {
            visualize_typed_ast_colored(typed_ast, stdout);
        } else {
            visualize_typed_ast_detailed(typed_ast, stdout, true, true);
        }
        printf("\n=== END TYPED AST ===\n\n");
    }

    CompilerContext* ctx = init_compiler_context(typed_ast);
    if (!ctx) {
        free_typed_ast_node(typed_ast);
        cleanup_type_inference();
        return false;
    }

    ctx->is_module = isModule;

    if (ctx->errors) {
        error_reporter_set_use_colors(ctx->errors, config ? config->error_colors : true);
    }

    if (ctx->is_module) {
        const char* module_name_for_symbols = NULL;
        if (typed_ast->original && typed_ast->original->type == NODE_PROGRAM) {
            module_name_for_symbols = typed_ast->original->program.moduleName;
        }
        if (!module_name_for_symbols && typed_ast->original &&
            typed_ast->original->type == NODE_PROGRAM) {
            module_name_for_symbols = typed_ast->typed.program.moduleName;
        }
        reserve_existing_module_globals(ctx);
        register_existing_module_symbols(ctx, module_name_for_symbols);
    }

    ctx->enable_visualization = show_typed_ast;
    ctx->dump_bytecode = show_bytecode;
    ctx->debug_output = stdout;

    bool success = compile_to_bytecode(ctx);
    if (!success || ctx->has_compilation_errors) {
        report_compiler_diagnostics(ctx);
        free_compiler_context(ctx);
        free_typed_ast_node(typed_ast);
        cleanup_type_inference();
        return false;
    }

    bool copied = copy_compiled_bytecode(compiler, ctx);

    free_compiler_context(ctx);
    free_typed_ast_node(typed_ast);
    cleanup_type_inference();

    return copied;
}

void emitByte(Compiler* compiler, uint8_t byte) {
    // Legacy compatibility - minimal implementation
    if (compiler && compiler->chunk) {
        writeChunk(compiler->chunk, byte, 0, 0, NULL);
    }
}

// ====== DUAL REGISTER SYSTEM - Smart Instruction Emission ======

// Get the appropriate typed opcode for an operation and type
OpCode get_typed_opcode(const char* op, RegisterType type) {
    if (strcmp(op, "+") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_ADD_I32_TYPED;
            case REG_TYPE_I64: return OP_ADD_I64_TYPED;
            case REG_TYPE_F64: return OP_ADD_F64_TYPED;
            case REG_TYPE_U32: return OP_ADD_U32_TYPED;
            case REG_TYPE_U64: return OP_ADD_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "-") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_SUB_I32_TYPED;
            case REG_TYPE_I64: return OP_SUB_I64_TYPED;
            case REG_TYPE_F64: return OP_SUB_F64_TYPED;
            case REG_TYPE_U32: return OP_SUB_U32_TYPED;
            case REG_TYPE_U64: return OP_SUB_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "*") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MUL_I32_TYPED;
            case REG_TYPE_I64: return OP_MUL_I64_TYPED;
            case REG_TYPE_F64: return OP_MUL_F64_TYPED;
            case REG_TYPE_U32: return OP_MUL_U32_TYPED;
            case REG_TYPE_U64: return OP_MUL_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "/") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_DIV_I32_TYPED;
            case REG_TYPE_I64: return OP_DIV_I64_TYPED;
            case REG_TYPE_F64: return OP_DIV_F64_TYPED;
            case REG_TYPE_U32: return OP_DIV_U32_TYPED;
            case REG_TYPE_U64: return OP_DIV_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "%") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MOD_I32_TYPED;
            case REG_TYPE_I64: return OP_MOD_I64_TYPED;
            case REG_TYPE_F64: return OP_MOD_F64_TYPED;
            case REG_TYPE_U32: return OP_MOD_U32_TYPED;
            case REG_TYPE_U64: return OP_MOD_U64_TYPED;
            default: return OP_HALT; // Invalid
        }
    }
    
    return OP_HALT; // Invalid operation
}

// Get the appropriate standard opcode for an operation and type
OpCode get_standard_opcode(const char* op, RegisterType type) {
    if (strcmp(op, "+") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_ADD_I32_R;
            case REG_TYPE_I64: return OP_ADD_I64_R;
            case REG_TYPE_F64: return OP_ADD_F64_R;
            case REG_TYPE_U32: return OP_ADD_U32_R;
            case REG_TYPE_U64: return OP_ADD_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "-") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_SUB_I32_R;
            case REG_TYPE_I64: return OP_SUB_I64_R;
            case REG_TYPE_F64: return OP_SUB_F64_R;
            case REG_TYPE_U32: return OP_SUB_U32_R;
            case REG_TYPE_U64: return OP_SUB_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "*") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MUL_I32_R;
            case REG_TYPE_I64: return OP_MUL_I64_R;
            case REG_TYPE_F64: return OP_MUL_F64_R;
            case REG_TYPE_U32: return OP_MUL_U32_R;
            case REG_TYPE_U64: return OP_MUL_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "/") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_DIV_I32_R;
            case REG_TYPE_I64: return OP_DIV_I64_R;
            case REG_TYPE_F64: return OP_DIV_F64_R;
            case REG_TYPE_U32: return OP_DIV_U32_R;
            case REG_TYPE_U64: return OP_DIV_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    else if (strcmp(op, "%") == 0) {
        switch (type) {
            case REG_TYPE_I32: return OP_MOD_I32_R;
            case REG_TYPE_I64: return OP_MOD_I64_R;
            case REG_TYPE_F64: return OP_MOD_F64_R;
            case REG_TYPE_U32: return OP_MOD_U32_R;
            case REG_TYPE_U64: return OP_MOD_U64_R;
            default: return OP_HALT; // Invalid
        }
    }
    
    return OP_HALT; // Invalid operation
}

// Smart instruction emission that chooses between typed and standard opcodes
void emit_arithmetic_instruction_smart(CompilerContext* ctx, const char* op, 
                                     struct RegisterAllocation* dst, 
                                     struct RegisterAllocation* left, 
                                     struct RegisterAllocation* right) {
    if (!ctx || !dst || !left || !right) {
        DEBUG_CODEGEN_PRINT("Error: NULL parameters\n");
        return;
    }
    
    // Ensure all registers use the same strategy for compatibility
    RegisterStrategy strategy = dst->strategy;
    if (left->strategy != strategy || right->strategy != strategy) {
        DEBUG_CODEGEN_PRINT("Warning: Mixed register strategies, forcing standard\n");
        strategy = REG_STRATEGY_STANDARD;
    }
    
    OpCode opcode;
    uint8_t reg1, reg2, reg3;
    
    if (strategy == REG_STRATEGY_TYPED) {
        // Use typed instruction (performance optimized)
        opcode = get_typed_opcode(op, dst->physical_type);
        reg1 = (uint8_t)dst->physical_id;
        reg2 = (uint8_t)left->physical_id;
        reg3 = (uint8_t)right->physical_id;
        
        DEBUG_CODEGEN_PRINT("Using TYPED instruction: %s (opcode=%d) dst=%d, left=%d, right=%d\n", 
               op, opcode, reg1, reg2, reg3);
    } else {
        // Use standard instruction (compatibility)
        opcode = get_standard_opcode(op, dst->physical_type);
        reg1 = (uint8_t)dst->logical_id;
        reg2 = (uint8_t)left->logical_id;
        reg3 = (uint8_t)right->logical_id;
        
        DEBUG_CODEGEN_PRINT("Using STANDARD instruction: %s (opcode=%d) dst=%d, left=%d, right=%d\n", 
               op, opcode, reg1, reg2, reg3);
    }
    
    if (opcode == OP_HALT) {
        DEBUG_CODEGEN_PRINT("Error: Invalid opcode for operation '%s' and type %d\n", op, dst->physical_type);
        return;
    }
    
    // Emit the instruction
    emit_instruction_to_buffer(ctx->bytecode, opcode, reg1, reg2, reg3);
}