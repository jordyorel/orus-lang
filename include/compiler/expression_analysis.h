#ifndef EXPRESSION_ANALYSIS_H
#define EXPRESSION_ANALYSIS_H

#include "ast.h"
#include "compiler.h"

// Register allocation hints for VM optimization
typedef struct {
    int preferredRegister;  // Suggested register for this expression
    bool canShareRegister;  // Can reuse parent's register
    bool isTemporary;       // Result is short-lived
} RegisterHint;

// Safety validation flags
typedef struct {
    bool isTypeSafe;        // Type checking passed
    bool canCast;           // Casting is allowed
    bool hasNullCheck;      // Null pointer validation needed
    bool isConstExpr;       // Compile-time constant
} SafetyFlags;

// Backend selection enum
typedef enum {
    BACKEND_FAST,           // Fast compilation (single-pass equivalent)
    BACKEND_OPTIMIZED,      // Optimized compilation (multi-pass equivalent)
    BACKEND_HYBRID          // Smart selection based on characteristics
} Backend;

// Typed expression with analysis results
typedef struct {
    ASTNode* node;          // Original AST node
    ValueType inferredType; // Type inference result
    SafetyFlags safety;     // Safety validation results
    RegisterHint regHint;   // VM register allocation hints
    Backend suggestedBackend; // Recommended compilation backend
} TypedExpression;

// Core analysis functions
TypedExpression* analyzeExpression(ASTNode* node, Compiler* compiler);
void freeTypedExpression(TypedExpression* expr);

// Backend-specific compilation
int compileTypedExpression(TypedExpression* expr, Compiler* compiler, Backend backend);

// Type inference utilities
ValueType inferNodeType(ASTNode* node, Compiler* compiler);
bool isConstantExpression(ASTNode* node);
bool canCastTypes(ValueType from, ValueType to);

// Register optimization utilities
RegisterHint suggestRegisterUsage(ASTNode* node, Compiler* compiler);
Backend chooseOptimalBackend(ASTNode* node, Compiler* compiler);

// Safety validation
SafetyFlags validateExpressionSafety(ASTNode* node, Compiler* compiler);

#endif // EXPRESSION_ANALYSIS_H