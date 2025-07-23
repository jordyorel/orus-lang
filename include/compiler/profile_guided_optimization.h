#ifndef PROFILE_GUIDED_OPTIMIZATION_H
#define PROFILE_GUIDED_OPTIMIZATION_H

// Forward declarations
typedef struct ASTNode ASTNode;
#include "compiler.h"
#include "backend_selection.h"
#include "vm_optimization.h"
#include "vm/vm_profiling.h"

// Profile-guided optimization decisions
typedef enum {
    PGO_DECISION_NONE = 0,
    PGO_DECISION_INLINE = 1 << 0,        // Inline hot functions
    PGO_DECISION_UNROLL = 1 << 1,        // Unroll hot loops
    PGO_DECISION_OPTIMIZE_BACKEND = 1 << 2, // Use optimized backend
    PGO_DECISION_REGISTER_OPTIMIZE = 1 << 3, // Optimize register allocation
    PGO_DECISION_VECTORIZE = 1 << 4,     // Apply vectorization
    PGO_DECISION_SPECIALIZE = 1 << 5     // Create specialized versions
} PGODecisionFlags;

// Hot path analysis data
typedef struct {
    void* codeAddress;                   // Address in bytecode
    uint64_t executionCount;             // How often executed
    uint64_t totalCycles;                // Total execution time
    double averageCycles;                // Average execution time
    float hotness;                       // Hotness score (0.0-1.0)
    bool isLoop;                         // Is this a loop
    bool isFunction;                     // Is this a function
    int nestingDepth;                    // Nesting level
    double averageIterations;            // Average iterations per execution (for loops)
    PGODecisionFlags decisions;          // Optimization decisions
} HotPathAnalysis;

// Profile-guided optimization context
typedef struct {
    bool isEnabled;                      // PGO enabled
    uint32_t hotPathThreshold;           // Threshold for hot path detection
    uint32_t optimizationLevel;          // Optimization aggressiveness
    double hotnessThreshold;             // Minimum hotness to optimize
    HotPathAnalysis* hotPaths;           // Array of hot paths
    uint32_t hotPathCount;               // Number of hot paths
    uint32_t hotPathCapacity;            // Capacity of hot paths array
    
    // Statistics
    uint32_t functionsOptimized;         // Functions optimized
    uint32_t loopsOptimized;             // Loops optimized
    uint32_t inliningDecisions;          // Inlining decisions made
    uint32_t backendSwitches;            // Backend switches made
} PGOContext;

// Global PGO context
extern PGOContext g_pgoContext;

// Core PGO functions
void initProfileGuidedOptimization(void);
void shutdownProfileGuidedOptimization(void);
void resetPGOContext(void);

// Hot path analysis
HotPathAnalysis* analyzeHotPath(ASTNode* node, void* codeAddress);
float calculateHotness(uint64_t executionCount, uint64_t totalCycles, double averageCycles);
bool isPGOHotPath(HotPathAnalysis* analysis);
bool shouldOptimizeNode(ASTNode* node, HotPathAnalysis* analysis);

// PGO decision making
PGODecisionFlags makePGODecisions(ASTNode* node, HotPathAnalysis* analysis, CompilerBackend currentBackend);
CompilerBackend choosePGOBackend(ASTNode* node, HotPathAnalysis* analysis, CompilerBackend defaultBackend);
bool shouldInlineFunction(ASTNode* functionNode, HotPathAnalysis* analysis);
bool shouldUnrollLoop(ASTNode* loopNode, HotPathAnalysis* analysis);
int calculateUnrollFactor(ASTNode* loopNode, HotPathAnalysis* analysis);

// Integration with existing systems
void updateHotPathFromProfiling(void);
void applyPGOToCompilation(ASTNode* node, Compiler* compiler);
void integrateWithBackendSelection(CompilationContext* ctx, HotPathAnalysis* analysis);
void integrateWithVMOptimization(VMOptimizationContext* vmCtx, HotPathAnalysis* analysis);

// Hot path recompilation (Phase 4 advanced feature)
typedef struct {
    void* originalCode;                  // Original bytecode
    void* optimizedCode;                 // Optimized version
    uint32_t originalLength;             // Original code length
    uint32_t optimizedLength;            // Optimized code length
    bool isActive;                       // Is optimized version active
    uint64_t recompileCount;             // Number of recompilations
} RecompiledCode;

bool shouldRecompile(HotPathAnalysis* analysis);
RecompiledCode* recompileHotPath(ASTNode* node, HotPathAnalysis* analysis, Compiler* compiler);
void activateOptimizedCode(RecompiledCode* recompiled);

// Specialization (create optimized versions for common cases)
typedef struct {
    ASTNode* originalNode;               // Original AST node
    ASTNode* specializedNode;            // Specialized version
    void* conditionCheck;                // Code to check if specialization applies
    uint64_t specializationHits;         // How often specialization used
    uint64_t specializationMisses;       // How often fallback used
} SpecializedVersion;

SpecializedVersion* createSpecialization(ASTNode* node, HotPathAnalysis* analysis);
bool shouldUseSpecialization(SpecializedVersion* spec, void* runtimeContext);

// Cross-function optimization
typedef struct {
    ASTNode** functions;                 // Functions in hot call chain
    uint32_t functionCount;              // Number of functions
    HotPathAnalysis* callChainAnalysis;  // Analysis of the call chain
    bool canOptimizeTogether;            // Can optimize as unit
} CallChainAnalysis;

CallChainAnalysis* analyzeCallChain(ASTNode* rootFunction);
void optimizeCallChain(CallChainAnalysis* chain, Compiler* compiler);

// Advanced loop optimizations for hot paths
typedef struct {
    bool enableUnrolling;                // Loop unrolling
    bool enableVectorization;            // SIMD-style vectorization
    bool enableInvariantHoisting;        // Move invariant code out
    bool enableStrengthReduction;        // Reduce operation strength
    bool enableInductionVarOptim;        // Optimize induction variables
    int unrollFactor;                    // How much to unroll
    int vectorWidth;                     // Vectorization width
} AdvancedLoopOptimizations;

AdvancedLoopOptimizations getAdvancedLoopOptimizations(ASTNode* loopNode, HotPathAnalysis* analysis);
void applyAdvancedLoopOptimizations(ASTNode* loopNode, AdvancedLoopOptimizations* opts, Compiler* compiler);

// Statistics and reporting
void printPGOStatistics(void);
void exportPGOData(const char* filename);
void printHotPathReport(void);
void printOptimizationDecisions(void);

// Configuration
void setPGOThreshold(uint32_t threshold);
void setPGOOptimizationLevel(uint32_t level);
void setHotnessThreshold(double threshold);
void enablePGOFeature(PGODecisionFlags feature);
void disablePGOFeature(PGODecisionFlags feature);

#endif // PROFILE_GUIDED_OPTIMIZATION_H