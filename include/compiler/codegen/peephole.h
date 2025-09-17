#ifndef PEEPHOLE_H
#define PEEPHOLE_H

#include "compiler/compiler.h"

// Peephole optimization algorithms for bytecode-level optimizations
// Applies local optimizations to small instruction windows

typedef struct PeepholeContext {
    int patterns_optimized;
    int instructions_eliminated;
    int load_move_fusions;
    int redundant_moves;
    int constant_propagations;
} PeepholeContext;

// Main peephole optimization function
bool apply_peephole_optimizations(CompilerContext* ctx);

// Specific optimization patterns
int optimize_load_move_pattern(CompilerContext* ctx);
int optimize_redundant_operations(CompilerContext* ctx);
int optimize_constant_propagation(CompilerContext* ctx);

// Pattern matching helpers
bool is_load_move_pattern(CompilerContext* ctx, int offset);
bool is_redundant_move(CompilerContext* ctx, int offset);

// Instruction manipulation
void eliminate_instruction_sequence(CompilerContext* ctx, int start_offset, int length);
void modify_instruction_register(CompilerContext* ctx, int offset, int reg_field, uint8_t new_reg);

// Statistics and reporting
void init_peephole_context(PeepholeContext* ctx);
void print_peephole_statistics(PeepholeContext* ctx);

#endif // PEEPHOLE_H