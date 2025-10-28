//  Orus Language Project

#include "compiler/optimization/constantfold.h"
#include "compiler/typed_ast.h"
#include "vm/vm.h"
#include "vm/vm_string_ops.h"
#include "debug/debug_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

static bool constant_fold_pre_visit(TypedASTNode* node, void* user_data);
static bool constant_fold_post_visit(TypedASTNode* node, void* user_data);

static bool is_numeric_zero_literal(const ASTNode* node) {
    if (!node || node->type != NODE_LITERAL) {
        return false;
    }

    Value value = node->literal.value;
    switch (value.type) {
        case VAL_I32:
            return AS_I32(value) == 0;
        case VAL_I64:
            return AS_I64(value) == 0;
        case VAL_U32:
            return AS_U32(value) == 0;
        case VAL_U64:
            return AS_U64(value) == 0;
        case VAL_F64:
            return AS_F64(value) == 0.0;
        default:
            return false;
    }
}

static bool is_bool_literal_with_value(const ASTNode* node, bool expected) {
    if (!node || node->type != NODE_LITERAL) {
        return false;
    }

    Value value = node->literal.value;
    return value.type == VAL_BOOL && AS_BOOL(value) == expected;
}

static void copy_literal_value(ASTNode* target, const ASTNode* source_literal) {
    if (!target || !source_literal || source_literal->type != NODE_LITERAL) {
        return;
    }

    target->type = NODE_LITERAL;
    target->literal = source_literal->literal;
    target->literal.hasExplicitSuffix = false;
}

static bool simplify_algebraic_binary_ast(ASTNode* node) {
    if (!node || node->type != NODE_BINARY || !node->binary.op) {
        return false;
    }

    ASTNode* left = node->binary.left;
    ASTNode* right = node->binary.right;
    if (!left || !right) {
        return false;
    }

    if (strcmp(node->binary.op, "*") == 0) {
        const ASTNode* zero_literal = NULL;
        if (is_numeric_zero_literal(left)) {
            zero_literal = left;
        } else if (is_numeric_zero_literal(right)) {
            zero_literal = right;
        }

        if (zero_literal) {
            DEBUG_CONSTANTFOLD_PRINT("Applying algebraic simplification: expr * 0 -> 0\n");
            copy_literal_value(node, zero_literal);
            return true;
        }
    } else if (strcmp(node->binary.op, "and") == 0) {
        const ASTNode* false_literal = NULL;
        if (is_bool_literal_with_value(left, false)) {
            false_literal = left;
        } else if (is_bool_literal_with_value(right, false)) {
            false_literal = right;
        }

        if (false_literal) {
            DEBUG_CONSTANTFOLD_PRINT("Applying algebraic simplification: expr and false -> false\n");
            copy_literal_value(node, false_literal);
            return true;
        }
    } else if (strcmp(node->binary.op, "or") == 0) {
        const ASTNode* true_literal = NULL;
        if (is_bool_literal_with_value(left, true)) {
            true_literal = left;
        } else if (is_bool_literal_with_value(right, true)) {
            true_literal = right;
        }

        if (true_literal) {
            DEBUG_CONSTANTFOLD_PRINT("Applying algebraic simplification: expr or true -> true\n");
            copy_literal_value(node, true_literal);
            return true;
        }
    }

    return false;
}

static bool simplify_algebraic_binary_typed(TypedASTNode* node, ConstantFoldContext* ctx) {
    if (!node || !node->original || node->original->type != NODE_BINARY) {
        return false;
    }

    if (!simplify_algebraic_binary_ast(node->original)) {
        return false;
    }

    node->isConstant = true;
    node->typed.binary.left = NULL;
    node->typed.binary.right = NULL;

    ctx->optimizations_applied++;
    ctx->constants_folded++;
    ctx->binary_expressions_folded++;
    ctx->nodes_eliminated++;

    return true;
}

