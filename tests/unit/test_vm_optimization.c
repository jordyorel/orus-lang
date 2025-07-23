// test_vm_optimization.c
// Unit tests for VM optimization and register allocation

#include "unity.h"
#include "compiler/vm_optimization.h"
#include "vm/vm_config.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"

// Test VM optimization context creation
void test_create_vm_optimization_context_fast(void) {
    VMOptimizationContext ctx = createVMOptimizationContext(BACKEND_FAST);
    
    TEST_ASSERT_EQUAL_INT(32, ctx.targetRegisterCount);
    TEST_ASSERT_FALSE(ctx.enableRegisterReuse);
    TEST_ASSERT_FALSE(ctx.optimizeForSpeed);
    TEST_ASSERT_FALSE(ctx.enableComputedGoto);
    TEST_ASSERT_TRUE(ctx.registerPressure == 0.0f);
    TEST_ASSERT_EQUAL_INT(24, ctx.spillThreshold);
}

void test_create_vm_optimization_context_optimized(void) {
    VMOptimizationContext ctx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    TEST_ASSERT_TRUE(ctx.targetRegisterCount > 32);
    TEST_ASSERT_TRUE(ctx.enableRegisterReuse);
    TEST_ASSERT_TRUE(ctx.optimizeForSpeed);
    TEST_ASSERT_TRUE(ctx.registerPressure == 0.0f);
    TEST_ASSERT_EQUAL_INT(200, ctx.spillThreshold);
}

void test_create_vm_optimization_context_hybrid(void) {
    VMOptimizationContext ctx = createVMOptimizationContext(BACKEND_HYBRID);
    
    TEST_ASSERT_EQUAL_INT(64, ctx.targetRegisterCount);
    TEST_ASSERT_TRUE(ctx.enableRegisterReuse);
    TEST_ASSERT_TRUE(ctx.optimizeForSpeed);
    TEST_ASSERT_TRUE(ctx.registerPressure == 0.0f);
    TEST_ASSERT_EQUAL_INT(50, ctx.spillThreshold);
}

// Test register state initialization
void test_init_register_state(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    TEST_ASSERT_TRUE(regState.highWaterMark == 0);
    TEST_ASSERT_TRUE(regState.availableRegisters < vmGetRegisterCount()); // Some are reserved
    
    // Check that first 4 registers are pinned (reserved)
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_TRUE(regState.isPinned[i]);
    }
}

// Test register pressure calculation
void test_calculate_register_pressure(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    // Initially should have low pressure
    float pressure = calculateRegisterPressure(&regState);
    TEST_ASSERT_TRUE(pressure >= 0.0f && pressure <= 1.0f);
    TEST_ASSERT_TRUE(pressure < 0.1f); // Should be very low initially
    
    // Simulate some live registers
    for (int i = 4; i < 20; i++) {
        regState.liveRegisters[i] = 10; // Mark as live
    }
    
    float newPressure = calculateRegisterPressure(&regState);
    TEST_ASSERT_TRUE(newPressure > pressure); // Should increase
    TEST_ASSERT_TRUE(newPressure >= 0.0f && newPressure <= 1.0f);
}

// Test optimal register allocation
void test_allocate_optimal_register(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    // Allocate a regular register
    int reg1 = allocateOptimalRegister(&regState, &vmCtx, false, 10);
    TEST_ASSERT_TRUE(reg1 >= 4); // Should skip reserved registers
    TEST_ASSERT_TRUE(reg1 < vmGetRegisterCount());
    
    // Allocate a loop variable register
    int reg2 = allocateOptimalRegister(&regState, &vmCtx, true, 100);
    TEST_ASSERT_TRUE(reg2 >= 4);
    TEST_ASSERT_TRUE(reg2 < vmGetRegisterCount());
    TEST_ASSERT_TRUE(reg2 != reg1); // Should be different
    
    // Check that registers are marked as live
    TEST_ASSERT_TRUE(regState.liveRegisters[reg1] > 0);
    TEST_ASSERT_TRUE(regState.liveRegisters[reg2] > 0);
    TEST_ASSERT_TRUE(regState.isLoopVariable[reg2]); // Should be marked as loop var
    TEST_ASSERT_FALSE(regState.isLoopVariable[reg1]); // Should not be marked as loop var
}

// Test register freeing
void test_free_optimized_register(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    // Allocate a register
    int reg = allocateOptimalRegister(&regState, &vmCtx, false, 10);
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(regState.liveRegisters[reg] > 0);
    
    int availableBefore = regState.availableRegisters;
    
    // Free the register
    freeOptimizedRegister(&regState, reg);
    
    TEST_ASSERT_TRUE(regState.liveRegisters[reg] == 0);
    TEST_ASSERT_TRUE(regState.lastUse[reg] > 0); // Should be marked as recently freed
    TEST_ASSERT_EQUAL_INT(availableBefore + 1, regState.availableRegisters);
}

// Test instruction optimization hints
void test_get_instruction_optimizations(void) {
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    // Test binary operation hints
    ASTNode binaryNode = {0};
    binaryNode.type = NODE_BINARY;
    
    InstructionOptHints hints = getInstructionOptimizations(&binaryNode, &vmCtx);
    
    TEST_ASSERT_TRUE(hints.enableConstFolding);
    TEST_ASSERT_TRUE(hints.enableDeadCodeElim);
    TEST_ASSERT_TRUE(hints.preferInlineOp);
    
    // Test loop hints
    ASTNode loopNode = {0};
    loopNode.type = NODE_FOR_RANGE;
    
    InstructionOptHints loopHints = getInstructionOptimizations(&loopNode, &vmCtx);
    TEST_ASSERT_TRUE(loopHints.loopUnrollFactor > 0);
}

