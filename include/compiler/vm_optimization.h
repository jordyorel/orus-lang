#ifndef VM_OPTIMIZATION_H
#define VM_OPTIMIZATION_H

#include "ast.h"
#include "compiler.h"
#include "backend_selection.h"

// VM-specific optimization context
typedef struct {
    int targetRegisterCount;    // Optimal register count for this code
    bool enableRegisterReuse;   // Enable aggressive register reuse
    bool optimizeForSpeed;      // Speed vs code size tradeoff
    bool enableComputedGoto;    // Use computed-goto dispatch hints
    float registerPressure;     // Current register pressure (0.0-1.0)
    int spillThreshold;         // When to start spilling registers
} VMOptimizationContext;

// Register allocation state for 256-register VM
typedef struct {
    int liveRegisters[256];     // Which registers are currently live
    int spillCost[256];         // Cost of spilling each register
    bool isLoopVariable[256];   // Is register used for loop variable
    int useCount[256];          // How often each register is used
    int lastUse[256];           // Last instruction that used this register
    bool isPinned[256];         // Register cannot be reallocated
    int availableRegisters;     // Number of available registers
    int highWaterMark;          // Maximum registers used simultaneously
} RegisterState;

// Instruction optimization hints
typedef struct {
    bool preferInlineOp;        // Use inline operation instead of call
    bool enableConstFolding;    // Enable constant folding
    bool enableDeadCodeElim;    // Enable dead code elimination
    int loopUnrollFactor;       // How much to unroll loops (0 = no unroll)
    bool enableBranchPrediction; // Add branch prediction hints
} InstructionOptHints;

// Core VM optimization functions
VMOptimizationContext createVMOptimizationContext(CompilerBackend backend);
void optimizeForRegisterVM(ASTNode* node, VMOptimizationContext* vmCtx, 
                          RegisterState* regState, Compiler* compiler);

// Register allocation optimization
void initRegisterState(RegisterState* regState);
int allocateOptimalRegister(RegisterState* regState, VMOptimizationContext* vmCtx, 
                           bool isLoopVar, int estimatedLifetime);
void freeOptimizedRegister(RegisterState* regState, int reg);
void optimizeRegisterUsage(ASTNode* node, RegisterState* regState);
float calculateRegisterPressure(RegisterState* regState);

// VM dispatch optimizations  
InstructionOptHints getInstructionOptimizations(ASTNode* node, VMOptimizationContext* vmCtx);
void emitOptimizedInstruction(int opcode, VMOptimizationContext* vmCtx, 
                             InstructionOptHints* hints, Compiler* compiler);

// Loop-specific VM optimizations
typedef struct {
    bool enableLoopUnrolling;   // Unroll small loops
    bool enableInvariantHoisting; // Hoist loop-invariant code
    bool optimizeInductionVars; // Optimize induction variables
    int maxUnrollIterations;    // Maximum iterations to unroll
    bool enableVectorization;   // Use SIMD-like optimizations where possible
} LoopOptimizationHints;

LoopOptimizationHints getLoopOptimizations(ASTNode* loopNode, VMOptimizationContext* vmCtx);
void applyLoopOptimizations(ASTNode* loopNode, LoopOptimizationHints* hints, 
                           Compiler* compiler);

// Memory layout optimizations for VM
typedef struct {
    int constantPoolSize;       // Size of constant pool
    int localVarFrameSize;      // Size of local variable frame
    bool enableConstantPooling; // Pool constants to reduce memory usage
    bool alignForCache;         // Align data for cache efficiency
} MemoryLayoutHints;

MemoryLayoutHints getMemoryLayoutOptimizations(ASTNode* node, VMOptimizationContext* vmCtx);

// Hot path specific optimizations
void markHotPath(ASTNode* node, RegisterState* regState);
void optimizeHotPath(ASTNode* node, VMOptimizationContext* vmCtx, Compiler* compiler);

// Debug and profiling helpers
void dumpRegisterState(RegisterState* regState);
void dumpVMOptimizationContext(VMOptimizationContext* vmCtx);
void validateRegisterAllocation(RegisterState* regState);

#endif // VM_OPTIMIZATION_H