#include "test_framework.h"
#include "../../../include/vm.h"
#include "../../../include/memory.h"

// Forward declarations to access internal VM functions for testing
extern VM vm;

void test_value_creation() {
    // Test basic value creation macros
    Value boolVal = BOOL_VAL(true);
    ASSERT(IS_BOOL(boolVal), "BOOL_VAL creates VAL_BOOL type");
    ASSERT(AS_BOOL(boolVal) == true, "BOOL_VAL stores correct boolean value");
    
    Value nilVal = NIL_VAL;
    ASSERT(IS_NIL(nilVal), "NIL_VAL creates VAL_NIL type");
    
    Value i32Val = I32_VAL(42);
    ASSERT(IS_I32(i32Val), "I32_VAL creates VAL_I32 type");
    ASSERT_EQ(42, AS_I32(i32Val), "I32_VAL stores correct int32 value");
    
    Value i64Val = I64_VAL(9223372036854775807LL);
    ASSERT(IS_I64(i64Val), "I64_VAL creates VAL_I64 type");
    ASSERT(AS_I64(i64Val) == 9223372036854775807LL, "I64_VAL stores correct int64 value");
    
    Value f64Val = F64_VAL(3.14159);
    ASSERT(IS_F64(f64Val), "F64_VAL creates VAL_F64 type");
    ASSERT(AS_F64(f64Val) == 3.14159, "F64_VAL stores correct double value");
}

void test_value_equality() {
    Value val1 = I32_VAL(42);
    Value val2 = I32_VAL(42);
    Value val3 = I32_VAL(24);
    
    ASSERT(valuesEqual(val1, val2), "Equal integer values compare as equal");
    ASSERT(!valuesEqual(val1, val3), "Different integer values compare as not equal");
    
    Value bool1 = BOOL_VAL(true);
    Value bool2 = BOOL_VAL(true);
    Value bool3 = BOOL_VAL(false);
    
    ASSERT(valuesEqual(bool1, bool2), "Equal boolean values compare as equal");
    ASSERT(!valuesEqual(bool1, bool3), "Different boolean values compare as not equal");
    
    Value nil1 = NIL_VAL;
    Value nil2 = NIL_VAL;
    
    ASSERT(valuesEqual(nil1, nil2), "NIL values compare as equal");
    ASSERT(!valuesEqual(val1, nil1), "Different types compare as not equal");
}

void test_chunk_operations() {
    Chunk chunk;
    initChunk(&chunk);
    
    ASSERT_EQ(0, chunk.count, "New chunk has zero count");
    ASSERT_EQ(0, chunk.constants.count, "New chunk has zero constants");
    
    // Add some bytecode
    writeChunk(&chunk, OP_LOAD_TRUE, 1, 5);
    writeChunk(&chunk, 0, 1, 6);  // register operand
    
    ASSERT_EQ(2, chunk.count, "Chunk count increases after writing");
    ASSERT_EQ(OP_LOAD_TRUE, chunk.code[0], "First opcode stored correctly");
    ASSERT_EQ(0, chunk.code[1], "First operand stored correctly");
    ASSERT_EQ(1, chunk.lines[0], "Line number stored correctly");
    ASSERT_EQ(5, chunk.columns[0], "Column number stored correctly");
    
    // Add a constant
    Value constant = I32_VAL(123);
    int constIndex = addConstant(&chunk, constant);
    
    ASSERT_EQ(0, constIndex, "First constant gets index 0");
    ASSERT_EQ(1, chunk.constants.count, "Constants count increases");
    ASSERT(valuesEqual(constant, chunk.constants.values[0]), "Constant stored correctly");
    
    freeChunk(&chunk);
}

void test_vm_initialization() {
    // Test VM initialization
    initVM();
    
    ASSERT_EQ(0, vm.frameCount, "VM starts with zero frames");
    ASSERT_EQ(0, vm.functionCount, "VM starts with zero functions");
    ASSERT_EQ(0, vm.variableCount, "VM starts with zero variables");
    ASSERT_EQ(0, vm.instruction_count, "VM starts with zero instructions executed");
    
    // Test that registers are initialized (all should be NIL)
    for (int i = 0; i < 10; i++) {  // Check first 10 registers
        ASSERT(IS_NIL(vm.registers[i]), "Registers initialize to NIL");
    }
    
    freeVM();
}

int main() {
    printf("Running VM Core Tests\n");
    printf("========================================\n");
    
    RUN_TEST(test_value_creation);
    RUN_TEST(test_value_equality);
    RUN_TEST(test_chunk_operations);
    RUN_TEST(test_vm_initialization);
    
    PRINT_TEST_RESULTS();
    
    return tests_failed > 0 ? 1 : 0;
}