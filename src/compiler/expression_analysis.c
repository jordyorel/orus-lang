#include "compiler/expression_analysis.h"
#include "compiler/compiler.h"
#include <stdlib.h>
#include <string.h>

// Type inference implementation
ValueType inferNodeType(ASTNode* node, Compiler* compiler) {
    if (!node) return VAL_NIL;
    
    switch (node->type) {
        case NODE_LITERAL:
            return node->literal.type;
            
        case NODE_IDENTIFIER: {
            // Look up variable type
            for (int i = compiler->localCount - 1; i >= 0; i--) {
                if (compiler->locals[i].active && 
                    strcmp(compiler->locals[i].name, node->identifier.name) == 0) {
                    return compiler->locals[i].type;
                }
            }
            return VAL_NIL; // Unknown identifier
        }
        
        case NODE_BINARY: {
            ValueType leftType = inferNodeType(node->binary.left, compiler);
            ValueType rightType = inferNodeType(node->binary.right, compiler);
            
            const char* op = node->binary.op;
            
            // Arithmetic operations
            if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || 
                strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
                if (leftType == VAL_F64 || rightType == VAL_F64) return VAL_F64;
                if (leftType == VAL_I64 || rightType == VAL_I64) return VAL_I64;
                if (leftType == VAL_U32 || rightType == VAL_U32) return VAL_U32;
                return VAL_I32;
            }
            
            // Comparison operations
            if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0 || strcmp(op, "<=") == 0 ||
                strcmp(op, ">") == 0 || strcmp(op, ">=") == 0) {
                return VAL_BOOL;
            }
            
            // Logical operations
            if (strcmp(op, "and") == 0 || strcmp(op, "or") == 0) {
                return VAL_BOOL;
            }
            
            return leftType; // Default to left operand type
        }
        
        case NODE_CAST:
            return node->cast.targetType;
            
        case NODE_CALL:
            // For now, assume function calls return i32
            // TODO: Implement proper function type inference
            return VAL_I32;
            
        default:
            return VAL_NIL;
    }
}

// Check if expression is compile-time constant
bool isConstantExpression(ASTNode* node) {
    if (!node) return false;
    
    switch (node->type) {
        case NODE_LITERAL:
            return true;
            
        case NODE_BINARY:
            return isConstantExpression(node->binary.left) && 
                   isConstantExpression(node->binary.right);
                   
        case NODE_CAST:
            return isConstantExpression(node->cast.expr);
            
        default:
            return false;
    }
}

// Check if type casting is allowed
bool canCastTypes(ValueType from, ValueType to) {
    // All types can cast to string
    if (to == VAL_STRING) return true;
    
    // String cannot cast to other types
    if (from == VAL_STRING) return false;
    
    // Numeric types can cast between each other
    if ((from == VAL_I32 || from == VAL_I64 || from == VAL_U32 || from == VAL_F64) &&
        (to == VAL_I32 || to == VAL_I64 || to == VAL_U32 || to == VAL_F64)) {
        return true;
    }
    
    // Bool can cast to numeric types
    if (from == VAL_BOOL && 
        (to == VAL_I32 || to == VAL_I64 || to == VAL_U32 || to == VAL_F64)) {
        return true;
    }
    
    // Numeric types can cast to bool
    if ((from == VAL_I32 || from == VAL_I64 || from == VAL_U32 || from == VAL_F64) &&
        to == VAL_BOOL) {
        return true;
    }
    
    return false;
}

// Suggest optimal register usage
RegisterHint suggestRegisterUsage(ASTNode* node, Compiler* compiler) {
    RegisterHint hint = {0};
    hint.preferredRegister = -1; // Let allocator choose
    hint.canShareRegister = false;
    hint.isTemporary = true;
    
    switch (node->type) {
        case NODE_LITERAL:
            hint.canShareRegister = true; // Literals can share
            break;
            
        case NODE_IDENTIFIER:
            hint.isTemporary = false; // Variables are persistent
            break;
            
        case NODE_BINARY:
            hint.isTemporary = true; // Binary ops create temporaries
            break;
            
        case NODE_CAST:
            hint.isTemporary = true; // Casts create new values
            break;
            
        default:
            break;
    }
    
    return hint;
}

