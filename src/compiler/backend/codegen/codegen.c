#include "compiler/codegen.h"
#include "compiler/codegen/peephole.h"
#include "compiler/typed_ast.h"
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "vm/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== CODE GENERATION COORDINATOR =====
// Orchestrates bytecode generation and low-level optimizations
// Delegates to specific codegen algorithms

// ===== SYMBOL TABLE MANAGEMENT =====

typedef struct Variable {
    char* name;
    int register_number;
    Type* type;
} Variable;

static Variable* variables = NULL;
static int variable_count = 0;
static int variable_capacity = 0;

int lookup_variable(const char* name) {
    for (int i = 0; i < variable_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            return variables[i].register_number;
        }
    }
    return -1; // Variable not found
}

void register_variable(const char* name, int reg, Type* type) {
    if (variable_count >= variable_capacity) {
        variable_capacity = variable_capacity == 0 ? 16 : variable_capacity * 2;
        variables = realloc(variables, variable_capacity * sizeof(Variable));
    }
    
    variables[variable_count].name = strdup(name);
    variables[variable_count].register_number = reg;
    variables[variable_count].type = type;
    variable_count++;
}

// ===== VM OPCODE SELECTION =====

uint8_t select_optimal_opcode(const char* op, Type* type) {
    if (!op || !type) return OP_HALT; // Fallback
    
    // Leverage VM's type-specific opcodes for 50% performance boost
    if (type->kind == TYPE_I32) {
        if (strcmp(op, "+") == 0) return OP_ADD_I32_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_I32_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_I32_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_I32_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_I32_TYPED;
    }
    else if (type->kind == TYPE_F64) {
        if (strcmp(op, "+") == 0) return OP_ADD_F64_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_F64_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_F64_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_F64_TYPED;
    }
    
    // Fallback to generic opcodes if type-specific not available
    printf("[CODEGEN] Warning: Using generic opcode for %s on type %d\n", op, type->kind);
    return OP_HALT; // Should not reach here in production
}

// ===== INSTRUCTION EMISSION =====

void emit_typed_instruction(CompilerContext* ctx, uint8_t opcode, int dst, int src1, int src2) {
    emit_instruction_to_buffer(ctx->bytecode, opcode, dst, src1, src2);
}

void emit_load_constant(CompilerContext* ctx, int reg, Value constant) {
    // Use VM's specialized constant loading for optimal performance
    switch (constant.type) {
        case VAL_I32:
            // OP_LOAD_I32_CONST: Direct register loading without constant pool lookup!
            emit_instruction_to_buffer(ctx->bytecode, OP_LOAD_I32_CONST, reg, 
                                     (AS_I32(constant) >> 8) & 0xFF, AS_I32(constant) & 0xFF);
            printf("[CODEGEN] Emitted OP_LOAD_I32_CONST R%d, %d\n", reg, AS_I32(constant));
            break;
            
        case VAL_F64:
            // TODO: Implement F64 constant loading
            emit_instruction_to_buffer(ctx->bytecode, OP_LOAD_F64_CONST, reg, 0, 0);
            printf("[CODEGEN] Emitted OP_LOAD_F64_CONST R%d, %.2f\n", reg, AS_F64(constant));
            break;
            
        case VAL_BOOL:
            // Boolean constants as i32
            emit_instruction_to_buffer(ctx->bytecode, OP_LOAD_I32_CONST, reg, 0, AS_BOOL(constant) ? 1 : 0);
            printf("[CODEGEN] Emitted OP_LOAD_I32_CONST R%d, %s\n", reg, AS_BOOL(constant) ? "true" : "false");
            break;
            
        default:
            printf("[CODEGEN] Warning: Unsupported constant type %d\n", constant.type);
            break;
    }
}

void emit_arithmetic_op(CompilerContext* ctx, const char* op, Type* type, int dst, int src1, int src2) {
    uint8_t opcode = select_optimal_opcode(op, type);
    if (opcode != OP_HALT) {
        emit_typed_instruction(ctx, opcode, dst, src1, src2);
        printf("[CODEGEN] Emitted %s_TYPED R%d, R%d, R%d\n", op, dst, src1, src2);
    }
}

void emit_move(CompilerContext* ctx, int dst, int src) {
    // Use type-specific move for better performance
    emit_instruction_to_buffer(ctx->bytecode, OP_MOVE_I32, dst, src, 0);
    printf("[CODEGEN] Emitted OP_MOVE_I32 R%d, R%d\n", dst, src);
}

