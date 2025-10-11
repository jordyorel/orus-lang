#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"

#define TEST_CASE(name) static bool name(void)
#define ASSERT_TRUE(cond)                                                        \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", #cond, __FILE__,   \
                    __LINE__);                                                   \
            return false;                                                        \
        }                                                                        \
    } while (0)

enum { FUNC_MAIN = 0 };

enum { LOOP_0 = 0 };

static void run_hot_loop(uint64_t hits) {
    if (hits == 0) {
        return;
    }

    HotPathSample* sample = &vm.profile[LOOP_0];
    sample->func = FUNC_MAIN;
    sample->loop = LOOP_0;
    sample->hit_count = hits - 1;
}

TEST_CASE(test_hot_loop_detection) {
    initVM();

    vm.functionCount = 1;

    run_hot_loop(HOT_THRESHOLD);

    bool triggered = vm_profile_tick(&vm, FUNC_MAIN, LOOP_0);
    ASSERT_TRUE(triggered);

    freeVM();
    return true;
}

TEST_CASE(test_hot_loop_resets_counter_when_jit_disabled) {
    initVM();

    vm.functionCount = 1;

    bool saved_jit_enabled = vm.jit_enabled;
    vm.jit_enabled = false;

    HotPathSample* sample = &vm.profile[LOOP_0];
    sample->func = FUNC_MAIN;
    sample->loop = LOOP_0;
    sample->hit_count = HOT_THRESHOLD - 1;

    ASSERT_TRUE(vm_profile_tick(&vm, FUNC_MAIN, LOOP_0));
    ASSERT_TRUE(vm.profile[LOOP_0].hit_count == 0);

    vm.jit_enabled = saved_jit_enabled;

    freeVM();
    return true;
}

TEST_CASE(test_hot_loop_triggers_jit_entry) {
    initVM();

    if (!vm.jit_enabled) {
        freeVM();
        return true;
    }

    vm.functionCount = 1;

    HotPathSample* sample = &vm.profile[LOOP_0];
    sample->func = FUNC_MAIN;
    sample->loop = LOOP_0;
    sample->hit_count = HOT_THRESHOLD - 1;

    uint64_t base_compilations = vm.jit_compilation_count;
    uint64_t base_invocations = vm.jit_invocation_count;

    ASSERT_TRUE(vm_profile_tick(&vm, FUNC_MAIN, LOOP_0));

    ASSERT_TRUE(vm.jit_compilation_count >= base_compilations + 1);
    ASSERT_TRUE(vm.jit_invocation_count >= base_invocations + 1);

    JITEntry* entry = vm_jit_lookup_entry(FUNC_MAIN, LOOP_0);
    ASSERT_TRUE(entry != NULL);
    ASSERT_TRUE(entry->entry_point != NULL);

    ASSERT_TRUE(vm.jit_cache.count >= 1);
    ASSERT_TRUE(vm.profile[LOOP_0].hit_count == 0);

    freeVM();
    return true;
}

int main(void) {
    if (!test_hot_loop_detection()) {
        return 1;
    }

    if (!test_hot_loop_resets_counter_when_jit_disabled()) {
        return 1;
    }

    if (!test_hot_loop_triggers_jit_entry()) {
        return 1;
    }

    puts("All hot loop profiling tests passed.");
    return 0;
}
