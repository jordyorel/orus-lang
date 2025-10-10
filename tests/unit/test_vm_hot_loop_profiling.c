#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_profiling.h"

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

    run_hot_loop(HOT_THRESHOLD);

    bool triggered = vm_profile_tick(&vm, FUNC_MAIN, LOOP_0);
    ASSERT_TRUE(triggered);

    freeVM();
    return true;
}

int main(void) {
    if (!test_hot_loop_detection()) {
        return 1;
    }

    puts("All hot loop profiling tests passed.");
    return 0;
}
