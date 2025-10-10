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

static bool test_scope_depth_overflow_records_diagnostics(void) {
    DualRegisterAllocator* allocator = compiler_create_allocator();
    ASSERT_TRUE(allocator != NULL, "allocator should be created");

    compiler_allocator_reset_diagnostics(allocator);

    for (int i = 0; i < MP_SCOPE_LEVEL_COUNT; i++) {
        compiler_enter_scope(allocator);
    }

    compiler_enter_scope(allocator);
    compiler_enter_scope(allocator);

    for (int i = 0; i < MP_SCOPE_LEVEL_COUNT; i++) {
        compiler_exit_scope(allocator);
    }

    compiler_exit_scope(allocator);

    AllocatorDiagnostics diagnostics = compiler_allocator_get_diagnostics(allocator);
    ASSERT_TRUE(diagnostics.scope_depth_overflow_count >= 2,
                "scope overflow attempts should be recorded");
    ASSERT_TRUE(diagnostics.scope_exit_underflow_count >= 1,
                "scope underflow attempts should be recorded");
    ASSERT_TRUE(diagnostics.max_scope_depth_seen >= MP_SCOPE_LEVEL_COUNT - 1,
                "max scope depth should track deepest level reached");

    compiler_destroy_allocator(allocator);
    return true;
}

static bool test_typed_span_reservation_and_reconciliation_tracking(void) {
    DualRegisterAllocator* allocator = compiler_create_allocator();
    ASSERT_TRUE(allocator != NULL, "allocator should be created");

    TypedSpanReservation span = {0};
    bool reserved = compiler_begin_typed_span(
        allocator, REG_BANK_TYPED_I32, 3, true, &span);
    ASSERT_TRUE(reserved, "typed span reservation should succeed");
    ASSERT_TRUE(span.length == 3, "span length should match requested count");
    ASSERT_TRUE(span.physical_start >= 0, "span should have a valid physical start index");

    compiler_release_typed_span(allocator, &span);

    TypedSpanReservation pending[4] = {0};
    int pending_count = compiler_collect_pending_reconciliations(allocator, pending, 4);
    ASSERT_TRUE(pending_count == 1, "released span should enqueue one reconciliation");
    ASSERT_TRUE(pending[0].physical_start == span.physical_start,
                "pending span should report the same start index");
    ASSERT_TRUE(pending[0].length == span.length,
                "pending span should report the same length");

    TypedSpanReservation span2 = {0};
    bool reserved_again = compiler_begin_typed_span(
        allocator, REG_BANK_TYPED_I32, 3, false, &span2);
    ASSERT_TRUE(reserved_again, "allocator should reuse freed typed span");
    ASSERT_TRUE(span2.physical_start == span.physical_start,
                "allocator should recycle contiguous window");

    compiler_release_typed_span(allocator, &span2);

    pending_count = compiler_collect_pending_reconciliations(allocator, pending, 4);
    ASSERT_TRUE(pending_count == 0,
                "non-reconciling span should not enqueue reconciliation work");

    compiler_destroy_allocator(allocator);
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_typed_register_allocation_cycle,
        test_distinct_banks_track_independently,
        test_scope_depth_overflow_records_diagnostics,
        test_typed_span_reservation_and_reconciliation_tracking,
    };

    const char* names[] = {
        "Typed register allocation/free cycle reuses freed slots",
        "Distinct register banks maintain independent indices",
        "Scope overflow attempts are captured as diagnostics instead of warnings",
        "Typed span reservations track reconciliation requests",
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
