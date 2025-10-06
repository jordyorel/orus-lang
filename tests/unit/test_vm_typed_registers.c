#include <stdbool.h>
#include <stdio.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"
#include "runtime/memory.h"

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
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 42,
                "Deferred store should keep boxed mirror synchronized for globals");
    ASSERT_TRUE(IS_I32(vm.register_file.globals[0]) && AS_I32(vm.register_file.globals[0]) == 42,
                "Register file globals should mirror deferred typed value");

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

    Value* slot = get_register(&vm.register_file, 0);
    ASSERT_TRUE(slot != NULL, "Register file should expose slot for capture");
    ObjUpvalue* upvalue = captureUpvalue(slot);
    ASSERT_TRUE(upvalue != NULL, "captureUpvalue should return handle");
    ASSERT_TRUE(upvalue->location == slot, "Upvalue should reference register slot");

    vm_store_i32_typed_hot(0, 99);
    ASSERT_TRUE(!vm.typed_regs.dirty[0], "Registers with open upvalues must stay boxed");
    ASSERT_TRUE(IS_I32(vm.registers[0]) && AS_I32(vm.registers[0]) == 99,
                "Boxed register should update when upvalue is open");
    ASSERT_TRUE(IS_I32(*upvalue->location) && AS_I32(*upvalue->location) == 99,
                "Open upvalue should see updated value");

    closeUpvalues(slot);
    freeVM();
    return true;
}

static bool run_single_iter_step(Chunk* chunk) {
    vm.chunk = chunk;
    vm.ip = chunk->code;
    vm.isShuttingDown = false;

    InterpretResult result = vm_run_dispatch();
    return result == INTERPRET_OK;
}

static void build_iter_next_chunk(Chunk* chunk, uint8_t dst, uint8_t iter_reg, uint8_t has_reg) {
    initChunk(chunk);
    writeChunk(chunk, OP_ITER_NEXT_R, 0, 0, NULL);
    writeChunk(chunk, dst, 0, 0, NULL);
    writeChunk(chunk, iter_reg, 0, 0, NULL);
    writeChunk(chunk, has_reg, 0, 0, NULL);
    writeChunk(chunk, OP_HALT, 0, 0, NULL);
}

static bool test_range_iterator_uses_typed_registers(void) {
    initVM();

    const uint8_t dst_reg = 1;
    const uint8_t iter_reg = 3;
    const uint8_t has_reg = 2;

    Chunk chunk;
    build_iter_next_chunk(&chunk, dst_reg, iter_reg, has_reg);

    ObjRangeIterator* iterator = allocateRangeIterator(0, 3, 1);
    ASSERT_TRUE(iterator != NULL, "allocateRangeIterator should succeed");
    vm_set_register_safe(iter_reg, RANGE_ITERATOR_VAL(iterator));

    ASSERT_TRUE(run_single_iter_step(&chunk), "First iteration should execute");
    ASSERT_TRUE(vm.typed_regs.reg_types[dst_reg] == REG_TYPE_I64,
                "Destination register should be typed as i64 after first iteration");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 0,
                "First iteration should yield starting value");
    ASSERT_TRUE(!vm.typed_regs.dirty[dst_reg],
                "Initial store should synchronize boxed register for range iterator");
    ASSERT_TRUE(IS_I64(vm.registers[dst_reg]) && AS_I64(vm.registers[dst_reg]) == 0,
                "Boxed register should receive first iteration value");
    ASSERT_TRUE(vm.typed_regs.reg_types[has_reg] == REG_TYPE_BOOL,
                "Has-value flag should occupy typed bool slot");
    ASSERT_TRUE(vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should be true when iterator yields a value");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Second iteration should execute");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 1,
                "Second iteration should advance typed payload");
    ASSERT_TRUE(vm.typed_regs.dirty[dst_reg],
                "Second iteration should defer boxing for hot path");
    ASSERT_TRUE(IS_I64(vm.registers[dst_reg]) && AS_I64(vm.registers[dst_reg]) == 1,
                "Global mirror should advance alongside typed payload");
    ASSERT_TRUE(IS_I64(vm.register_file.globals[dst_reg]) &&
                    AS_I64(vm.register_file.globals[dst_reg]) == 1,
                "Register file globals should match deferred range writes");
    ASSERT_TRUE(vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should stay true while range produces values");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Third iteration should execute");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 2,
                "Third iteration should update typed payload without boxing");
    ASSERT_TRUE(vm.typed_regs.dirty[dst_reg],
                "Typed register should remain dirty until explicit read");
    ASSERT_TRUE(IS_I64(vm.registers[dst_reg]) && AS_I64(vm.registers[dst_reg]) == 2,
                "Deferred writes should keep boxed range mirror current");
    ASSERT_TRUE(IS_I64(vm.register_file.globals[dst_reg]) &&
                    AS_I64(vm.register_file.globals[dst_reg]) == 2,
                "Register file globals should retain last deferred range value");
    ASSERT_TRUE(vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should be true before iterator exhaustion");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Fourth iteration should signal exhaustion");
    ASSERT_TRUE(!vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should become false once range iterator finishes");
    ASSERT_TRUE(IS_BOOL(vm.registers[has_reg]) && !AS_BOOL(vm.registers[has_reg]),
                "Boxed has-value flag should flush false on exhaustion");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 2,
                "Destination typed value should retain last yielded integer");

    freeChunk(&chunk);
    freeVM();
    return true;
}

