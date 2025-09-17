#include "compiler/codegen/peephole.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "vm/vm_constants.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Disable all debug output for clean program execution
#define PEEPHOLE_DEBUG 0
#if PEEPHOLE_DEBUG == 0
#define printf(...) ((void)0)
#endif

// Global context for tracking statistics
static PeepholeContext peephole_stats;

typedef struct {
    bool known;
    uint8_t opcode;
    uint16_t constant_index;
} RegisterConstant;

static void reset_register_state(RegisterConstant* state) {
    if (!state) return;
    memset(state, 0, sizeof(RegisterConstant) * VM_MAX_REGISTERS);
}

static void propagate_register(RegisterConstant* state, uint16_t dst, uint16_t src) {
    if (!state || dst >= VM_MAX_REGISTERS) return;
    if (src < VM_MAX_REGISTERS && state[src].known) {
        state[dst] = state[src];
    } else {
        state[dst].known = false;
    }
}

void init_peephole_context(PeepholeContext* ctx) {
    ctx->patterns_optimized = 0;
    ctx->instructions_eliminated = 0;
    ctx->load_move_fusions = 0;
    ctx->redundant_moves = 0;
    ctx->constant_propagations = 0;
}

bool apply_peephole_optimizations(CompilerContext* ctx) {
    if (!ctx || !ctx->bytecode) return false;
    
    printf("[PEEPHOLE] 🔧 Starting peephole optimizations...\n");
    init_peephole_context(&peephole_stats);
    
    // Apply different peephole optimization patterns
    peephole_stats.load_move_fusions = optimize_load_move_pattern(ctx);
    peephole_stats.redundant_moves = optimize_redundant_operations(ctx);
    peephole_stats.constant_propagations = optimize_constant_propagation(ctx);

    peephole_stats.patterns_optimized = peephole_stats.load_move_fusions +
                                       peephole_stats.redundant_moves +
                                       peephole_stats.constant_propagations;
    
    print_peephole_statistics(&peephole_stats);
    return true;
}

int optimize_load_move_pattern(CompilerContext* ctx) {
    int optimizations_applied = 0;
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    // Pattern: LOAD_CONST + MOVE → Direct LOAD_CONST to target
    // Before: LOAD_CONST R192, 5; MOVE R64, R192
    // After:  LOAD_CONST R64, 5
    for (int i = 0; i < bytecode->count - 4; i += 4) {
        if (is_load_move_pattern(ctx, i)) {
            uint8_t load_reg = bytecode->instructions[i + 1];
            uint8_t move_dst = bytecode->instructions[i + 5];
            uint8_t move_src = bytecode->instructions[i + 6];
            
            // If MOVE is copying from the LOAD target
            if (load_reg == move_src) {
                // Optimize: change LOAD target to final destination
                modify_instruction_register(ctx, i, 1, move_dst);
                
                // Remove the MOVE instruction
                eliminate_instruction_sequence(ctx, i + 4, 4);
                
                optimizations_applied++;
                peephole_stats.instructions_eliminated += 4;
                
                printf("[PEEPHOLE] ✅ Optimized LOAD+MOVE pattern: R%d directly loaded to R%d\n", 
                       load_reg, move_dst);
            }
        }
    }
    
    return optimizations_applied;
}

int optimize_redundant_operations(CompilerContext* ctx) {
    int moves_eliminated = 0;
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    // Pattern: Eliminate redundant moves where src == dst
    // MOVE R64, R64 → (remove instruction)
    for (int i = 0; i < bytecode->count - 4; i += 4) {
        if (is_redundant_move(ctx, i)) {
            uint8_t dst = bytecode->instructions[i + 1];
            uint8_t src = bytecode->instructions[i + 2];
            
            // If moving register to itself, eliminate the instruction
            if (dst == src) {
                eliminate_instruction_sequence(ctx, i, 4);
                moves_eliminated++;
                peephole_stats.instructions_eliminated += 4;
                i -= 4; // Check this position again
                printf("[PEEPHOLE] ✅ Eliminated redundant move R%d → R%d\n", src, dst);
            }
        }
    }
    
    return moves_eliminated;
}

static uint16_t read_u16(BytecodeBuffer* bytecode, int offset) {
    if (!bytecode || offset < 0 || offset + 1 >= bytecode->count) return 0;
    return (uint16_t)((bytecode->instructions[offset] << 8) |
                      bytecode->instructions[offset + 1]);
}

static bool can_eliminate_load(RegisterConstant* state, uint8_t reg,
                               uint8_t opcode, uint16_t constant_index) {
    if (!state || (unsigned)reg >= VM_MAX_REGISTERS) return false;
    return state[reg].known && state[reg].opcode == opcode &&
           state[reg].constant_index == constant_index;
}

static void remember_constant(RegisterConstant* state, uint8_t reg,
                              uint8_t opcode, uint16_t constant_index) {
    if (!state || (unsigned)reg >= VM_MAX_REGISTERS) return;
    state[reg].known = true;
    state[reg].opcode = opcode;
    state[reg].constant_index = constant_index;
}

