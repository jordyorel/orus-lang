#ifndef CODEGEN_H  
#define CODEGEN_H

#include "compiler/typed_ast.h"
#include "compiler/compiler.h"

// ===== PRODUCTION-READY CODE GENERATION =====
// Leverages VM's 150+ specialized opcodes for optimal performance
// Features:
// - Type-specific instruction selection (OP_ADD_I32_TYPED vs OP_ADD_R)
// - Efficient constant loading (OP_LOAD_I32_CONST vs OP_LOAD_CONST)
// - Optimal register allocation utilizing 256 registers
// - Comprehensive error handling and validation

// Main code generation function
bool generate_bytecode_from_ast(CompilerContext* ctx);

// Expression compilation functions
int compile_expression(CompilerContext* ctx, TypedASTNode* expr);
void compile_statement(CompilerContext* ctx, TypedASTNode* stmt);
void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg);
void compile_binary_op(CompilerContext* ctx, TypedASTNode* binary, int target_reg);

// Instruction emission helpers leveraging VM superpowers
void emit_load_constant(CompilerContext* ctx, int reg, Value constant);
void emit_arithmetic_op(CompilerContext* ctx, const char* op, Type* type, int dst, int src1, int src2);
void emit_move(CompilerContext* ctx, int dst, int src);

// Advanced code generation functions
void compile_assignment(CompilerContext* ctx, TypedASTNode* assign);
void compile_print_statement(CompilerContext* ctx, TypedASTNode* print);
void compile_variable_access(CompilerContext* ctx, TypedASTNode* var, int target_reg);

// VM-specific optimization helpers
void emit_typed_instruction(CompilerContext* ctx, uint8_t opcode, int dst, int src1, int src2);
uint8_t select_optimal_opcode(const char* op, Type* type);
void emit_constant_optimized(CompilerContext* ctx, int reg, Value constant, Type* type);

// Bytecode-level optimizations (applied after initial code generation)
bool apply_peephole_optimizations(CompilerContext* ctx);
bool apply_register_coalescing(CompilerContext* ctx);

#endif // CODEGEN_H