static bool test_array_iterator_preserves_typed_loop_variable(void) {
    initVM();

    const uint8_t dst_reg = 5;
    const uint8_t iter_reg = 7;
    const uint8_t has_reg = 6;

    Chunk chunk;
    build_iter_next_chunk(&chunk, dst_reg, iter_reg, has_reg);

    ObjArray* array = allocateArray(3);
    ASSERT_TRUE(array != NULL, "allocateArray should succeed");
    array->length = 3;
    array->elements[0] = I64_VAL(10);
    array->elements[1] = I64_VAL(20);
    array->elements[2] = I64_VAL(30);

    ObjArrayIterator* iterator = allocateArrayIterator(array);
    ASSERT_TRUE(iterator != NULL, "allocateArrayIterator should succeed");
    vm_set_register_safe(iter_reg, ARRAY_ITERATOR_VAL(iterator));

    ASSERT_TRUE(run_single_iter_step(&chunk), "First array iteration should execute");
    ASSERT_TRUE(vm.typed_regs.reg_types[dst_reg] == REG_TYPE_I64,
                "Array iterator should type the loop variable as i64");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 10,
                "First array iteration should load first element");
    ASSERT_TRUE(!vm.typed_regs.dirty[dst_reg],
                "Initial array iteration should write boxed value");
    ASSERT_TRUE(vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should start true for populated arrays");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Second array iteration should execute");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 20,
                "Second array iteration should update typed payload");
    ASSERT_TRUE(vm.typed_regs.dirty[dst_reg],
                "Hot array path should avoid boxing on subsequent iterations");
    ASSERT_TRUE(IS_I64(vm.registers[dst_reg]) && AS_I64(vm.registers[dst_reg]) == 20,
                "Array iterator globals should update during deferred writes");
    ASSERT_TRUE(IS_I64(vm.register_file.globals[dst_reg]) &&
                    AS_I64(vm.register_file.globals[dst_reg]) == 20,
                "Register file globals should stay in sync for array iterators");
    ASSERT_TRUE(vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should remain true while elements remain");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Third array iteration should execute");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 30,
                "Third array iteration should expose final element via typed path");
    ASSERT_TRUE(vm.typed_regs.dirty[dst_reg],
                "Typed loop variable should stay dirty until read");

    ASSERT_TRUE(run_single_iter_step(&chunk), "Fourth array iteration should detect exhaustion");
    ASSERT_TRUE(!vm.typed_regs.bool_regs[has_reg],
                "Has-value flag should clear when iterator exhausts array");
    ASSERT_TRUE(IS_BOOL(vm.registers[has_reg]) && !AS_BOOL(vm.registers[has_reg]),
                "Boxed boolean flag should flush false at exhaustion");
    ASSERT_TRUE(vm.typed_regs.i64_regs[dst_reg] == 30,
                "Typed register should preserve last array element");

    freeChunk(&chunk);
    freeVM();
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_typed_register_deferred_boxing_flushes_on_read,
        test_typed_register_flushes_for_open_upvalue,
        test_range_iterator_uses_typed_registers,
        test_array_iterator_preserves_typed_loop_variable,
    };

    const char* names[] = {
        "Deferred boxing flushes via vm_get_register_safe",
        "Open upvalues force boxed synchronization",
        "Range iterators keep loop variable typed",
        "Array iterators keep loop variable typed",
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