// Validate expression safety
SafetyFlags validateExpressionSafety(ASTNode* node, Compiler* compiler) {
    SafetyFlags flags = {0};
    flags.isTypeSafe = true;
    flags.canCast = true;
    flags.hasNullCheck = false;
    flags.isConstExpr = isConstantExpression(node);
    
    if (!node) {
        flags.isTypeSafe = false;
        return flags;
    }
    
    switch (node->type) {
        case NODE_CAST: {
            ValueType fromType = inferNodeType(node->cast.expr, compiler);
            ValueType toType = node->cast.targetType;
            flags.canCast = canCastTypes(fromType, toType);
            flags.isTypeSafe = flags.canCast;
            break;
        }
        
        case NODE_BINARY: {
            ValueType leftType = inferNodeType(node->binary.left, compiler);
            ValueType rightType = inferNodeType(node->binary.right, compiler);
            
            // Check type compatibility for operations
            const char* op = node->binary.op;
            if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 || 
                strcmp(op, "*") == 0 || strcmp(op, "/") == 0) {
                flags.isTypeSafe = (leftType != VAL_STRING && rightType != VAL_STRING);
            }
            break;
        }
        
        default:
            break;
    }
    
    return flags;
}

// Choose optimal backend for expression
Backend chooseOptimalBackend(ASTNode* node, Compiler* compiler) {
    if (!node) return BACKEND_FAST;
    
    // Simple heuristics for now
    switch (node->type) {
        case NODE_LITERAL:
        case NODE_IDENTIFIER:
            return BACKEND_FAST; // Simple expressions
            
        case NODE_BINARY:
            // Complex nested expressions benefit from optimization
            if (node->binary.left->type == NODE_BINARY || 
                node->binary.right->type == NODE_BINARY) {
                return BACKEND_OPTIMIZED;
            }
            return BACKEND_FAST;
            
        case NODE_CAST:
            return BACKEND_FAST; // Casts are straightforward
            
        case NODE_CALL:
            return BACKEND_OPTIMIZED; // Function calls benefit from optimization
            
        default:
            return BACKEND_FAST;
    }
}

// Main analysis function
TypedExpression* analyzeExpression(ASTNode* node, Compiler* compiler) {
    if (!node) return NULL;
    
    TypedExpression* expr = malloc(sizeof(TypedExpression));
    if (!expr) return NULL;
    
    expr->node = node;
    expr->inferredType = inferNodeType(node, compiler);
    expr->safety = validateExpressionSafety(node, compiler);
    expr->regHint = suggestRegisterUsage(node, compiler);
    expr->suggestedBackend = chooseOptimalBackend(node, compiler);
    
    return expr;
}

// Cleanup function
void freeTypedExpression(TypedExpression* expr) {
    if (expr) {
        free(expr);
    }
}

// Backend-specific compilation dispatcher
int compileTypedExpression(TypedExpression* expr, Compiler* compiler, Backend backend) {
    if (!expr || !expr->node) return -1;
    
    // For now, delegate to existing compilers
    // TODO: Implement proper backend dispatch
    switch (backend) {
        case BACKEND_FAST:
            // Use single-pass compilation
            return compileExpr(expr->node, compiler);
            
        case BACKEND_OPTIMIZED:
            // Use multi-pass compilation  
            return compileMultiPassExpr(expr->node, compiler);
            
        case BACKEND_HYBRID:
            // Choose based on expression characteristics
            if (expr->suggestedBackend == BACKEND_OPTIMIZED) {
                return compileMultiPassExpr(expr->node, compiler);
            } else {
                return compileExpr(expr->node, compiler);
            }
            
        default:
            return compileExpr(expr->node, compiler);
    }
}