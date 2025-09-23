#include "compiler/optimization/constantfold.h"
#include "compiler/typed_ast.h"
#include "compiler/optimization/optimizer.h"
#include "vm/vm.h"
#include "debug/debug_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

// Global context for tracking statistics
static ConstantFoldContext fold_stats;

// Helper to safely recurse into optional child nodes
static void fold_child(TypedASTNode* node) {
    if (node) {
        apply_constant_folding_recursive(node);
    }
}

// Helper to recurse into arrays of child nodes
static void fold_children(TypedASTNode** nodes, int count) {
    if (!nodes || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        if (nodes[i]) {
            apply_constant_folding_recursive(nodes[i]);
        }
    }
}

// Helper to recurse through struct field metadata
static void fold_struct_fields(TypedStructField* fields, int count) {
    if (!fields || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        fold_child(fields[i].typeAnnotation);
        fold_child(fields[i].defaultValue);
    }
}

// Helper to recurse through enum variant field metadata
static void fold_enum_variants(TypedEnumVariant* variants, int count) {
    if (!variants || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        TypedEnumVariant* variant = &variants[i];
        if (!variant->fields || variant->fieldCount <= 0) {
            continue;
        }

        for (int j = 0; j < variant->fieldCount; j++) {
            fold_child(variant->fields[j].typeAnnotation);
        }
    }
}

// Helper to recurse through match expression arms
static void fold_match_arms(TypedMatchArm* arms, int count) {
    if (!arms || count <= 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        TypedMatchArm* arm = &arms[i];
        fold_child(arm->valuePattern);
        fold_child(arm->body);
        fold_child(arm->condition);
        fold_children(arm->payloadAccesses, arm->payloadCount);
    }
}

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

static bool simplify_algebraic_binary_typed(TypedASTNode* node) {
    if (!node || !node->original || node->original->type != NODE_BINARY) {
        return false;
    }

    if (!simplify_algebraic_binary_ast(node->original)) {
        return false;
    }

    node->isConstant = true;
    node->typed.binary.left = NULL;
    node->typed.binary.right = NULL;

    fold_stats.optimizations_applied++;
    fold_stats.constants_folded++;
    fold_stats.binary_expressions_folded++;
    fold_stats.nodes_eliminated++;

    return true;
}

void init_constant_fold_context(ConstantFoldContext* ctx) {
    ctx->optimizations_applied = 0;
    ctx->constants_folded = 0;
    ctx->binary_expressions_folded = 0;
    ctx->nodes_eliminated = 0;
}

