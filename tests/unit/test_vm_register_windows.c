#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"
#include "runtime/memory.h"
#include "vm/spill_manager.h"

static void write_short(Chunk* chunk, uint16_t value) {
    writeChunk(chunk, (uint8_t)(value >> 8), 0, 0, NULL);
    writeChunk(chunk, (uint8_t)(value & 0xFF), 0, 0, NULL);
}

static ObjClosure* make_constant_closure(Value constant) {
    Chunk* chunk = (Chunk*)malloc(sizeof(Chunk));
    if (!chunk) {
        return NULL;
    }
    initChunk(chunk);

    uint16_t const_index = (uint16_t)addConstant(chunk, constant);
    writeChunk(chunk, OP_LOAD_CONST, 0, 0, NULL);
    writeChunk(chunk, FRAME_REG_START, 0, 0, NULL);
    write_short(chunk, const_index);
    writeChunk(chunk, OP_RETURN_R, 0, 0, NULL);
    writeChunk(chunk, FRAME_REG_START, 0, 0, NULL);

    ObjFunction* function = allocateFunction();
    if (!function) {
        freeChunk(chunk);
        free(chunk);
        return NULL;
    }
    function->arity = 0;
    function->chunk = chunk;
    function->upvalueCount = 0;
    function->name = NULL;

    return allocateClosure(function);
}

static bool object_in_heap(Obj* target) {
    for (Obj* obj = vm.objects; obj != NULL; obj = obj->next) {
        if (obj == target) {
            return true;
        }
    }
    return false;
}

static bool run_chunk(Chunk* chunk) {
    vm.chunk = chunk;
    vm.ip = chunk->code;
    vm.isShuttingDown = false;
    return vm_run_dispatch() == INTERPRET_OK;
}

static bool test_nested_closure_call_returns_value(void) {
    initVM();

    ObjClosure* inner = make_constant_closure(I32_VAL(123));
    ObjClosure* outer = NULL;

    if (!inner) {
        fprintf(stderr, "Failed to allocate inner closure\n");
        freeVM();
        return false;
    }

    Chunk* outer_chunk = (Chunk*)malloc(sizeof(Chunk));
    if (!outer_chunk) {
        fprintf(stderr, "Failed to allocate outer chunk\n");
        freeVM();
        return false;
    }
    initChunk(outer_chunk);

    writeChunk(outer_chunk, OP_CALL_R, 0, 0, NULL);
    writeChunk(outer_chunk, 1, 0, 0, NULL);       // funcReg -> inner closure in R1
    writeChunk(outer_chunk, 0, 0, 0, NULL);       // firstArgReg (unused)
    writeChunk(outer_chunk, 0, 0, 0, NULL);       // argCount = 0
    writeChunk(outer_chunk, FRAME_REG_START, 0, 0, NULL); // result in frame register
    writeChunk(outer_chunk, OP_RETURN_R, 0, 0, NULL);
    writeChunk(outer_chunk, FRAME_REG_START, 0, 0, NULL);

    ObjFunction* outer_function = allocateFunction();
    if (!outer_function) {
        fprintf(stderr, "Failed to allocate outer function\n");
        freeChunk(outer_chunk);
        free(outer_chunk);
        freeVM();
        return false;
    }
    outer_function->arity = 0;
    outer_function->chunk = outer_chunk;
    outer_function->upvalueCount = 0;
    outer = allocateClosure(outer_function);
    if (!outer) {
        fprintf(stderr, "Failed to allocate outer closure\n");
        freeChunk(outer_chunk);
        free(outer_chunk);
        freeVM();
        return false;
    }

    Chunk top_chunk;
    initChunk(&top_chunk);
    writeChunk(&top_chunk, OP_CALL_R, 0, 0, NULL);
    writeChunk(&top_chunk, 0, 0, 0, NULL);  // funcReg -> outer closure in R0
    writeChunk(&top_chunk, 0, 0, 0, NULL);  // firstArgReg (unused)
    writeChunk(&top_chunk, 0, 0, 0, NULL);  // argCount = 0
    writeChunk(&top_chunk, 3, 0, 0, NULL);  // result -> R3
    writeChunk(&top_chunk, OP_HALT, 0, 0, NULL);

    vm_set_register_safe(0, CLOSURE_VAL(outer));
    vm_set_register_safe(1, CLOSURE_VAL(inner));

    bool ok = run_chunk(&top_chunk);
    if (!ok) {
        fprintf(stderr, "Interpreter failed for nested call test\n");
        freeChunk(&top_chunk);
        freeVM();
        return false;
    }

    bool success = true;
    if (vm.frameCount != 0) {
        fprintf(stderr, "Expected frame stack to unwind to zero after nested call\n");
        success = false;
    }

    Value result = vm_get_register_safe(3);
    success = success && IS_I32(result) && AS_I32(result) == 123;
    if (!success) {
        fprintf(stderr, "Expected nested call to return 123, got type %d value %d\n",
                result.type, IS_I32(result) ? AS_I32(result) : -1);
    }

    freeChunk(&top_chunk);
    freeVM();
    return success;
}

