#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/codegen/peephole.h"
#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                        \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                 \
        }                                                                                 \
    } while (0)

static bool test_redundant_i32_load_eliminated(void) {
    CompilerContext ctx;
    memset(&ctx, 0, sizeof(CompilerContext));

    ctx.bytecode = init_bytecode_buffer();
    ctx.constants = init_constant_pool();

    ASSERT_TRUE(ctx.bytecode != NULL, "bytecode buffer allocation");
    ASSERT_TRUE(ctx.constants != NULL, "constant pool allocation");

    int constant_index = add_constant(ctx.constants, I32_VAL(42));
    ASSERT_TRUE(constant_index >= 0, "constant pool insertion");

    emit_byte_to_buffer(ctx.bytecode, OP_LOAD_I32_CONST);
    emit_byte_to_buffer(ctx.bytecode, 64);
    emit_word_to_buffer(ctx.bytecode, (uint16_t)constant_index);

    emit_byte_to_buffer(ctx.bytecode, OP_MOVE_I32);
    emit_byte_to_buffer(ctx.bytecode, 65);
    emit_byte_to_buffer(ctx.bytecode, 64);

    emit_byte_to_buffer(ctx.bytecode, OP_LOAD_I32_CONST);
    emit_byte_to_buffer(ctx.bytecode, 65);
    emit_word_to_buffer(ctx.bytecode, (uint16_t)constant_index);

    int initial_count = ctx.bytecode->count;
    int optimized = optimize_constant_propagation(&ctx);

    ASSERT_TRUE(optimized == 1, "exactly one redundant load optimized");
    ASSERT_TRUE(ctx.bytecode->count == initial_count - 4, "bytecode shrunk by 4 bytes");

    free_constant_pool(ctx.constants);
    free_bytecode_buffer(ctx.bytecode);
    return true;
}

static bool test_duplicate_bool_load_eliminated(void) {
    CompilerContext ctx;
    memset(&ctx, 0, sizeof(CompilerContext));

    ctx.bytecode = init_bytecode_buffer();
    ASSERT_TRUE(ctx.bytecode != NULL, "bytecode buffer allocation");

    emit_byte_to_buffer(ctx.bytecode, OP_LOAD_TRUE);
    emit_byte_to_buffer(ctx.bytecode, 70);

    emit_byte_to_buffer(ctx.bytecode, OP_LOAD_TRUE);
    emit_byte_to_buffer(ctx.bytecode, 70);

    int initial_count = ctx.bytecode->count;
    int optimized = optimize_constant_propagation(&ctx);

    ASSERT_TRUE(optimized == 1, "duplicate boolean load eliminated");
    ASSERT_TRUE(ctx.bytecode->count == initial_count - 2, "bytecode shrunk by 2 bytes");

    free_bytecode_buffer(ctx.bytecode);
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_redundant_i32_load_eliminated,
        test_duplicate_bool_load_eliminated,
    };

    const char* names[] = {
        "redundant i32 load eliminated",
        "duplicate bool load eliminated",
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

    printf("%d/%d constant propagation tests passed\n", passed, total);
    return 0;
}
