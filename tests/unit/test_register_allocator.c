#include <stdbool.h>
#include <stdio.h>

#include "compiler/register_allocator.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool test_typed_register_allocation_cycle(void) {
    DualRegisterAllocator* allocator = compiler_create_allocator();
    ASSERT_TRUE(allocator != NULL, "allocator should be created");

    RegisterAllocation* first = compiler_alloc_typed(allocator, REG_BANK_TYPED_I32);
    ASSERT_TRUE(first != NULL, "first typed allocation should succeed");
    ASSERT_TRUE(first->strategy == REG_STRATEGY_TYPED, "first allocation should be typed");
    ASSERT_TRUE(first->physical_type == REG_TYPE_I32, "first allocation should target i32 bank");

    RegisterAllocation* second = compiler_alloc_typed(allocator, REG_BANK_TYPED_I32);
    ASSERT_TRUE(second != NULL, "second typed allocation should succeed");
    ASSERT_TRUE(second->physical_id != first->physical_id,
                "second allocation should use a different physical register");

    int released_id = first->physical_id;
    compiler_free_allocation(allocator, first);

    RegisterAllocation* third = compiler_alloc_typed(allocator, REG_BANK_TYPED_I32);
    ASSERT_TRUE(third != NULL, "third typed allocation should succeed after freeing");
    ASSERT_TRUE(third->physical_id == released_id,
                "freed typed register should be reused on subsequent allocation");

    compiler_free_allocation(allocator, second);
    compiler_free_allocation(allocator, third);
    compiler_destroy_allocator(allocator);
    return true;
}

static bool test_distinct_banks_track_independently(void) {
    DualRegisterAllocator* allocator = compiler_create_allocator();
    ASSERT_TRUE(allocator != NULL, "allocator should be created");

    RegisterAllocation* int_alloc = compiler_alloc_typed(allocator, REG_BANK_TYPED_I32);
    RegisterAllocation* float_alloc = compiler_alloc_typed(allocator, REG_BANK_TYPED_F64);
    ASSERT_TRUE(int_alloc != NULL && float_alloc != NULL, "typed allocations should succeed");
    ASSERT_TRUE(int_alloc->physical_type == REG_TYPE_I32, "integer bank should tag i32 type");
    ASSERT_TRUE(float_alloc->physical_type == REG_TYPE_F64, "float bank should tag f64 type");
    ASSERT_TRUE(int_alloc->physical_id == 0, "integer bank should allocate from offset 0");
    ASSERT_TRUE(float_alloc->physical_id == 0, "float bank should allocate independently from offset 0");

    compiler_free_allocation(allocator, int_alloc);
    compiler_free_allocation(allocator, float_alloc);
    compiler_destroy_allocator(allocator);
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_typed_register_allocation_cycle,
        test_distinct_banks_track_independently,
    };

    const char* names[] = {
        "Typed register allocation/free cycle reuses freed slots",
        "Distinct register banks maintain independent indices",
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        if (tests[i]()) {
            printf("[PASS] %s\n", names[i]);
            passed++;
        } else {
            printf("[FAIL] %s\n", names[i]);
            return 1;
        }
    }

    printf("%d/%d register allocator tests passed\n", passed, total);
    return 0;
}
