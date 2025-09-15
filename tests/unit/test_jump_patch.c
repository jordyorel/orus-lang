#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#include "compiler/compiler.h"
#include "debug/debug_config.h"

#define ASSERT_TRUE(cond, message) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false; \
        } \
    } while (0)

static void append_dummy_bytes(BytecodeBuffer* buffer, int count) {
    for (int i = 0; i < count; i++) {
        emit_byte_to_buffer(buffer, OP_HALT);  // Use OP_HALT as harmless filler
    }
}

static bool test_if_jump_patch(void) {
    BytecodeBuffer* buffer = init_bytecode_buffer();
    ASSERT_TRUE(buffer != NULL, "init_bytecode_buffer returned NULL");

    // Simulate conditional jump and then-branch
    emit_byte_to_buffer(buffer, OP_JUMP_IF_NOT_R);
    emit_byte_to_buffer(buffer, 5);  // fake condition register
    int else_patch = emit_jump_placeholder(buffer, OP_JUMP_IF_NOT_R);
    ASSERT_TRUE(else_patch >= 0, "emit_jump_placeholder for else jump failed");

    append_dummy_bytes(buffer, 2);  // then-branch body

    // Emit skip-over jump for else branch
    emit_byte_to_buffer(buffer, OP_JUMP_SHORT);
    int end_patch = emit_jump_placeholder(buffer, OP_JUMP_SHORT);
    ASSERT_TRUE(end_patch >= 0, "emit_jump_placeholder for end jump failed");

    int else_target = buffer->count;
    ASSERT_TRUE(patch_jump(buffer, else_patch, else_target), "patch_jump failed for else branch");

    append_dummy_bytes(buffer, 3);  // else branch body

    int end_target = buffer->count;
    ASSERT_TRUE(patch_jump(buffer, end_patch, end_target), "patch_jump failed for end jump");

    JumpPatch* cond_patch = &buffer->patches[else_patch];
    int next_ip = cond_patch->operand_offset + cond_patch->operand_size;
    int32_t expected_offset = else_target - next_ip;
    uint16_t stored_offset = (uint16_t)(buffer->instructions[cond_patch->operand_offset] << 8 |
                                       buffer->instructions[cond_patch->operand_offset + 1]);
    ASSERT_TRUE(expected_offset == (int32_t)stored_offset,
                "Conditional jump offset mismatch");

    JumpPatch* skip_patch = &buffer->patches[end_patch];
    int32_t expected_short = end_target - (skip_patch->operand_offset + skip_patch->operand_size);
    ASSERT_TRUE(expected_short >= 0 && expected_short <= 0xFF,
                "Short jump expected offset out of range");
    ASSERT_TRUE(buffer->instructions[skip_patch->operand_offset] == (uint8_t)expected_short,
                "Short jump offset mismatch");

    free_bytecode_buffer(buffer);
    return true;
}

static bool test_while_loop_jump_patch(void) {
    BytecodeBuffer* buffer = init_bytecode_buffer();
    ASSERT_TRUE(buffer != NULL, "init_bytecode_buffer returned NULL");

    int loop_start = buffer->count;
    append_dummy_bytes(buffer, 4);  // loop body instructions

    emit_byte_to_buffer(buffer, OP_JUMP);
    int loop_patch = emit_jump_placeholder(buffer, OP_JUMP);
    ASSERT_TRUE(loop_patch >= 0, "emit_jump_placeholder for loop failed");

    bool patched = patch_jump(buffer, loop_patch, loop_start);
    ASSERT_TRUE(patched, "patch_jump failed for loop back edge");

    JumpPatch* loop_info = &buffer->patches[loop_patch];
    ASSERT_TRUE(buffer->instructions[loop_info->instruction_offset] == OP_LOOP,
                "Backward jump should convert opcode to OP_LOOP");

    uint16_t stored = (uint16_t)(buffer->instructions[loop_info->operand_offset] << 8 |
                                 buffer->instructions[loop_info->operand_offset + 1]);
    int expected = (loop_info->operand_offset + loop_info->operand_size) - loop_start;
    ASSERT_TRUE(stored == (uint16_t)expected, "Loop back edge distance mismatch");

    free_bytecode_buffer(buffer);
    return true;
}

static bool test_for_loop_multiple_patches(void) {
    BytecodeBuffer* buffer = init_bytecode_buffer();
    ASSERT_TRUE(buffer != NULL, "init_bytecode_buffer returned NULL");

    emit_byte_to_buffer(buffer, OP_JUMP);
    int break_patch_one = emit_jump_placeholder(buffer, OP_JUMP);
    ASSERT_TRUE(break_patch_one >= 0, "emit_jump_placeholder failed for break one");

    emit_byte_to_buffer(buffer, OP_JUMP);
    int break_patch_two = emit_jump_placeholder(buffer, OP_JUMP);
    ASSERT_TRUE(break_patch_two >= 0, "emit_jump_placeholder failed for break two");

    append_dummy_bytes(buffer, 6);  // body after breaks

    int loop_end = buffer->count;
    ASSERT_TRUE(patch_jump(buffer, break_patch_one, loop_end),
                "patch_jump failed for first break");
    ASSERT_TRUE(patch_jump(buffer, break_patch_two, loop_end),
                "patch_jump failed for second break");

    JumpPatch* first = &buffer->patches[break_patch_one];
    JumpPatch* second = &buffer->patches[break_patch_two];

    uint16_t first_offset = (uint16_t)(buffer->instructions[first->operand_offset] << 8 |
                                      buffer->instructions[first->operand_offset + 1]);
    uint16_t second_offset = (uint16_t)(buffer->instructions[second->operand_offset] << 8 |
                                       buffer->instructions[second->operand_offset + 1]);

    int first_expected = loop_end - (first->operand_offset + first->operand_size);
    int second_expected = loop_end - (second->operand_offset + second->operand_size);
    ASSERT_TRUE(first_offset == (uint16_t)first_expected,
                "First break patch offset mismatch");
    ASSERT_TRUE(second_offset == (uint16_t)second_expected,
                "Second break patch offset mismatch");

    free_bytecode_buffer(buffer);
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"if jump patch", test_if_jump_patch},
        {"while loop jump patch", test_while_loop_jump_patch},
        {"for loop multiple patches", test_for_loop_multiple_patches},
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

    printf("%d/%d jump patch tests passed\n", passed, total);
    return 0;
}
