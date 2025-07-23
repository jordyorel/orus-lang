#ifndef BACKEND_SELECTION_H
#define BACKEND_SELECTION_H

#include "ast.h"
#include "compiler.h"
#include "vm/vm.h"

// Backend types
typedef enum {
    BACKEND_FAST,           // Fast compilation - single-pass equivalent
    BACKEND_OPTIMIZED,      // Optimized compilation - multi-pass equivalent  
    BACKEND_HYBRID,         // Smart selection based on characteristics
    BACKEND_AUTO            // Automatic selection based on context
} CompilerBackend;

// Compilation context for smart decisions
typedef struct {
    bool isDebugMode;           // Debug vs release build
    bool isHotPath;             // Is this a frequently executed path
    int functionCallDepth;      // Current function nesting depth
    int loopNestingDepth;       // Current loop nesting depth
    int expressionComplexity;   // Complexity score of current expression
    bool hasBreakContinue;      // Contains break/continue statements
    bool hasComplexTypes;       // Uses complex type operations
    size_t codeSize;           // Estimated code size
} CompilationContext;

// Unified code complexity structure (merging CodeAnalysisResult and CodeComplexity)
typedef struct {
    int loopCount;
    int nestedLoopDepth;
    int functionCount;
    int callCount;
    int complexExpressionCount;
    bool hasBreakContinue;
    bool hasComplexArithmetic;
    float complexityScore; // Unified score for backend selection
} CodeComplexity;

// Core backend selection functions
CompilerBackend chooseOptimalBackend(ASTNode* node, CompilationContext* ctx);
CodeComplexity analyzeCodeComplexity(ASTNode* node);
void initCompilationContext(CompilationContext* ctx, bool debugMode);
void updateCompilationContext(CompilationContext* ctx, ASTNode* node);

// Code analysis heuristics
bool isSimpleExpression(ASTNode* node);
bool isComplexLoop(ASTNode* node);
bool hasOptimizationOpportunities(ASTNode* node);
bool shouldUseOptimizedBackend(CodeComplexity* analysis, CompilationContext* ctx);
float calculateOptimizationBenefit(ASTNode* node);

// Hot path detection (Phase 4 preparation)
typedef struct {
    const char* functionName;
    int executionCount;
    double averageTime;
    bool isHotPath;
} ProfileData;

bool isCompilationHotPath(ASTNode* node, ProfileData* profile);
void updateProfileData(const char* functionName, double executionTime);
void applyPGOToCompilationContext(CompilationContext* ctx, ASTNode* node);

// VM-specific optimizations hints
typedef struct {
    bool preferRegisterReuse;   // Reuse registers when possible
    bool minimizeSpilling;      // Avoid register spilling
    bool optimizeForSpeed;      // Speed vs size tradeoff
    int targetRegisterCount;    // Preferred number of registers to use
} VMOptimizationHints;

VMOptimizationHints getVMOptimizationHints(CompilerBackend backend);

#endif // BACKEND_SELECTION_H