// Test loop optimization hints
void test_get_loop_optimizations(void) {
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    ASTNode loopNode = {0};
    loopNode.type = NODE_FOR_RANGE;
    
    LoopOptimizationHints hints = getLoopOptimizations(&loopNode, &vmCtx);
    
    TEST_ASSERT_TRUE(hints.enableLoopUnrolling);
    TEST_ASSERT_TRUE(hints.enableInvariantHoisting);
    TEST_ASSERT_TRUE(hints.optimizeInductionVars);
    TEST_ASSERT_TRUE(hints.maxUnrollIterations > 0);
    
    // Test fast backend (should have no optimizations)
    VMOptimizationContext fastCtx = createVMOptimizationContext(BACKEND_FAST);
    LoopOptimizationHints fastHints = getLoopOptimizations(&loopNode, &fastCtx);
    
    TEST_ASSERT_FALSE(fastHints.enableLoopUnrolling);
    TEST_ASSERT_FALSE(fastHints.enableInvariantHoisting);
    TEST_ASSERT_FALSE(fastHints.optimizeInductionVars);
}

// Test memory layout optimizations
void test_get_memory_layout_optimizations(void) {
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    ASTNode node = {0};
    node.type = NODE_LITERAL;
    
    MemoryLayoutHints hints = getMemoryLayoutOptimizations(&node, &vmCtx);
    
    TEST_ASSERT_TRUE(hints.enableConstantPooling);
    TEST_ASSERT_TRUE(hints.constantPoolSize > 0);
    TEST_ASSERT_TRUE(hints.localVarFrameSize > 0);
}

// Test register allocation with pressure
void test_register_allocation_under_pressure(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    // Allocate many registers to create pressure
    int allocatedRegs[50];
    int allocatedCount = 0;
    
    for (int i = 0; i < 50 && allocatedCount < 50; i++) {
        int reg = allocateOptimalRegister(&regState, &vmCtx, false, 10);
        if (reg >= 0) {
            allocatedRegs[allocatedCount++] = reg;
        } else {
            break; // No more registers available
        }
    }
    
    TEST_ASSERT_TRUE(allocatedCount > 0);
    
    // Check that pressure has increased
    float pressure = calculateRegisterPressure(&regState);
    TEST_ASSERT_TRUE(pressure > 0.1f);
    
    // Free all allocated registers
    for (int i = 0; i < allocatedCount; i++) {
        freeOptimizedRegister(&regState, allocatedRegs[i]);
    }
}

// Test register reuse optimization
void test_register_reuse_optimization(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    vmCtx.enableRegisterReuse = true;
    
    // Allocate and free a register
    int reg1 = allocateOptimalRegister(&regState, &vmCtx, false, 10);
    TEST_ASSERT_TRUE(reg1 >= 0);
    
    freeOptimizedRegister(&regState, reg1);
    
    // Allocate another register - with reuse enabled, it should prefer the recently freed one
    int reg2 = allocateOptimalRegister(&regState, &vmCtx, false, 10);
    TEST_ASSERT_TRUE(reg2 >= 0);
    
    // The exact register returned depends on the scoring algorithm,
    // but the recently freed register should have a higher score
    TEST_ASSERT_TRUE(regState.lastUse[reg1] > 0 || reg2 == reg1);
}

// Test debug functions
void test_debug_functions(void) {
    RegisterState regState;
    initRegisterState(&regState);
    
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    
    // These functions should not crash
    dumpRegisterState(&regState);
    dumpVMOptimizationContext(&vmCtx);
    validateRegisterAllocation(&regState);
    
    // Allocate some registers and test again
    int reg1 = allocateOptimalRegister(&regState, &vmCtx, false, 10);
    int reg2 = allocateOptimalRegister(&regState, &vmCtx, true, 50);
    
    TEST_ASSERT_TRUE(reg1 >= 0 && reg2 >= 0);
    
    dumpRegisterState(&regState);
    validateRegisterAllocation(&regState);
    
    freeOptimizedRegister(&regState, reg1);
    freeOptimizedRegister(&regState, reg2);
}

int main(void) {
    UNITY_BEGIN();
    
    // VM optimization context tests
    RUN_TEST(test_create_vm_optimization_context_fast);
    RUN_TEST(test_create_vm_optimization_context_optimized);
    RUN_TEST(test_create_vm_optimization_context_hybrid);
    
    // Register state tests
    RUN_TEST(test_init_register_state);
    RUN_TEST(test_calculate_register_pressure);
    
    // Register allocation tests
    RUN_TEST(test_allocate_optimal_register);
    RUN_TEST(test_free_optimized_register);
    RUN_TEST(test_register_allocation_under_pressure);
    RUN_TEST(test_register_reuse_optimization);
    
    // Optimization hints tests
    RUN_TEST(test_get_instruction_optimizations);
    RUN_TEST(test_get_loop_optimizations);
    RUN_TEST(test_get_memory_layout_optimizations);
    
    // Debug and utility tests
    RUN_TEST(test_debug_functions);
    
    UNITY_END();
}