bool apply_constant_folding(TypedASTNode* ast, OptimizationContext* opt_ctx) {
    if (!ast) return false;
    
    DEBUG_CONSTANTFOLD_PRINT("🚀 Starting constant folding optimization...\n");
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
            fold_children(ast->typed.program.declarations, ast->typed.program.count);
            break;

        case NODE_FUNCTION:
            fold_child(ast->typed.function.returnType);
            fold_child(ast->typed.function.body);
            break;

        case NODE_BLOCK:
            fold_children(ast->typed.block.statements, ast->typed.block.count);
            break;

        case NODE_VAR_DECL:
            fold_child(ast->typed.varDecl.initializer);
            fold_child(ast->typed.varDecl.typeAnnotation);
            break;

        case NODE_ASSIGN:
            fold_child(ast->typed.assign.value);
            break;

        case NODE_BINARY:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing binary expression: %s\n", ast->original->binary.op);

            // Recursively fold child expressions first
            fold_child(ast->typed.binary.left);
            fold_child(ast->typed.binary.right);

            // Try to fold this binary expression
            bool folded = fold_binary_expression(ast);
            if (!folded) {
                simplify_algebraic_binary_typed(ast);
            }
            break;

        case NODE_UNARY:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing unary expression: %s\n",
                   ast->original->unary.op ? ast->original->unary.op : "unknown");

            // First recursively fold the operand using the typed AST if available
            if (ast->typed.unary.operand) {
                DEBUG_CONSTANTFOLD_PRINT("Recursively folding unary operand via typed AST\n");
                apply_constant_folding_recursive(ast->typed.unary.operand);
            } else if (ast->original->unary.operand) {
                DEBUG_CONSTANTFOLD_PRINT("No typed operand, trying to fold original AST operand directly\n");
                // For cases where typed AST isn't fully populated, work with original AST
                // This is a fallback that handles simple cases
                if (ast->original->unary.operand->type == NODE_BINARY) {
                    ASTNode* binary_operand = ast->original->unary.operand;
                    // Check if both operands of the binary expression are literals
                    if (binary_operand->binary.left && binary_operand->binary.right &&
                        binary_operand->binary.left->type == NODE_LITERAL &&
                        binary_operand->binary.right->type == NODE_LITERAL) {
                        
                        DEBUG_CONSTANTFOLD_PRINT("Found foldable binary operand in unary expression\n");
                        
                        // Evaluate the binary operation directly
                        Value left = binary_operand->binary.left->literal.value;
                        Value right = binary_operand->binary.right->literal.value;
                        const char* op = binary_operand->binary.op;
                        
                        Value binary_result = evaluate_binary_operation(left, op, right);
                        
                        // Transform the binary operand to a literal
                        binary_operand->type = NODE_LITERAL;
                        binary_operand->literal.value = binary_result;
                        binary_operand->literal.hasExplicitSuffix = false;
                        
                        DEBUG_CONSTANTFOLD_PRINT("Folded binary operand to: ");
                        if (binary_result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(binary_result) ? "true" : "false");
                        else if (binary_result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(binary_result));
                        else DEBUG_CONSTANTFOLD_PRINT("(value)");
                        DEBUG_CONSTANTFOLD_PRINT("\n");
                        
                        fold_stats.optimizations_applied++;
                        fold_stats.constants_folded++;
                        fold_stats.binary_expressions_folded++;
                    }
                }
            }
            
            // Try to fold the unary expression if the operand is now a literal
            fold_unary_expression(ast);
            break;
            
        case NODE_PRINT:
            fold_children(ast->typed.print.values, ast->typed.print.count);
            break;

        case NODE_INDEX_ACCESS:
            fold_child(ast->typed.indexAccess.array);
            fold_child(ast->typed.indexAccess.index);
            break;

        case NODE_IF:
            DEBUG_CONSTANTFOLD_PRINT("Analyzing if statement\n");

            // Fold the condition expression using typed AST if available
            if (ast->typed.ifStmt.condition) {
                DEBUG_CONSTANTFOLD_PRINT("Folding if condition via typed AST\n");
                apply_constant_folding_recursive(ast->typed.ifStmt.condition);
            } else if (ast->original->ifStmt.condition) {
                DEBUG_CONSTANTFOLD_PRINT("Folding if condition directly on original AST\n");
                // Apply constant folding directly to the condition expression
                fold_ast_node_directly(ast->original->ifStmt.condition);
            }
            
            // Fold the then branch
            fold_child(ast->typed.ifStmt.thenBranch);

            // Fold the else branch if it exists
            fold_child(ast->typed.ifStmt.elseBranch);
            break;

        case NODE_WHILE:
            fold_child(ast->typed.whileStmt.condition);
            fold_child(ast->typed.whileStmt.body);
            break;

        case NODE_FOR_RANGE:
            fold_child(ast->typed.forRange.start);
            fold_child(ast->typed.forRange.end);
            fold_child(ast->typed.forRange.step);
            fold_child(ast->typed.forRange.body);
            break;

        case NODE_FOR_ITER:
            fold_child(ast->typed.forIter.iterable);
            fold_child(ast->typed.forIter.body);
            break;

        case NODE_RETURN:
            fold_child(ast->typed.returnStmt.value);
            break;

        case NODE_CALL:
            fold_child(ast->typed.call.callee);
            fold_children(ast->typed.call.args, ast->typed.call.argCount);
            break;

        case NODE_THROW:
            fold_child(ast->typed.throwStmt.value);
            break;

        case NODE_ARRAY_LITERAL:
            fold_children(ast->typed.arrayLiteral.elements, ast->typed.arrayLiteral.count);
            break;

        case NODE_ARRAY_ASSIGN:
            fold_child(ast->typed.arrayAssign.target);
            fold_child(ast->typed.arrayAssign.value);
            break;

        case NODE_ARRAY_SLICE:
            fold_child(ast->typed.arraySlice.array);
            fold_child(ast->typed.arraySlice.start);
            fold_child(ast->typed.arraySlice.end);
            break;

        case NODE_TERNARY:
            fold_child(ast->typed.ternary.condition);
            fold_child(ast->typed.ternary.trueExpr);
            fold_child(ast->typed.ternary.falseExpr);
            break;

        case NODE_CAST:
            fold_child(ast->typed.cast.expression);
            fold_child(ast->typed.cast.targetType);
            break;

        case NODE_TRY:
            fold_child(ast->typed.tryStmt.tryBlock);
            fold_child(ast->typed.tryStmt.catchBlock);
            break;

        case NODE_MEMBER_ACCESS:
            fold_child(ast->typed.member.object);
            break;

        case NODE_MEMBER_ASSIGN:
            fold_child(ast->typed.memberAssign.target);
            fold_child(ast->typed.memberAssign.value);
            break;

        case NODE_STRUCT_LITERAL:
            fold_children(ast->typed.structLiteral.values, ast->typed.structLiteral.fieldCount);
            break;

        case NODE_STRUCT_DECL:
            fold_struct_fields(ast->typed.structDecl.fields, ast->typed.structDecl.fieldCount);
            break;

        case NODE_IMPL_BLOCK:
            fold_children(ast->typed.implBlock.methods, ast->typed.implBlock.methodCount);
            break;

        case NODE_ENUM_DECL:
            fold_enum_variants(ast->typed.enumDecl.variants, ast->typed.enumDecl.variantCount);
            break;

        case NODE_ENUM_MATCH_TEST:
            fold_child(ast->typed.enumMatchTest.value);
            break;

        case NODE_ENUM_PAYLOAD:
            fold_child(ast->typed.enumPayload.value);
            break;

        case NODE_ENUM_MATCH_CHECK:
            fold_child(ast->typed.enumMatchCheck.value);
            break;

        case NODE_MATCH_EXPRESSION:
            fold_child(ast->typed.matchExpr.subject);
            fold_match_arms(ast->typed.matchExpr.arms, ast->typed.matchExpr.armCount);
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
    Value result = evaluate_binary_operation(left, op, right);
    DEBUG_CONSTANTFOLD_PRINT("Evaluation completed\n");
    
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
    
    DEBUG_CONSTANTFOLD_PRINT("✅ Successfully folded to: ");
    if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
    else if (result.type == VAL_F64) DEBUG_CONSTANTFOLD_PRINT("%.2f", AS_F64(result));
    else if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
    else DEBUG_CONSTANTFOLD_PRINT("(value)");
    DEBUG_CONSTANTFOLD_PRINT(" (memory-safe transformation)\n");
    
    return true;
}

