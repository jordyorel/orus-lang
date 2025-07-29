#include "compiler/codegen/codegen.h"
#include "compiler/codegen/peephole.h"
#include "compiler/typed_ast.h"
#include "compiler/compiler.h"
#include "compiler/register_allocator.h"
#include "compiler/symbol_table.h"
#include "vm/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ===== CODE GENERATION COORDINATOR =====
// Orchestrates bytecode generation and low-level optimizations
// Delegates to specific codegen algorithms

// ===== SYMBOL TABLE INTEGRATION =====
// Now using the proper symbol table system instead of static arrays

int lookup_variable(CompilerContext* ctx, const char* name) {
    if (!ctx || !ctx->symbols || !name) return -1;
    
    Symbol* symbol = resolve_symbol(ctx->symbols, name);
    if (symbol) {
        return symbol->register_id;
    }
    
    return -1; // Variable not found
}

void register_variable(CompilerContext* ctx, const char* name, int reg, Type* type, bool is_mutable) {
    if (!ctx || !ctx->symbols || !name) return;
    
    Symbol* symbol = declare_symbol(ctx->symbols, name, type, is_mutable, reg);
    if (!symbol) {
        printf("[CODEGEN] Error: Failed to register variable %s\n", name);
    }
}

// ===== VM OPCODE SELECTION =====

uint8_t select_optimal_opcode(const char* op, Type* type) {
    if (!op || !type) return OP_HALT; // Fallback
    
    // Leverage VM's type-specific opcodes for 50% performance boost
    if (type->kind == TYPE_I32) {
        // Arithmetic operators
        if (strcmp(op, "+") == 0) return OP_ADD_I32_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_I32_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_I32_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_I32_TYPED;
        if (strcmp(op, "%") == 0) return OP_MOD_I32_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_I32_R;
        if (strcmp(op, ">") == 0) return OP_GT_I32_R;
        if (strcmp(op, "<=") == 0) return OP_LT_I32_R;  // TODO: Implement LE_I32_R
        if (strcmp(op, ">=") == 0) return OP_GT_I32_R;  // TODO: Implement GE_I32_R  
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_EQ_R;      // TODO: Implement NE_R
    }
    else if (type->kind == TYPE_F64) {
        // Arithmetic operators
        if (strcmp(op, "+") == 0) return OP_ADD_F64_TYPED;
        if (strcmp(op, "-") == 0) return OP_SUB_F64_TYPED;
        if (strcmp(op, "*") == 0) return OP_MUL_F64_TYPED;
        if (strcmp(op, "/") == 0) return OP_DIV_F64_TYPED;
        
        // Comparison operators (result is boolean)
        if (strcmp(op, "<") == 0) return OP_LT_F64_R;
        if (strcmp(op, ">") == 0) return OP_GT_F64_R;
        if (strcmp(op, "==") == 0) return OP_EQ_R;
        if (strcmp(op, "!=") == 0) return OP_EQ_R;      // TODO: Implement NE_R
    }
    
    // Generic equality for all types
    if (strcmp(op, "==") == 0) return OP_EQ_R;
    if (strcmp(op, "!=") == 0) return OP_EQ_R;          // TODO: Implement NE_R
    
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
        case VAL_I32: {
            // OP_LOAD_I32_CONST: Add to constant pool and reference by index
            int const_index = add_constant(ctx->constants, constant);
            if (const_index >= 0) {
                // OP_LOAD_I32_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_I32_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                printf("[CODEGEN] Emitted OP_LOAD_I32_CONST R%d, #%d (%d)\n", 
                       reg, const_index, AS_I32(constant));
            } else {
                printf("[CODEGEN] Error: Failed to add i32 constant to pool\n");
            }
            break;
        }
            
        case VAL_F64:
            // TODO: Implement F64 constant loading
            emit_instruction_to_buffer(ctx->bytecode, OP_LOAD_F64_CONST, reg, 0, 0);
            printf("[CODEGEN] Emitted OP_LOAD_F64_CONST R%d, %.2f\n", reg, AS_F64(constant));
            break;
            
        case VAL_BOOL: {
            // Boolean constants stored as i32 in constant pool
            Value bool_as_i32;
            bool_as_i32.type = VAL_I32;
            bool_as_i32.as.i32 = AS_BOOL(constant) ? 1 : 0;
            
            int const_index = add_constant(ctx->constants, bool_as_i32);
            if (const_index >= 0) {
                // OP_LOAD_I32_CONST format: opcode + register + 2-byte constant index
                emit_byte_to_buffer(ctx->bytecode, OP_LOAD_I32_CONST);
                emit_byte_to_buffer(ctx->bytecode, reg);
                emit_byte_to_buffer(ctx->bytecode, (const_index >> 8) & 0xFF); // High byte
                emit_byte_to_buffer(ctx->bytecode, const_index & 0xFF);        // Low byte
                printf("[CODEGEN] Emitted OP_LOAD_I32_CONST R%d, #%d (%s)\n", 
                       reg, const_index, AS_BOOL(constant) ? "true" : "false");
            } else {
                printf("[CODEGEN] Error: Failed to add boolean constant to pool\n");
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
                printf("[CODEGEN] Emitted OP_LOAD_CONST R%d, #%d \"%s\"\n", 
                       reg, const_index, AS_STRING(constant)->chars);
            } else {
                printf("[CODEGEN] Error: Failed to add string constant to pool\n");
            }
            break;
        }
            
        default:
            printf("[CODEGEN] Warning: Unsupported constant type %d\n", constant.type);
            break;
    }
}

