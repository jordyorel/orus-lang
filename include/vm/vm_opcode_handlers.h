#ifndef VM_OPCODE_HANDLERS_H
#define VM_OPCODE_HANDLERS_H

#include "vm/vm.h"

// Forward declarations for performance-critical opcode handlers
// These functions are designed to be inlined by the compiler for zero-cost abstraction

// ====== Arithmetic Operation Handlers ======

// I32 arithmetic operations
static inline void handle_add_i32_typed(void);
static inline void handle_sub_i32_typed(void);
static inline void handle_mul_i32_typed(void);
static inline void handle_div_i32_typed(void);
static inline void handle_mod_i32_typed(void);

// I64 arithmetic operations
static inline void handle_add_i64_typed(void);
static inline void handle_sub_i64_typed(void);
static inline void handle_mul_i64_typed(void);
static inline void handle_div_i64_typed(void);
static inline void handle_mod_i64_typed(void);

// U32 arithmetic operations
static inline void handle_add_u32_typed(void);
static inline void handle_sub_u32_typed(void);
static inline void handle_mul_u32_typed(void);
static inline void handle_div_u32_typed(void);
static inline void handle_mod_u32_typed(void);

// U64 arithmetic operations
static inline void handle_add_u64_typed(void);
static inline void handle_sub_u64_typed(void);
static inline void handle_mul_u64_typed(void);
static inline void handle_div_u64_typed(void);
static inline void handle_mod_u64_typed(void);

// F64 arithmetic operations
static inline void handle_add_f64_typed(void);
static inline void handle_sub_f64_typed(void);
static inline void handle_mul_f64_typed(void);
static inline void handle_div_f64_typed(void);
static inline void handle_mod_f64_typed(void);

// ====== Memory Operation Handlers ======

// Constant loading operations
static inline void handle_load_i32_const(void);
static inline void handle_load_i64_const(void);
static inline void handle_load_u32_const(void);
static inline void handle_load_u64_const(void);
static inline void handle_load_f64_const(void);

// Register operations
static inline void handle_move_reg(void);
static inline void handle_load_const(void);
static inline void handle_load_true(void);
static inline void handle_load_false(void);

// Global variable operations
static inline void handle_load_global(void);
static inline void handle_store_global(void);

// Typed move operations
static inline void handle_move_i32(void);
static inline void handle_move_i64(void);
static inline void handle_move_u32(void);
static inline void handle_move_u64(void);
static inline void handle_move_f64(void);

// ====== Control Flow Operation Handlers ======

// Jump operations
static inline void handle_jump_short(void);
static inline void handle_jump_back_short(void);
static inline void handle_jump_if_not_short(void);
static inline void handle_loop_short(void);

// Long jump operations
static inline void handle_jump_long(void);
static inline void handle_jump_if_not_long(void);
static inline void handle_loop_long(void);

// Conditional operations
static inline void handle_jump_if_false(void);
static inline void handle_jump_if_true(void);

// ====== Comparison Operation Handlers ======

// I32 comparison operations
static inline void handle_lt_i32_typed(void);
static inline void handle_le_i32_typed(void);
static inline void handle_gt_i32_typed(void);
static inline void handle_ge_i32_typed(void);
static inline void handle_eq_i32_typed(void);
static inline void handle_ne_i32_typed(void);

// I64 comparison operations
static inline void handle_lt_i64_typed(void);
static inline void handle_le_i64_typed(void);
static inline void handle_gt_i64_typed(void);
static inline void handle_ge_i64_typed(void);
static inline void handle_eq_i64_typed(void);
static inline void handle_ne_i64_typed(void);

// U32 comparison operations
static inline void handle_lt_u32_typed(void);
static inline void handle_le_u32_typed(void);
static inline void handle_gt_u32_typed(void);
static inline void handle_ge_u32_typed(void);
static inline void handle_eq_u32_typed(void);
static inline void handle_ne_u32_typed(void);

// U64 comparison operations
static inline void handle_lt_u64_typed(void);
static inline void handle_le_u64_typed(void);
static inline void handle_gt_u64_typed(void);
static inline void handle_ge_u64_typed(void);
static inline void handle_eq_u64_typed(void);
static inline void handle_ne_u64_typed(void);

// F64 comparison operations
static inline void handle_lt_f64_typed(void);
static inline void handle_le_f64_typed(void);
static inline void handle_gt_f64_typed(void);
static inline void handle_ge_f64_typed(void);
static inline void handle_eq_f64_typed(void);
static inline void handle_ne_f64_typed(void);

// ====== Type Conversion Operation Handlers ======

// I32 conversions
static inline void handle_i32_to_i64(void);
static inline void handle_i32_to_u32(void);
static inline void handle_i32_to_u64(void);
static inline void handle_i32_to_f64(void);
static inline void handle_i32_to_bool(void);
static inline void handle_i32_to_string(void);

// I64 conversions
static inline void handle_i64_to_i32(void);
static inline void handle_i64_to_u32(void);
static inline void handle_i64_to_u64(void);
static inline void handle_i64_to_f64(void);
static inline void handle_i64_to_bool(void);
static inline void handle_i64_to_string(void);

// U32 conversions
static inline void handle_u32_to_i32(void);
static inline void handle_u32_to_i64(void);
static inline void handle_u32_to_u64(void);
static inline void handle_u32_to_f64(void);
static inline void handle_u32_to_bool(void);
static inline void handle_u32_to_string(void);

// U64 conversions
static inline void handle_u64_to_i32(void);
static inline void handle_u64_to_i64(void);
static inline void handle_u64_to_u32(void);
static inline void handle_u64_to_f64(void);
static inline void handle_u64_to_bool(void);
static inline void handle_u64_to_string(void);

// F64 conversions
static inline void handle_f64_to_i32(void);
static inline void handle_f64_to_i64(void);
static inline void handle_f64_to_u32(void);
static inline void handle_f64_to_u64(void);
static inline void handle_f64_to_bool(void);
static inline void handle_f64_to_string(void);

// Bool conversions
static inline void handle_bool_to_i32(void);
static inline void handle_bool_to_i64(void);
static inline void handle_bool_to_u32(void);
static inline void handle_bool_to_u64(void);
static inline void handle_bool_to_f64(void);
static inline void handle_bool_to_string(void);

// ====== String Operation Handlers ======

// String operations
static inline void handle_concat_string(void);
static inline void handle_load_string_const(void);

// ====== Utility Operation Handlers ======

// Print and debug operations
static inline void handle_print(void);
static inline void handle_print_multi(void);
static inline void handle_print_no_nl(void);
static inline void handle_halt(void);
static inline void handle_time_stamp(void);

// Increment/decrement operations
static inline void handle_inc_i32(void);
static inline void handle_dec_i32(void);
static inline void handle_inc_i64(void);
static inline void handle_dec_i64(void);

// Unary operations
static inline void handle_neg_i32(void);
static inline void handle_neg_i64(void);
static inline void handle_neg_f64(void);

// Bitwise operations
static inline void handle_and_i32(void);
static inline void handle_or_i32(void);
static inline void handle_xor_i32(void);
static inline void handle_not_i32(void);
static inline void handle_shl_i32(void);
static inline void handle_shr_i32(void);

// Performance note: All functions are marked static inline to ensure
// the compiler inlines them directly into the dispatch loop, maintaining
// the performance characteristics of the original monolithic implementation.

#endif // VM_OPCODE_HANDLERS_H