static bool constant_fold_pre_visit(TypedASTNode* node, void* user_data) {
    (void)user_data;

    if (!node || !node->original) {
        return true;
    }

    switch (node->original->type) {
        case NODE_BINARY:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing binary expression: %s\n",
                   node->original->binary.op ? node->original->binary.op :
                                               "unknown");
            break;
        case NODE_UNARY:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing unary expression: %s\n",
                   node->original->unary.op ? node->original->unary.op :
                                              "unknown");
            break;
        case NODE_IF:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing if statement\n");
            break;
        default:
            break;
    }

    return true;
}

static bool constant_fold_post_visit(TypedASTNode* node, void* user_data) {
    if (!node || !node->original) {
        return true;
    }

    ConstantFoldContext* ctx = (ConstantFoldContext*)user_data;
    ConstantFoldContext local_ctx;
    if (!ctx) {
        init_constant_fold_context(&local_ctx);
        ctx = &local_ctx;
    }

    switch (node->original->type) {
        case NODE_BINARY: {
            bool folded = fold_binary_expression(node, ctx);
            if (!folded) {
                simplify_algebraic_binary_typed(node, ctx);
            }
            break;
        }
        case NODE_UNARY:
            if (!node->typed.unary.operand && node->original->unary.operand) {
                fold_ast_node_directly(node->original->unary.operand, ctx);
            }
            fold_unary_expression(node, ctx);
            break;
        case NODE_IF:
            if (!node->typed.ifStmt.condition &&
                node->original->ifStmt.condition) {
                fold_ast_node_directly(node->original->ifStmt.condition, ctx);
            }
            break;
        default:
            break;
    }

    return true;
}

void init_constant_fold_context(ConstantFoldContext* ctx) {
    ctx->optimizations_applied = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
    ctx->nodes_eliminated = 0;
}

bool apply_constant_folding(TypedASTNode* ast, ConstantFoldContext* ctx) {
    if (!ast) return false;

    DEBUG_CONSTANTFOLD_PRINT("🚀 Starting constant folding optimization...\n");

    ConstantFoldContext local_stats;
    ConstantFoldContext* active_ctx = ctx ? ctx : &local_stats;

    init_constant_fold_context(active_ctx);

    bool result = apply_constant_folding_recursive(ast, active_ctx);

    print_constant_fold_statistics(active_ctx);
    return result;
}

// Main constant folding pass (moved from optimizer.c)
bool apply_constant_folding_recursive(TypedASTNode* ast, ConstantFoldContext* ctx) {
    if (!ast || !ast->original) {
        return true;
    }

    TypedASTVisitor visitor = {
        .pre = constant_fold_pre_visit,
        .post = constant_fold_post_visit,
    };

    return typed_ast_visit(ast, &visitor, ctx);
}