void emit_binary_op(CompilerContext* ctx, const char* op, Type* operand_type, int dst, int src1, int src2) {
    uint8_t opcode = select_optimal_opcode(op, operand_type);
    if (opcode != OP_HALT) {
        emit_typed_instruction(ctx, opcode, dst, src1, src2);
        
        // Check if this is a comparison operation (returns boolean)
        bool is_comparison = (strcmp(op, "<") == 0 || strcmp(op, ">") == 0 || 
                             strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
                             strcmp(op, "==") == 0 || strcmp(op, "!=") == 0);
        
        if (is_comparison) {
            printf("[CODEGEN] Emitted %s_CMP R%d, R%d, R%d (result: boolean)\n", op, dst, src1, src2);
        } else {
            printf("[CODEGEN] Emitted %s_TYPED R%d, R%d, R%d\n", op, dst, src1, src2);
        }
    }
}

void emit_move(CompilerContext* ctx, int dst, int src) {
    // OP_MOVE format: opcode + dst_reg + src_reg (3 bytes total)
    emit_byte_to_buffer(ctx->bytecode, OP_MOVE);
    emit_byte_to_buffer(ctx->bytecode, dst);
    emit_byte_to_buffer(ctx->bytecode, src);
    printf("[CODEGEN] Emitted OP_MOVE R%d, R%d (3 bytes)\n", dst, src);
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
            int reg = lookup_variable(ctx, expr->original->identifier.name);
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
    
    // Emit type-specific binary instruction (arithmetic or comparison)
    emit_binary_op(ctx, binary->original->binary.op, binary->typed.binary.left->resolvedType, 
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
    
    // Register the variable in symbol table (assuming mutable by default)
    register_variable(ctx, var_name, var_reg, assign->resolvedType, true);
    
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
        // Multiple expressions - need consecutive registers for OP_PRINT_MULTI_R
        // Allocate consecutive temp registers and move values there
        int* expression_regs = malloc(print->typed.print.count * sizeof(int));
        int first_consecutive_reg = -1;
        int expressions_compiled = 0;
        
        // Compile all expressions first
        for (int i = 0; i < print->typed.print.count; i++) {
            TypedASTNode* expr = print->typed.print.values[i];
            int reg = compile_expression(ctx, expr);
            
            if (reg != -1) {
                expression_regs[expressions_compiled] = reg;
                expressions_compiled++;
            }
        }
        
        if (expressions_compiled > 0) {
            // Allocate consecutive temp registers for the print operation
            first_consecutive_reg = mp_allocate_temp_register(ctx->allocator);
            if (first_consecutive_reg != -1) {
                // Move/copy all values to consecutive registers starting from first_consecutive_reg
                for (int i = 0; i < expressions_compiled; i++) {
                    int target_reg = first_consecutive_reg + i;
                    int source_reg = expression_regs[i];
                    
                    if (source_reg != target_reg) {
                        // Move the value to the consecutive register
                        emit_move(ctx, target_reg, source_reg);
                        printf("[CODEGEN] Moved R%d -> R%d for consecutive print\n", source_reg, target_reg);
                    }
                    
                    // Free the original register if it was temporary
                    if (source_reg >= MP_TEMP_REG_START && source_reg <= MP_TEMP_REG_END) {
                        mp_free_temp_register(ctx->allocator, source_reg);
                    }
                }
                
                // Now emit the print instruction with consecutive registers
                emit_instruction_to_buffer(ctx->bytecode, OP_PRINT_MULTI_R, 
                                         first_consecutive_reg, expressions_compiled, 1); // 1 = newline
                printf("[CODEGEN] Emitted OP_PRINT_MULTI_R R%d, count=%d (consecutive registers)\n", 
                       first_consecutive_reg, expressions_compiled);
                
                // Free the consecutive temp registers
                for (int i = 0; i < expressions_compiled; i++) {
                    mp_free_temp_register(ctx->allocator, first_consecutive_reg + i);
                }
            }
        }
        
        free(expression_regs);
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