// Function to apply constant folding directly to AST nodes without typed AST
void fold_ast_node_directly(ASTNode* node) {
    if (!node) return;
    
    switch (node->type) {
        case NODE_BINARY:
            DEBUG_CONSTANTFOLD_PRINT("Folding binary expression directly: %s\n", node->binary.op);
            
            // Recursively fold children first
            fold_ast_node_directly(node->binary.left);
            fold_ast_node_directly(node->binary.right);
            
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
                
                Value result = evaluate_binary_operation(left, op, right);
                
                // Transform this binary node to a literal
                node->type = NODE_LITERAL;
                node->literal.value = result;
                node->literal.hasExplicitSuffix = false;
                
                fold_stats.optimizations_applied++;
                fold_stats.constants_folded++;
                fold_stats.binary_expressions_folded++;

                DEBUG_CONSTANTFOLD_PRINT("Direct folded to: ");
                if (result.type == VAL_BOOL) DEBUG_CONSTANTFOLD_PRINT("%s", AS_BOOL(result) ? "true" : "false");
                else if (result.type == VAL_I32) DEBUG_CONSTANTFOLD_PRINT("%d", AS_I32(result));
                else DEBUG_CONSTANTFOLD_PRINT("(value)");
                DEBUG_CONSTANTFOLD_PRINT("\n");
            } else if (simplify_algebraic_binary_ast(node)) {
                fold_stats.optimizations_applied++;
                fold_stats.constants_folded++;
                fold_stats.binary_expressions_folded++;
                fold_stats.nodes_eliminated++;
            }
            break;
            
        case NODE_UNARY:
            DEBUG_CONSTANTFOLD_PRINT("Folding unary expression directly: %s\n", node->unary.op);
            
            // Recursively fold operand first
            fold_ast_node_directly(node->unary.operand);
            
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
                    
                    fold_stats.optimizations_applied++;
                    fold_stats.constants_folded++;
                    
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

bool fold_unary_expression(TypedASTNode* node) {
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
    fold_stats.optimizations_applied++;
    fold_stats.constants_folded++;
    
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

Value evaluate_binary_operation(Value left, const char* op, Value right) {
    // Arithmetic operations
    if (strcmp(op, "+") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return I32_VAL(AS_I32(left) + AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return F64_VAL(AS_F64(left) + AS_F64(right));
        }
        if (left.type == VAL_STRING && right.type == VAL_STRING) {
            ObjString* leftStr = AS_STRING(left);
            ObjString* rightStr = AS_STRING(right);
            int newLength = leftStr->length + rightStr->length;

            char* buffer = (char*)malloc((size_t)newLength + 1);
            if (!buffer) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Failed to allocate buffer for string folding\n");
                return left;
            }

            memcpy(buffer, leftStr->chars, (size_t)leftStr->length);
            memcpy(buffer + leftStr->length, rightStr->chars, (size_t)rightStr->length);
            buffer[newLength] = '\0';

            ObjString* resultStr = intern_string(buffer, newLength);
            free(buffer);

            if (!resultStr) {
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Failed to intern folded string\n");
                return left;
            }

            return STRING_VAL(resultStr);
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
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Division by zero detected\n");
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
                DEBUG_CONSTANTFOLD_PRINT("⚠️ Modulo by zero detected\n");
                return left; // Return unchanged
            }
            return I32_VAL(AS_I32(left) % AS_I32(right));
        }
    }
    // Logical operations
    else if (strcmp(op, "and") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            return BOOL_VAL(AS_BOOL(left) && AS_BOOL(right));
        }
    }
    else if (strcmp(op, "or") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            return BOOL_VAL(AS_BOOL(left) || AS_BOOL(right));
        }
    }
    // Comparison operations
    else if (strcmp(op, "==") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            return BOOL_VAL(AS_BOOL(left) == AS_BOOL(right));
        }
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) == AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) == AS_F64(right));
        }
    }
    else if (strcmp(op, "!=") == 0) {
        if (left.type == VAL_BOOL && right.type == VAL_BOOL) {
            return BOOL_VAL(AS_BOOL(left) != AS_BOOL(right));
        }
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) != AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) != AS_F64(right));
        }
    }
    else if (strcmp(op, "<") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) < AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) < AS_F64(right));
        }
    }
    else if (strcmp(op, ">") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) > AS_I32(right));
        }  
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) > AS_F64(right));
        }
    }
    else if (strcmp(op, "<=") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) <= AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) <= AS_F64(right));
        }
    }
    else if (strcmp(op, ">=") == 0) {
        if (left.type == VAL_I32 && right.type == VAL_I32) {
            return BOOL_VAL(AS_I32(left) >= AS_I32(right));
        }
        if (left.type == VAL_F64 && right.type == VAL_F64) {
            return BOOL_VAL(AS_F64(left) >= AS_F64(right));
        }
    }
    
    DEBUG_CONSTANTFOLD_PRINT("⚠️ Unsupported operation or type mismatch\n");
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
    (void)ctx; // Suppress unused parameter warning when DEBUG output is disabled
    DEBUG_CONSTANTFOLD_PRINT("\n=== CONSTANT FOLDING OPTIMIZATION STATISTICS ===\n");
    DEBUG_CONSTANTFOLD_PRINT("Total optimizations applied: %d\n", ctx->optimizations_applied);
    DEBUG_CONSTANTFOLD_PRINT("Constants folded: %d\n", ctx->constants_folded);
    DEBUG_CONSTANTFOLD_PRINT("Binary expressions folded: %d\n", ctx->binary_expressions_folded);
    DEBUG_CONSTANTFOLD_PRINT("Nodes eliminated: %d\n", ctx->nodes_eliminated);
    DEBUG_CONSTANTFOLD_PRINT("================================================\n\n");
}