int optimize_constant_propagation(CompilerContext* ctx) {
    if (!ctx || !ctx->bytecode) return 0;

    BytecodeBuffer* bytecode = ctx->bytecode;
    RegisterConstant register_state[VM_MAX_REGISTERS];
    reset_register_state(register_state);

    int optimizations = 0;
    int offset = 0;

    while (offset < bytecode->count) {
        uint8_t opcode = bytecode->instructions[offset];
        bool handled = false;

        switch (opcode) {
            case OP_LOAD_FALSE:
            case OP_LOAD_TRUE: {
                if (offset + 1 >= bytecode->count) {
                    offset = bytecode->count;
                    break;
                }

                uint8_t target = bytecode->instructions[offset + 1];
                uint16_t constant_index = (opcode == OP_LOAD_TRUE) ? 1 : 0;

                if (can_eliminate_load(register_state, target, opcode,
                                      constant_index)) {
                    eliminate_instruction_sequence(ctx, offset, 2);
                    peephole_stats.instructions_eliminated += 2;
                    optimizations++;
                    handled = true;
                    continue;
                }

                remember_constant(register_state, target, opcode, constant_index);
                offset += 2;
                handled = true;
                break;
            }

            case OP_LOAD_CONST:
            case OP_LOAD_I32_CONST:
            case OP_LOAD_I64_CONST:
            case OP_LOAD_F64_CONST: {
                if (offset + 3 >= bytecode->count) {
                    offset = bytecode->count;
                    break;
                }

                uint8_t target = bytecode->instructions[offset + 1];
                uint16_t constant_index = read_u16(bytecode, offset + 2);

                if (can_eliminate_load(register_state, target, opcode,
                                      constant_index)) {
                    eliminate_instruction_sequence(ctx, offset, 4);
                    peephole_stats.instructions_eliminated += 4;
                    optimizations++;
                    handled = true;
                    continue; // Re-process current offset after removal
                }

                remember_constant(register_state, target, opcode, constant_index);
                offset += 4;
                handled = true;
                break;
            }

            case OP_MOVE:
            case OP_MOVE_I32:
            case OP_MOVE_I64:
            case OP_MOVE_F64: {
                if (offset + 2 >= bytecode->count) {
                    offset = bytecode->count;
                    break;
                }

                uint8_t dst = bytecode->instructions[offset + 1];
                uint8_t src = bytecode->instructions[offset + 2];
                propagate_register(register_state, dst, src);
                offset += 3;
                handled = true;
                break;
            }

            default:
                // Stop scanning once we encounter an opcode the simple
                // tracker does not understand. This keeps the analysis
                // conservative and avoids corrupting the bytecode stream.
                reset_register_state(register_state);
                offset = bytecode->count;
                break;
        }

        if (!handled) {
            break;
        }
    }

    return optimizations;
}

bool is_load_move_pattern(CompilerContext* ctx, int offset) {
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    if (offset + 7 >= bytecode->count) return false;
    
    uint8_t op1 = bytecode->instructions[offset];
    uint8_t op2 = bytecode->instructions[offset + 4];
    
    // Check for LOAD_CONST followed by MOVE pattern
    return (op1 == 0xAB && op2 == 0xAE); // OP_LOAD_I32_CONST + OP_MOVE_I32
}

bool is_redundant_move(CompilerContext* ctx, int offset) {
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    if (offset + 3 >= bytecode->count) return false;
    
    uint8_t opcode = bytecode->instructions[offset];
    return (opcode == 0xAE); // OP_MOVE_I32
}

void eliminate_instruction_sequence(CompilerContext* ctx, int start_offset, int length) {
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    // Shift remaining instructions left
    for (int j = start_offset; j < bytecode->count - length; j++) {
        bytecode->instructions[j] = bytecode->instructions[j + length];
        bytecode->source_lines[j] = bytecode->source_lines[j + length];
        bytecode->source_columns[j] = bytecode->source_columns[j + length];
        if (bytecode->source_files) {
            bytecode->source_files[j] = bytecode->source_files[j + length];
        }
    }
    bytecode->count -= length;
}

void modify_instruction_register(CompilerContext* ctx, int offset, int reg_field, uint8_t new_reg) {
    BytecodeBuffer* bytecode = ctx->bytecode;
    
    if (offset + reg_field < bytecode->count) {
        bytecode->instructions[offset + reg_field] = new_reg;
    }
}

void print_peephole_statistics(PeepholeContext* ctx) {
    // Suppress unused parameter warning (though ctx is clearly used)
    (void)ctx;
    printf("[PEEPHOLE] ✅ Peephole optimizations: %d patterns optimized\n", ctx->patterns_optimized);
    printf("[PEEPHOLE] 📊 LOAD+MOVE fusions: %d\n", ctx->load_move_fusions);
    printf("[PEEPHOLE] 📊 Redundant moves eliminated: %d\n", ctx->redundant_moves);
    printf("[PEEPHOLE] 📊 Constant propagations: %d\n", ctx->constant_propagations);
    printf("[PEEPHOLE] 📊 Total instructions eliminated: %d\n", ctx->instructions_eliminated);
}

// Register coalescing function (temporary wrapper)
bool apply_register_coalescing(CompilerContext* ctx) {
    // This is now handled within apply_peephole_optimizations
    (void)ctx; // Suppress unused parameter warning
    return true;
}