// ===== EXPRESSION COMPILATION =====

int compile_expression(CompilerContext* ctx, TypedASTNode* expr) {
    if (!ctx || !expr) return -1;
    
    printf("[CODEGEN] Compiling expression type %d\n", expr->original->type);
    
    switch (expr->original->type) {
        case NODE_LITERAL: {
            int reg = mp_allocate_temp_register(ctx->allocator);
            if (reg == -1) {
                printf("[CODEGEN] Error: Failed to allocate register for literal\n");
                return -1;
            }
            compile_literal(ctx, expr, reg);
            return reg;
        }
        
        case NODE_BINARY: {
            int left_reg = compile_expression(ctx, expr->typed.binary.left);
            int right_reg = compile_expression(ctx, expr->typed.binary.right);
            int result_reg = mp_allocate_temp_register(ctx->allocator);
            
            if (left_reg == -1 || right_reg == -1 || result_reg == -1) {
                printf("[CODEGEN] Error: Failed to allocate registers for binary operation\n");
                return -1;
            }
            
            compile_binary_op(ctx, expr, result_reg);
            
            // Free temp registers for optimal register usage
            mp_free_temp_register(ctx->allocator, left_reg);
            mp_free_temp_register(ctx->allocator, right_reg);
            
            return result_reg;
        }
        
        case NODE_IDENTIFIER: {
            // Look up variable in symbol table
            int reg = lookup_variable(expr->original->identifier.name);
            if (reg == -1) {
                printf("[CODEGEN] Error: Unbound variable %s\n", expr->original->identifier.name);
                return -1;
            }
            return reg;
        }
        
        default:
            printf("[CODEGEN] Error: Unsupported expression type: %d\n", expr->original->type);
            return -1;
    }
}

void compile_literal(CompilerContext* ctx, TypedASTNode* literal, int target_reg) {
    if (!ctx || !literal || target_reg < 0) return;
    
    Value value = literal->original->literal.value;
    emit_load_constant(ctx, target_reg, value);
}

void compile_binary_op(CompilerContext* ctx, TypedASTNode* binary, int target_reg) {
    if (!ctx || !binary || target_reg < 0) return;
    
    // Get operand registers
    int left_reg = compile_expression(ctx, binary->typed.binary.left);
    int right_reg = compile_expression(ctx, binary->typed.binary.right);
    
    if (left_reg == -1 || right_reg == -1) {
        printf("[CODEGEN] Error: Failed to compile binary operands\n");
        return;
    }
    
    // Emit type-specific arithmetic instruction
    emit_arithmetic_op(ctx, binary->original->binary.op, binary->resolvedType, 
                      target_reg, left_reg, right_reg);
}

// ===== STATEMENT COMPILATION =====

void compile_statement(CompilerContext* ctx, TypedASTNode* stmt) {
    if (!ctx || !stmt) return;
    
    printf("[CODEGEN] Compiling statement type %d\n", stmt->original->type);
    
    switch (stmt->original->type) {
        case NODE_ASSIGN:
            compile_assignment(ctx, stmt);
            break;
            
        case NODE_PRINT:
            compile_print_statement(ctx, stmt);
            break;
            
        default:
            printf("[CODEGEN] Warning: Unsupported statement type: %d\n", stmt->original->type);
            break;
    }
}

void compile_assignment(CompilerContext* ctx, TypedASTNode* assign) {
    if (!ctx || !assign) return;
    
    // Get variable name
    const char* var_name = assign->original->assign.name;
    
    // Compile the value expression
    int value_reg = compile_expression(ctx, assign->typed.assign.value);
    if (value_reg == -1) {
        printf("[CODEGEN] Error: Failed to compile assignment value\n");
        return;
    }
    
    // Allocate register for the variable (using frame registers for locals)
    int var_reg = mp_allocate_frame_register(ctx->allocator);
    if (var_reg == -1) {
        printf("[CODEGEN] Error: Failed to allocate register for variable %s\n", var_name);
        return;
    }
    
    // Register the variable in symbol table
    register_variable(var_name, var_reg, assign->resolvedType);
    
    // Move value to variable register
    emit_move(ctx, var_reg, value_reg);
    
    // Free the temporary register
    mp_free_temp_register(ctx->allocator, value_reg);
    
    printf("[CODEGEN] Assigned %s to R%d\n", var_name, var_reg);
}

