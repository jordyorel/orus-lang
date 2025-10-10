#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "type/type.h"
#include "vm/vm.h"
#include "vm/vm_comparison.h"
#include "vm/vm_dispatch.h"

static void write_inc_global_program(Chunk* chunk, uint8_t inc_opcode, uint8_t target_reg,
                                     uint8_t load_dest, uint8_t global_index, const char* tag) {
    const int line = 1;
    const int column = 0;

    writeChunk(chunk, inc_opcode, line, column, tag);
    writeChunk(chunk, target_reg, line, column, tag);

    writeChunk(chunk, OP_LOAD_GLOBAL, line, column, tag);
    writeChunk(chunk, load_dest, line, column, tag);
    writeChunk(chunk, global_index, line, column, tag);

    writeChunk(chunk, OP_HALT, line, column, tag);
}

static bool test_inc_i32_global_updates_boxed_storage(void) {
    initVM();

    vm.variableCount = 1;
    vm.globalTypes[0] = getPrimitiveType(TYPE_I32);

    Chunk chunk;
    initChunk(&chunk);

    const int32_t initial_value = 41;
    const uint8_t target_reg = 0;
    const uint8_t load_dest = FRAME_REG_START;  // any non-global destination is fine
    const uint8_t global_index = 0;

    write_inc_global_program(&chunk, OP_INC_I32_R, target_reg, load_dest, global_index, "inc_global_i32");

    vm_set_register_safe(target_reg, I32_VAL(initial_value));
    vm_store_i32_typed_hot(target_reg, initial_value);

    vm.chunk = &chunk;
    vm.ip = chunk.code;

    InterpretResult result = vm_run_dispatch();
    bool success = true;

    if (result != INTERPRET_OK) {
        fprintf(stderr, "Expected INTERPRET_OK from vm_run_dispatch, got %d\n", result);
        success = false;
        goto cleanup;
    }

    Value loaded = vm_get_register_safe(load_dest);
    if (!IS_I32(loaded) || AS_I32(loaded) != initial_value + 1) {
        fprintf(stderr, "Expected OP_LOAD_GLOBAL to observe incremented value %d, got type %d value %d\n",
                initial_value + 1, loaded.type, IS_I32(loaded) ? AS_I32(loaded) : -1);
        success = false;
        goto cleanup;
    }

    Value published = vm.globals[global_index];
    if (!IS_I32(published) || AS_I32(published) != initial_value + 1) {
        fprintf(stderr, "Expected global slot to reconcile to %d, got type %d value %d\n",
                initial_value + 1, published.type, IS_I32(published) ? AS_I32(published) : -1);
        success = false;
        goto cleanup;
    }

cleanup:
    freeChunk(&chunk);
    freeVM();
    return success;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"OP_INC_I32_R updates global boxed storage", test_inc_i32_global_updates_boxed_storage},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        printf("[ RUN      ] %s\n", tests[i].name);
        if (tests[i].fn()) {
            printf("[     OK  ] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[ FAILED ] %s\n", tests[i].name);
        }
    }

    printf("%d/%d OP_INC_* global reconciliation tests passed\n", passed, total);

    return passed == total ? 0 : 1;
}
