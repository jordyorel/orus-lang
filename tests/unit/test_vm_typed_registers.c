#include <stdbool.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool test_typed_register_deferred_boxing_flushes_on_read(void) {
    initVM();

    vm_store_i32_typed_hot(0, 10);
    ASSERT_TRUE(!vm.typed_regs.dirty[0], "Initial store should synchronize boxed register");
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 10,
                "Initial store should write boxed value");

    vm_store_i32_typed_hot(0, 42);
    ASSERT_TRUE(vm.typed_regs.dirty[0], "Second store should defer boxing");
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 10,
                "Deferred store should leave boxed value untouched");

    Value flushed = vm_get_register_safe(0);
    ASSERT_TRUE(IS_I32(flushed) && AS_I32(flushed) == 42,
                "vm_get_register_safe should flush deferred integer");
    ASSERT_TRUE(!vm.typed_regs.dirty[0], "Dirty bit should clear after flush");
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 42,
                "Boxed register should reflect flushed value");

    freeVM();
    return true;
}

static bool test_typed_register_flushes_for_open_upvalue(void) {
    initVM();

    vm_set_register_safe(0, I32_VAL(7));
    Value initial = vm_get_register_safe(0);
    ASSERT_TRUE(IS_I32(initial) && AS_I32(initial) == 7,
                "Initial value should be accessible");

    ObjUpvalue* upvalue = captureUpvalue(&vm.registers[0]);
    ASSERT_TRUE(upvalue != NULL, "captureUpvalue should return handle");
    ASSERT_TRUE(upvalue->location == &vm.registers[0], "Upvalue should reference register slot");

    vm_store_i32_typed_hot(0, 99);
    ASSERT_TRUE(!vm.typed_regs.dirty[0], "Registers with open upvalues must stay boxed");
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 99,
                "Boxed register should update when upvalue is open");
    ASSERT_TRUE(IS_I32(*upvalue->location) && AS_I32(*upvalue->location) == 99,
                "Open upvalue should see updated value");

    closeUpvalues(&vm.registers[0]);
    freeVM();
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_typed_register_deferred_boxing_flushes_on_read,
        test_typed_register_flushes_for_open_upvalue,
    };

    const char* names[] = {
        "Deferred boxing flushes via vm_get_register_safe",
        "Open upvalues force boxed synchronization",
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

    printf("%d/%d typed register tests passed\n", passed, total);
    return 0;
}