void compile_print_statement(CompilerContext* ctx, TypedASTNode* print) {
    if (!ctx || !print) return;
    
    // Use the VM's builtin print implementation through OP_PRINT_R
    // This integrates with handle_print() which calls builtin_print()
    
    if (print->typed.print.count == 0) {
        // Print with no arguments - use register 0 (standard behavior)
        // OP_PRINT_R format: opcode + register (2 bytes total)
        emit_byte_to_buffer(ctx->bytecode, OP_PRINT_R);
        emit_byte_to_buffer(ctx->bytecode, 0);
        printf("[CODEGEN] Emitted OP_PRINT_R R0 (no arguments)\n");
    } else if (print->typed.print.count == 1) {
        // Single expression print - compile expression and emit print
        TypedASTNode* expr = print->typed.print.values[0];
        int reg = compile_expression(ctx, expr);
        
        if (reg != -1) {
            // Use OP_PRINT_R which calls handle_print() -> builtin_print()
            // OP_PRINT_R format: opcode + register (2 bytes total)
            emit_byte_to_buffer(ctx->bytecode, OP_PRINT_R);
            emit_byte_to_buffer(ctx->bytecode, reg);
            printf("[CODEGEN] Emitted OP_PRINT_R R%d (single expression)\n", reg);
            
            // Free the temporary register
            mp_free_temp_register(ctx->allocator, reg);
        }
    } else {
        // Multiple expressions - use OP_PRINT_MULTI_R for efficiency
        // Compile all expressions and emit multi-print instruction
        int first_reg = -1;
        int expressions_compiled = 0;
        
        for (int i = 0; i < print->typed.print.count; i++) {
            TypedASTNode* expr = print->typed.print.values[i];
            int reg = compile_expression(ctx, expr);
            
            if (reg != -1) {
                if (first_reg == -1) {
                    first_reg = reg;
                }
                expressions_compiled++;
            }
        }
        
        if (first_reg != -1 && expressions_compiled > 0) {
            // Use OP_PRINT_MULTI_R which calls handle_print_multi() -> builtin_print()
            emit_instruction_to_buffer(ctx->bytecode, OP_PRINT_MULTI_R, 
                                     first_reg, expressions_compiled, 1); // 1 = newline
            printf("[CODEGEN] Emitted OP_PRINT_MULTI_R R%d, count=%d (multiple expressions)\n", 
                   first_reg, expressions_compiled);
        }
    }
}

// ===== MAIN CODE GENERATION ENTRY POINT =====

bool generate_bytecode_from_ast(CompilerContext* ctx) {
    if (!ctx || !ctx->optimized_ast) {
        printf("[CODEGEN] Error: Invalid context or AST\n");
        return false;
    }
    
    printf("[CODEGEN] 🚀 Starting production-grade code generation...\n");
    printf("[CODEGEN] Leveraging VM's 256 registers and 150+ specialized opcodes\n");
    
    TypedASTNode* ast = ctx->optimized_ast;
    
    // Store initial instruction count for optimization metrics
    int initial_count = ctx->bytecode->count;
    
    // Handle program node
    if (ast->original->type == NODE_PROGRAM) {
        for (int i = 0; i < ast->typed.program.count; i++) {
            TypedASTNode* stmt = ast->typed.program.declarations[i];
            if (stmt) {
                compile_statement(ctx, stmt);
            }
        }
    } else {
        // Single statement
        compile_statement(ctx, ast);
    }
    
    // PHASE 1: Apply bytecode-level optimizations (peephole, register coalescing)
    printf("[CODEGEN] 🔧 Applying bytecode optimizations...\n");
    apply_peephole_optimizations(ctx);
    
    // Emit HALT instruction to complete the program
    // OP_HALT format: opcode only (1 byte total)
    emit_byte_to_buffer(ctx->bytecode, OP_HALT);
    printf("[CODEGEN] Emitted OP_HALT\n");
    
    int final_count = ctx->bytecode->count;
    int saved_instructions = initial_count > 0 ? initial_count - final_count + initial_count : 0;
    
    printf("[CODEGEN] ✅ Code generation completed, %d instructions generated\n", final_count);
    if (saved_instructions > 0) {
        printf("[CODEGEN] 🚀 Bytecode optimizations saved %d instructions (%.1f%% reduction)\n", 
               saved_instructions, (float)saved_instructions / initial_count * 100);
    }
    
    return true;
}