bool fold_binary_expression(TypedASTNode* node, ConstantFoldContext* ctx) {
    if (!is_foldable_binary(node)) {
        return false;
    }
    
    // Get the left and right operand values
    Value left = node->typed.binary.left->original->literal.value;
    Value right = node->typed.binary.right->original->literal.value;
    const char* op = node->original->binary.op;
    
    DEBUG_CONSTANTFOLD_PRINT("Found foldable constants: ");
    // Print values using basic formatting
    if (left.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(left));
    else if (left.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(left));
    else if (left.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(left) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    
    DEBUG_CONSTANTFOLD_PRINT(" %s ", op);
    
    if (right.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(right));
    else if (right.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(right));
    else if (right.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(right) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    DEBUG_CONSTANTFOLD_PRINT("\n");
    
    // Check for overflow before evaluation
    if (has_overflow(left, op, right)) {
        DEBUG_CONSTANTFOLD_PRINT("⚠️ Overflow detected, skipping fold\n");
        return false;
    }
    
    DEBUG_CONSTANTFOLD_PRINT("Evaluating expression...\n");
    Value result;
    if (!evaluate_binary_operation(left, op, right, &result)) {
        DEBUG_CONSTANTFOLD_PRINT("Evaluation failed; leaving expression unchanged\n");
        return false;
    }
    DEBUG_CONSTANTFOLD_PRINT("Evaluation completed\n");
    
    // Transform the original AST node to a literal
    node->original->type = NODE_LITERAL;
    node->original->literal.value = result;
    node->original->literal.hasExplicitSuffix = false;
    
    // Do NOT modify the typed AST structure - leave it intact
    // The codegen will handle folded nodes by checking if original->type is NODE_LITERAL
    
    // Update statistics
    ctx->optimizations_applied++;
    ctx->constants_folded++;
    ctx->binary_expressions_folded++;
    
    DEBUG_CONSTANTFOLD_PRINT("✅ Successfully folded to: ");
    if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
    else if (result.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(result));
    else if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    DEBUG_CONSTANTFOLD_PRINT(" (memory-safe transformation)\n");
    
    return true;
}

// Function to apply constant folding directly to AST nodes without typed AST
void fold_ast_node_directly(ASTNode* node, ConstantFoldContext* ctx) {
    if (!node) return;

    switch (node->type) {
        case NODE_BINARY:
            DEBUG_CONSTANTFOLD_PRINT("Folding binary expression directly: %s\n", node->binary.op);

            // Recursively fold children first
            fold_ast_node_directly(node->binary.left, ctx);
            fold_ast_node_directly(node->binary.right, ctx);
            
            // Try to fold this binary expression
            if (node->binary.left && node->binary.right &&
                node->binary.left->type == NODE_LITERAL &&
                node->binary.right->type == NODE_LITERAL) {

                Value left = node->binary.left->literal.value;
                Value right = node->binary.right->literal.value;
                const char* op = node->binary.op;

                DEBUG_CONSTANTFOLD_PRINT("Direct folding: ");
                if (left.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(left) ? "true" : "false");
                else if (left.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(left));
                else DEBUG_CONSTANTFOLD_PRINT("(value)");
                DEBUG_CONSTANTFOLD_PRINT(" %s ", op);
                if (right.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(right) ? "true" : "false");
                else if (right.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(right));
                else DEBUG_CONSTANTFOLD_PRINT("(value)");
                DEBUG_CONSTANTFOLD_PRINT("\n");
                
                Value result;
                if (evaluate_binary_operation(left, op, right, &result)) {
                    // Transform this binary node to a literal
                    node->type = NODE_LITERAL;
                    node->literal.value = result;
                    node->literal.hasExplicitSuffix = false;

                    ctx->optimizations_applied++;
                    ctx->constants_folded++;
                    ctx->binary_expressions_folded++;

                    DEBUG_CONSTANTFOLD_PRINT("Direct folded to: ");
                    if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
                    else if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
                    else DEBUG_CONSTANTFOLD_PRINT("(value)");
                    DEBUG_CONSTANTFOLD_PRINT("\n");
                }
            }

            if (node->type == NODE_BINARY && simplify_algebraic_binary_ast(node)) {
                ctx->optimizations_applied++;
                ctx->constants_folded++;
                ctx->binary_expressions_folded++;
                ctx->nodes_eliminated++;
            }
            break;

        case NODE_UNARY:
            DEBUG_CONSTANTFOLD_PRINT("Folding unary expression directly: %s\n", node->unary.op);

            // Recursively fold operand first
            fold_ast_node_directly(node->unary.operand, ctx);
            
            // Try to fold this unary expression
            if (node->unary.operand && node->unary.operand->type == NODE_LITERAL) {
                Value operand = node->unary.operand->literal.value;
                const char* op = node->unary.op;
                
                DEBUG_CONSTANTFOLD_PRINT("Direct unary folding: %s ", op);
                if (operand.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(operand) ? "true" : "false");
                else if (operand.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(operand));
                else DEBUG_CONSTANTFOLD_PRINT("(value)");
                DEBUG_CONSTANTFOLD_PRINT("\n");
                
                Value result;
                bool can_fold = false;
                
                if (strcmp(op, "not") == 0 && operand.type == VAL_BOOL) {
                    result.type = VAL_BOOL;
                    result.as.boolean = !AS_BOOL(operand);
                    can_fold = true;
                } else if (strcmp(op, "-") == 0) {
                    if (operand.type == VAL_I32) {
                        result.type = VAL_I32;
                        result.as.i32 = -AS_I32(operand);
                        can_fold = true;
                    }
                } else if (strcmp(op, "+") == 0) {
                    result = operand;
                    can_fold = true;
                }
                
                if (can_fold) {
                    // Transform this unary node to a literal
                    node->type = NODE_LITERAL;
                    node->literal.value = result;
                    node->literal.hasExplicitSuffix = false;
                    
                    ctx->optimizations_applied++;
                    ctx->constants_folded++;
                    
                    DEBUG_CONSTANTFOLD_PRINT("Direct unary folded to: ");
                    if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
                    else if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
                    else DEBUG_CONSTANTFOLD_PRINT("(value)");
                    DEBUG_CONSTANTFOLD_PRINT("\n");
                }
            }
            break;
            
        default:
            // For other node types, don't fold but continue recursively if needed
            break;
    }
}

bool fold_unary_expression(TypedASTNode* node, ConstantFoldContext* ctx) {
    if (!node || !node->original || node->original->type != NODE_UNARY) {
        return false;
    }
    
    // Check if the operand is a literal that can be folded
    if (!node->original->unary.operand || node->original->unary.operand->type != NODE_LITERAL) {
        return false;
    }
    
    Value operand = node->original->unary.operand->literal.value;
    const char* op = node->original->unary.op;
    
    DEBUG_CONSTANTFOLD_PRINT("Found foldable unary constant: %s ", op);
    if (operand.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(operand));
    else if (operand.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(operand));
    else if (operand.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(operand) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    DEBUG_CONSTANTFOLD_PRINT("\n");
    
    Value result;
    
    // Handle different unary operators
    if (strcmp(op, "not") == 0) {
        if (operand.type == VAL_BOOL) {
            result.type = VAL_BOOL;
            result.as.boolean = !AS_BOOL(operand);
        } else {
            DEBUG_CONSTANTFOLD_PRINT("Cannot apply 'not' to non-boolean value\n");
            return false;
        }
    } else if (strcmp(op, "-") == 0) {
        if (operand.type == VAL_I32) {
            result.type = VAL_I32;
            result.as.i32 = -AS_I32(operand);
        } else if (operand.type == VAL_F64) {
            result.type = VAL_F64;
            result.as.f64 = -AS_F64(operand);
        } else {
            DEBUG_CONSTANTFOLD_PRINT("Cannot apply unary minus to non-numeric value\n");
            return false;
        }
    } else if (strcmp(op, "+") == 0) {
        // Unary plus just returns the value unchanged
        result = operand;
    } else {
        DEBUG_CONSTANTFOLD_PRINT("Unknown unary operator: %s\n", op);
        return false;
    }
    
    // Transform the original AST node to a literal
    node->original->type = NODE_LITERAL;
    node->original->literal.value = result;
    node->original->literal.hasExplicitSuffix = false;
    
    // Update statistics
    ctx->optimizations_applied++;
    ctx->constants_folded++;
    
    DEBUG_CONSTANTFOLD_PRINT("✅ Successfully folded unary to: ");
    if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
    else if (result.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(result));
    else if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    DEBUG_CONSTANTFOLD_PRINT(" (memory-safe transformation)\n");
    
    return true;
}

bool is_foldable_binary(TypedASTNode* node) {
    if (!node || node->original->type != NODE_BINARY) {
        DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: Not a binary node\n");
        return false;
    }
    
    // Both operands must be literals
    TypedASTNode* left = node->typed.binary.left;
    TypedASTNode* right = node->typed.binary.right;
    
    DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: left=%p, right=%p\n", (void*)left, (void*)right);
    
    if (!left || !right) {
        DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: Missing operands\n");
        return false;
    }
    
    DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: left->original->type=%d, right->original->type=%d\n", 
           left->original->type, right->original->type);
    DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: NODE_LITERAL=%d, NODE_IDENTIFIER=%d\n", 
           NODE_LITERAL, NODE_IDENTIFIER);
    
    bool is_foldable = left->original->type == NODE_LITERAL && right->original->type == NODE_LITERAL;
    DEBUG_CONSTANTFOLD_PRINT("is_foldable_binary: result=%s\n", is_foldable ? "true" : "false");
    
    return is_foldable;
}

bool evaluate_binary_operation(Value left, const char* op, Value right, Value* out_result) {
    if (!op || !out_result) {
        return false;
    }

    // Arithmetic operations
    if (strcmp(op, "+") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = I32_VAL(AS_I32(left) + AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = F64_VAL(AS_F64(left) + AS_F64(right));
            return true;
        }
        if (left.type == VAL_STRING && right.type == VAL_STRING) {
            ObjString* leftStr = AS_STRING(left);
            ObjString* rightStr = AS_STRING(right);
            int newLength = leftStr->length + rightStr->length;

            char* buffer = (char*)malloc((size_t)newLength + 1);
            if (!buffer) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Failed to allocate buffer for string folding\n");
                return false;
            }

            const char* left_chars = string_get_chars(leftStr);
            const char* right_chars = string_get_chars(rightStr);
            if (!left_chars || !right_chars) {
                free(buffer);
                return false;
            }
            memcpy(buffer, left_chars, (size_t)leftStr->length);
            memcpy(buffer + leftStr->length, right_chars, (size_t)rightStr->length);
            buffer[newLength] = '\0';

            ObjString* resultStr = intern_string(buffer, newLength);
            free(buffer);

            if (!resultStr) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Failed to intern folded string\n");
                return false;
            }

            *out_result = STRING_VAL(resultStr);
            return true;
        }
    }
    else if (strcmp(op, "-") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = I32_VAL(AS_I32(left) - AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = F64_VAL(AS_F64(left) - AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "*") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = I32_VAL(AS_I32(left) * AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = F64_VAL(AS_F64(left) * AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "/") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            if (AS_I32(right) == 0) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Division by zero detected\n");
                return false;
            }
            *out_result = I32_VAL(AS_I32(left) / AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = F64_VAL(AS_F64(left) / AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "%") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            if (AS_I32(right) == 0) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Modulo by zero detected\n");
                return false;
            }
            *out_result = I32_VAL(AS_I32(left) % AS_I32(right));
            return true;
        }
    }
    // Logical operations
    else if (strcmp(op, "and") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            *out_result = BOOL_VAL(AS_BOOL(left) && AS_BOOL(right));
            return true;
        }
    }
    else if (strcmp(op, "or") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            *out_result = BOOL_VAL(AS_BOOL(left) || AS_BOOL(right));
            return true;
        }
    }
    // Comparison operations
    else if (strcmp(op, "==") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            *out_result = BOOL_VAL(AS_BOOL(left) == AS_BOOL(right));
            return true;
        }
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) == AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) == AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "!=") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            *out_result = BOOL_VAL(AS_BOOL(left) != AS_BOOL(right));
            return true;
        }
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) != AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) != AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "<") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) < AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) < AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, ">") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) > AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) > AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, "<=") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) <= AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) <= AS_F64(right));
            return true;
        }
    }
    else if (strcmp(op, ">=") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            *out_result = BOOL_VAL(AS_I32(left) >= AS_I32(right));
            return true;
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            *out_result = BOOL_VAL(AS_F64(left) >= AS_F64(right));
            return true;
        }
    }

    DEBUG_CONSTANTFOLD_PRINT("⚠️ Unsupported operation or type mismatch\n");
    return false;
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
    (void)ctx; // Suppress unused parameter warning when DEBUG output is disabled
    DEBUG_CONSTANTFOLD_PRINT("\n=== CONSTANT FOLDING OPTIMIZATION STATISTICS ===\n");
    DEBUG_CONSTANTFOLD_PRINT("Total optimizations applied: %d\n", ctx->optimizations_applied);
    DEBUG_CONSTANTFOLD_PRINT("Constants folded: %d\n", ctx->constants_folded);
    DEBUG_CONSTANTFOLD_PRINT("Binary expressions folded: %d\n", ctx->binary_expressions_folded);
    DEBUG_CONSTANTFOLD_PRINT("Nodes eliminated: %d\n", ctx->nodes_eliminated);
    DEBUG_CONSTANTFOLD_PRINT("================================================\n\n");
}
