// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/vm_opcode_handlers.h
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares grouped opcode handler tables used during dispatch.

#ifndef VM_OPCODE_HANDLERS_H
#define VM_OPCODE_HANDLERS_H

#include "vm/vm.h"

// Forward declarations for performance-critical opcode handlers
// These functions are designed to be inlined by the compiler for zero-cost abstraction

// ====== Arithmetic Operation Handlers ======

// I32 arithmetic operations
void handle_add_i32_typed(void);
void handle_sub_i32_typed(void);
void handle_mul_i32_typed(void);
void handle_div_i32_typed(void);
void handle_mod_i32_typed(void);

// I64 arithmetic operations
void handle_add_i64_typed(void);
void handle_sub_i64_typed(void);
void handle_mul_i64_typed(void);
void handle_div_i64_typed(void);
void handle_mod_i64_typed(void);

// U32 arithmetic operations
void handle_add_u32_typed(void);
void handle_sub_u32_typed(void);
void handle_mul_u32_typed(void);
void handle_div_u32_typed(void);
void handle_mod_u32_typed(void);

// U64 arithmetic operations
void handle_add_u64_typed(void);
void handle_sub_u64_typed(void);
void handle_mul_u64_typed(void);
void handle_div_u64_typed(void);
void handle_mod_u64_typed(void);

// F64 arithmetic operations
void handle_add_f64_typed(void);
void handle_sub_f64_typed(void);
void handle_mul_f64_typed(void);
void handle_div_f64_typed(void);
void handle_mod_f64_typed(void);

// ====== Control Flow Handlers ======

bool handle_jump_short(void);
bool handle_jump_back_short(void);
bool handle_jump_if_not_short(void);
bool handle_loop_short(void);
bool handle_jump_long(void);
bool handle_jump_if_not_long(void);
bool handle_loop_long(void);

// ====== Memory Operation Handlers ======

// Constant loading operations
void handle_load_i32_const(void);
void handle_load_i64_const(void);
void handle_load_u32_const(void);
void handle_load_u64_const(void);
void handle_load_f64_const(void);
void handle_load_const_ext(void);

// Register operations
void handle_move_reg(void);
void handle_load_const(void);
void handle_move_ext(void);
void handle_load_true(void);
void handle_load_false(void);

// Global variable operations
void handle_load_global(void);
void handle_store_global(void);

// Typed move operations
void handle_move_i32(void);
void handle_move_i64(void);
void handle_move_u32(void);
void handle_move_u64(void);
void handle_move_f64(void);

// Print and debug operations
void handle_print(void);
void handle_print_multi(void);
void handle_input(void);
void handle_range(void);
void handle_sorted(void);
void handle_array_repeat(void);
void handle_parse_int(void);
void handle_parse_float(void);
void handle_typeof(void);
void handle_istype(void);
void handle_halt(void);
void handle_timestamp(void);

#endif // VM_OPCODE_HANDLERS_H
