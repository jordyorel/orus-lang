#include "compiler/optimization/constantfold.h"
#include "compiler/typed_ast.h"
#include "compiler/optimization/optimizer.h"
#include "vm/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

// Global context for tracking statistics
static ConstantFoldContext fold_stats;

void init_constant_fold_context(ConstantFoldContext* ctx) {
    ctx->optimizations_applied = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
    ctx->nodes_eliminated = 0;
}

bool apply_constant_folding(TypedASTNode* ast, OptimizationContext* opt_ctx) {
    if (!ast) return false;
    
    printf("[CONSTANTFOLD] 🚀 Starting constant folding optimization...\n");
    init_constant_fold_context(&fold_stats);
    
    bool result = apply_constant_folding_recursive(ast);
    
    // Update optimization context statistics
    if (opt_ctx) {
        opt_ctx->optimizations_applied += fold_stats.optimizations_applied;
        opt_ctx->constants_folded += fold_stats.constants_folded;
        opt_ctx->nodes_eliminated += fold_stats.nodes_eliminated;
    }
    
    print_constant_fold_statistics(&fold_stats);
    return result;
}

// Main constant folding pass (moved from optimizer.c)
bool apply_constant_folding_recursive(TypedASTNode* ast) {
    if (!ast || !ast->original) return true;
    
    switch (ast->original->type) {
        case NODE_PROGRAM:
            if (ast->typed.program.declarations) {
                for (int i = 0; i < ast->typed.program.count; i++) {
                    if (ast->typed.program.declarations[i]) {
                        apply_constant_folding_recursive(ast->typed.program.declarations[i]);
                    }
                }
            }
            break;
            
        case NODE_ASSIGN:
            if (ast->typed.assign.value) {
                apply_constant_folding_recursive(ast->typed.assign.value);
            }
            break;
            
        case NODE_BINARY:
            printf("[CONSTANTFOLD] Analyzing binary expression: %s\n", ast->original->binary.op);
            
            // Recursively fold child expressions first
            if (ast->typed.binary.left) {
                apply_constant_folding_recursive(ast->typed.binary.left);
            }
            if (ast->typed.binary.right) {
                apply_constant_folding_recursive(ast->typed.binary.right);
            }
            
            // Try to fold this binary expression
            fold_binary_expression(ast);
            break;
            
        case NODE_PRINT:
            // Print nodes don't have child expressions to fold in current implementation
            break;
            
        default:
            // No folding needed for other node types
            break;
    }
    
    return true;
}

bool fold_binary_expression(TypedASTNode* node) {
    if (!is_foldable_binary(node)) {
        return false;
    }
    
    // Get the left and right operand values
    Value left = node->typed.binary.left->original->literal.value;
    Value right = node->typed.binary.right->original->literal.value;
    const char* op = node->original->binary.op;
    
    printf("[CONSTANTFOLD] Found foldable constants: ");
    // Print values using basic formatting
    if (left.type == VAL_I32) printf("%d", AS_I32(left));
    else if (left.type == VAL_F64) printf("%.2f", AS_F64(left));
    else printf("(value)");
    
    printf(" %s ", op);
    
    if (right.type == VAL_I32) printf("%d", AS_I32(right));
    else if (right.type == VAL_F64) printf("%.2f", AS_F64(right));
    else printf("(value)");
    printf("\n");
    
    // Check for overflow before evaluation
    if (has_overflow(left, op, right)) {
        printf("[CONSTANTFOLD] ⚠️ Overflow detected, skipping fold\n");
        return false;
    }
    
    printf("[CONSTANTFOLD] Evaluating expression...\n");
    Value result = evaluate_binary_operation(left, op, right);
    printf("[CONSTANTFOLD] Evaluation completed\n");
    
    // Transform the original AST node to a literal
    node->original->type = NODE_LITERAL;
    node->original->literal.value = result;
    node->original->literal.hasExplicitSuffix = false;
    
    // Do NOT modify the typed AST structure - leave it intact
    // The codegen will handle folded nodes by checking if original->type is NODE_LITERAL
    
    // Update statistics
    fold_stats.optimizations_applied++;
    fold_stats.constants_folded++;
    fold_stats.binary_expressions_folded++;
    
    printf("[CONSTANTFOLD] ✅ Successfully folded to: ");
    if (result.type == VAL_I32) printf("%d", AS_I32(result));
    else if (result.type == VAL_F64) printf("%.2f", AS_F64(result));
    else printf("(value)");
    printf(" (memory-safe transformation)\n");
    
    return true;
}