static bool test_tail_call_reuses_frame(void) {
    initVM();

    ObjClosure* inner = make_constant_closure(I32_VAL(77));
    if (!inner) {
        fprintf(stderr, "Failed to allocate inner closure\n");
        freeVM();
        return false;
    }

    Chunk* outer_chunk = (Chunk*)malloc(sizeof(Chunk));
    if (!outer_chunk) {
        fprintf(stderr, "Failed to allocate outer chunk\n");
        freeVM();
        return false;
    }
    initChunk(outer_chunk);

    writeChunk(outer_chunk, OP_TAIL_CALL_R, 0, 0, NULL);
    writeChunk(outer_chunk, 1, 0, 0, NULL);   // funcReg -> inner closure in R1
    writeChunk(outer_chunk, 0, 0, 0, NULL);   // firstArgReg
    writeChunk(outer_chunk, 0, 0, 0, NULL);   // argCount
    writeChunk(outer_chunk, 3, 0, 0, NULL);   // result register propagated to caller

    ObjFunction* outer_function = allocateFunction();
    if (!outer_function) {
        fprintf(stderr, "Failed to allocate outer function\n");
        freeChunk(outer_chunk);
        free(outer_chunk);
        freeVM();
        return false;
    }
    outer_function->arity = 0;
    outer_function->chunk = outer_chunk;
    outer_function->upvalueCount = 0;
    ObjClosure* outer = allocateClosure(outer_function);
    if (!outer) {
        fprintf(stderr, "Failed to allocate outer closure\n");
        freeChunk(outer_chunk);
        free(outer_chunk);
        freeVM();
        return false;
    }

    Chunk top_chunk;
    initChunk(&top_chunk);
    writeChunk(&top_chunk, OP_CALL_R, 0, 0, NULL);
    writeChunk(&top_chunk, 0, 0, 0, NULL);  // outer closure in R0
    writeChunk(&top_chunk, 0, 0, 0, NULL);
    writeChunk(&top_chunk, 0, 0, 0, NULL);
    writeChunk(&top_chunk, 3, 0, 0, NULL);  // result -> R3
    writeChunk(&top_chunk, OP_HALT, 0, 0, NULL);

    vm_set_register_safe(0, CLOSURE_VAL(outer));
    vm_set_register_safe(1, CLOSURE_VAL(inner));

    bool ok = run_chunk(&top_chunk);
    if (!ok) {
        fprintf(stderr, "Interpreter failed for tail call test\n");
        freeChunk(&top_chunk);
        freeVM();
        return false;
    }

    bool success = true;
    if (vm.frameCount != 0) {
        fprintf(stderr, "Expected frame stack to unwind to zero after tail call\n");
        success = false;
    }

    Value result = vm_get_register_safe(3);
    success = success && IS_I32(result) && AS_I32(result) == 77;
    if (!success) {
        fprintf(stderr, "Expected tail call to return 77, got type %d value %d\n",
                result.type, IS_I32(result) ? AS_I32(result) : -1);
    }

    freeChunk(&top_chunk);
    freeVM();
    return success;
}

static bool test_gc_preserves_frame_roots(void) {
    initVM();

    const char* labels[] = {"frame0", "frame1", "frame2", "frame3"};
    const int depth = (int)(sizeof(labels) / sizeof(labels[0]));
    for (int i = 0; i < depth; i++) {
        CallFrame* frame = allocate_frame(&vm.register_file);
        if (!frame) {
            fprintf(stderr, "Failed to allocate frame %d\n", i);
            freeVM();
            return false;
        }
        ObjString* str = allocateString(labels[i], (int)strlen(labels[i]));
        vm_set_register_safe(FRAME_REG_START, STRING_VAL(str));
    }

    collectGarbage();

    bool success = true;
    CallFrame* frame_iter = vm.register_file.frame_stack;
    int index = depth - 1;
    while (frame_iter && success) {
        Value stored = frame_iter->registers[0];
        if (!IS_STRING(stored) || strcmp(AS_STRING(stored)->chars, labels[index]) != 0) {
            fprintf(stderr, "GC lost frame value at depth %d\n", index);
            success = false;
            break;
        }
        frame_iter = frame_iter->next;
        index--;
    }

    for (int i = 0; i < depth; i++) {
        deallocate_frame(&vm.register_file);
    }

    freeVM();
    return success;
}

static bool test_gc_preserves_spilled_roots(void) {
    initVM();

    SpillManager* manager = vm.register_file.spilled_registers;
    if (!manager) {
        fprintf(stderr, "Spill manager is not initialized\n");
        freeVM();
        return false;
    }

    ObjString* payload = allocateString("spilled-root", 12);
    if (!payload) {
        fprintf(stderr, "Failed to allocate string for spill test\n");
        freeVM();
        return false;
    }

    uint16_t spill_id = SPILL_REG_START;
    if (!set_spill_register_value(manager, spill_id, STRING_VAL(payload))) {
        fprintf(stderr, "Failed to register spilled value\n");
        freeVM();
        return false;
    }

    collectGarbage();

    Value restored;
    bool found = unspill_register_value(manager, spill_id, &restored);
    bool success = found && IS_STRING(restored) && AS_STRING(restored) == payload &&
                   object_in_heap((Obj*)payload);

    if (!success) {
        fprintf(stderr, "Spilled value was not preserved across GC\n");
    }

    remove_spilled_register(manager, spill_id);
    freeVM();
    return success;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"nested closure call returns value", test_nested_closure_call_returns_value},
        {"tail call reuses frame and returns value", test_tail_call_reuses_frame},
        {"GC preserves register file roots", test_gc_preserves_frame_roots},
        {"GC preserves spilled register roots", test_gc_preserves_spilled_roots},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        if (tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            return 1;
        }
    }

    printf("%d/%d register window tests passed\n", passed, total);
    return 0;
}