bool is_foldable_binary(TypedASTNode* node) {
    if (!node || node->original->type != NODE_BINARY) {
        printf("[CONSTANTFOLD] is_foldable_binary: Not a binary node\n");
        return false;
    }
    
    // Both operands must be literals
    TypedASTNode* left = node->typed.binary.left;
    TypedASTNode* right = node->typed.binary.right;
    
    printf("[CONSTANTFOLD] is_foldable_binary: left=%p, right=%p\n", (void*)left, (void*)right);
    
    if (!left || !right) {
        printf("[CONSTANTFOLD] is_foldable_binary: Missing operands\n");
        return false;
    }
    
    printf("[CONSTANTFOLD] is_foldable_binary: left->original->type=%d, right->original->type=%d\n", 
           left->original->type, right->original->type);
    printf("[CONSTANTFOLD] is_foldable_binary: NODE_LITERAL=%d, NODE_IDENTIFIER=%d\n", 
           NODE_LITERAL, NODE_IDENTIFIER);
    
    bool is_foldable = left->original->type == NODE_LITERAL && right->original->type == NODE_LITERAL;
    printf("[CONSTANTFOLD] is_foldable_binary: result=%s\n", is_foldable ? "true" : "false");
    
    return is_foldable;
}

Value evaluate_binary_operation(Value left, const char* op, Value right) {
    // Arithmetic operations
    if (strcmp(op, "+") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return I32_VAL(AS_I32(left) + AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return F64_VAL(AS_F64(left) + AS_F64(right));
        }
    }
    else if (strcmp(op, "-") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return I32_VAL(AS_I32(left) - AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return F64_VAL(AS_F64(left) - AS_F64(right));
        }
    }
    else if (strcmp(op, "*") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return I32_VAL(AS_I32(left) * AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return F64_VAL(AS_F64(left) * AS_F64(right));
        }
    }
    else if (strcmp(op, "/") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            if (AS_I32(right) == 0) {
                printf("[CONSTANTFOLD] ⚠️ Division by zero detected\n");
                return left; // Return unchanged
            }
            return I32_VAL(AS_I32(left) / AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return F64_VAL(AS_F64(left) / AS_F64(right));
        }
    }
    else if (strcmp(op, "%") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            if (AS_I32(right) == 0) {
                printf("[CONSTANTFOLD] ⚠️ Modulo by zero detected\n");
                return left; // Return unchanged
            }
            return I32_VAL(AS_I32(left) % AS_I32(right));
        }
    }
    
    printf("[CONSTANTFOLD] ⚠️ Unsupported operation or type mismatch\n");
    return left; // Return left operand unchanged
}

bool has_overflow(Value left, const char* op, Value right) {
    if (left.type != VAL_I32 || right.type != VAL_I32) {
        return false; // Only check overflow for i32
    }
    
    int32_t a = AS_I32(left);
    int32_t b = AS_I32(right);
    
    if (strcmp(op, "+") == 0) {
        // Check for addition overflow
        if (b > 0 && a > INT_MAX - b) return true;
        if (b < 0 && a < INT_MIN - b) return true;
    }
    else if (strcmp(op, "-") == 0) {
        // Check for subtraction overflow
        if (b < 0 && a > INT_MAX + b) return true;
        if (b > 0 && a < INT_MIN + b) return true;
    }
    else if (strcmp(op, "*") == 0) {
        // Check for multiplication overflow
        if (a > 0 && b > 0 && a > INT_MAX / b) return true;
        if (a > 0 && b < 0 && b < INT_MIN / a) return true;
        if (a < 0 && b > 0 && a < INT_MIN / b) return true;
        if (a < 0 && b < 0 && a < INT_MAX / b) return true;
    }
    
    return false;
}

void print_constant_fold_statistics(ConstantFoldContext* ctx) {
    printf("\n=== CONSTANT FOLDING OPTIMIZATION STATISTICS ===\n");
    printf("Total optimizations applied: %d\n", ctx->optimizations_applied);
    printf("Constants folded: %d\n", ctx->constants_folded);
    printf("Binary expressions folded: %d\n", ctx->binary_expressions_folded);
    printf("Nodes eliminated: %d\n", ctx->nodes_eliminated);
    printf("================================================\n\n");
}