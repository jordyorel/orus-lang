#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
#include "compiler/error_reporter.h"
#include "debug/debug_config.h"
#include "type/type.h"
#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

typedef struct {
    CompilerContext* ctx;
    TypedASTNode* typed;
    ASTNode* ast;
} CompiledProgram;

static bool compile_program(const char* source, const char* filename, CompiledProgram* out) {
    memset(out, 0, sizeof(*out));

    ASTNode* ast = parseSource(source);
    if (!ast) {
        return false;
    }
    ast->location.file = filename;

    init_type_inference();
    TypeEnv* env = type_env_new(NULL);
    if (!env) {
        cleanup_type_inference();
        freeAST(ast);
        return false;
    }

    TypedASTNode* typed = generate_typed_ast(ast, env);
    if (!typed) {
        cleanup_type_inference();
        freeAST(ast);
        return false;
    }

    CompilerContext* ctx = init_compiler_context(typed);
    if (!ctx) {
        cleanup_type_inference();
        free_typed_ast_node(typed);
        freeAST(ast);
        return false;
    }

    if (!compile_to_bytecode(ctx)) {
        free_compiler_context(ctx);
        cleanup_type_inference();
        free_typed_ast_node(typed);
        freeAST(ast);
        return false;
    }

    out->ctx = ctx;
    out->typed = typed;
    out->ast = ast;
    return true;
}

static void destroy_program(CompiledProgram* program) {
    if (!program) {
        return;
    }
    free_compiler_context(program->ctx);
    free_typed_ast_node(program->typed);
    freeAST(program->ast);
    cleanup_type_inference();
}

static int find_opcode(BytecodeBuffer* bytecode, uint8_t opcode, int start) {
    for (int i = start; i < bytecode->count; ++i) {
        if (bytecode->instructions[i] == opcode) {
            return i;
        }
    }
    return -1;
}

static bool contains_opcode(BytecodeBuffer* bytecode, uint8_t opcode) {
    return find_opcode(bytecode, opcode, 0) >= 0;
}

static int jump_target(BytecodeBuffer* bytecode, int index) {
    uint8_t opcode = bytecode->instructions[index];
    if (opcode == OP_JUMP) {
        int16_t offset = (int16_t)((bytecode->instructions[index + 1] << 8) |
                                   bytecode->instructions[index + 2]);
        return index + 3 + offset;
    }
    if (opcode == OP_JUMP_IF_NOT_I32_TYPED) {
        int16_t offset = (int16_t)((bytecode->instructions[index + 3] << 8) |
                                   bytecode->instructions[index + 4]);
        return index + 5 + offset;
    }
    if (opcode == OP_JUMP_IF_NOT_R) {
        int16_t offset = (int16_t)((bytecode->instructions[index + 2] << 8) |
                                   bytecode->instructions[index + 3]);
        return index + 4 + offset;
    }
    if (opcode == OP_LOOP_SHORT) {
        uint8_t back = bytecode->instructions[index + 1];
        return index + 2 - back;
    }
    if (opcode == OP_LOOP) {
        int16_t distance = (int16_t)((bytecode->instructions[index + 1] << 8) |
                                     bytecode->instructions[index + 2]);
        return index + 3 - distance;
    }
    return -1;
}

static bool verify_back_edge(BytecodeBuffer* bytecode, int search_start, int guard_index) {
    int loop_idx = find_opcode(bytecode, OP_LOOP_SHORT, search_start);
    if (loop_idx < 0) {
        loop_idx = find_opcode(bytecode, OP_JUMP, search_start);
        while (loop_idx >= 0) {
            int target = jump_target(bytecode, loop_idx);
            if (target >= 0 && target <= guard_index) {
                return true;
            }
            loop_idx = find_opcode(bytecode, OP_JUMP, loop_idx + 1);
        }
        return false;
    }
    int target = jump_target(bytecode, loop_idx);
    return target >= 0 && target <= guard_index;
}

static bool test_positive_step_guard(void) {
    static const char* source =
        "for i in 0..10..2:\n"
        "    pass\n";

    CompiledProgram program;
    if (!compile_program(source, "positive_step.orus", &program)) {
        return false;
    }

    BytecodeBuffer* bytecode = program.ctx->bytecode;

    ASSERT_TRUE(!contains_opcode(bytecode, OP_BRANCH_TYPED), "Fallback loop should avoid OP_BRANCH_TYPED");

    int guard_index = find_opcode(bytecode, OP_JUMP_IF_NOT_I32_TYPED, 0);
    ASSERT_TRUE(guard_index >= 0, "Guard should use OP_JUMP_IF_NOT_I32_TYPED");

    int increment_index = find_opcode(bytecode, OP_ADD_I32_TYPED, guard_index + 1);
    ASSERT_TRUE(increment_index >= 0, "Loop increment should use OP_ADD_I32_TYPED");

    ASSERT_TRUE(verify_back_edge(bytecode, increment_index + 1, guard_index),
                "Back edge should return to guard using OP_LOOP_SHORT/OP_JUMP");

    destroy_program(&program);
    return true;
}

static bool test_continue_targets_increment(void) {
    static const char* source =
        "for i in 0..10..2:\n"
        "    if i == 4:\n"
        "        continue\n";

    CompiledProgram program;
    if (!compile_program(source, "continue.orus", &program)) {
        return false;
    }

    BytecodeBuffer* bytecode = program.ctx->bytecode;

    ASSERT_TRUE(!contains_opcode(bytecode, OP_BRANCH_TYPED), "Continue loop should avoid OP_BRANCH_TYPED");

    int guard_index = find_opcode(bytecode, OP_JUMP_IF_NOT_I32_TYPED, 0);
    ASSERT_TRUE(guard_index >= 0, "Guard should use OP_JUMP_IF_NOT_I32_TYPED");

    int increment_index = find_opcode(bytecode, OP_ADD_I32_TYPED, guard_index + 1);
    ASSERT_TRUE(increment_index >= 0, "Loop increment should use OP_ADD_I32_TYPED");

    int continue_jump_index = -1;
    for (int i = guard_index + 1; i < bytecode->count; ++i) {
        if (bytecode->instructions[i] == OP_JUMP) {
            int target = jump_target(bytecode, i);
            if (target == increment_index) {
                continue_jump_index = i;
                break;
            }
        }
    }

    ASSERT_TRUE(continue_jump_index >= 0, "Continue jump should land on loop increment");
    ASSERT_TRUE(verify_back_edge(bytecode, increment_index + 1, guard_index),
                "Continue loop should retain back edge to guard");

    destroy_program(&program);
    return true;
}

static bool test_negative_step_guard(void) {
    static const char* source =
        "for i in 10..0..-2:\n"
        "    pass\n";

    CompiledProgram program;
    if (!compile_program(source, "negative_step.orus", &program)) {
        return false;
    }

    BytecodeBuffer* bytecode = program.ctx->bytecode;

    ASSERT_TRUE(!contains_opcode(bytecode, OP_BRANCH_TYPED), "Negative step loop should avoid OP_BRANCH_TYPED");

    int guard_index = find_opcode(bytecode, OP_JUMP_IF_NOT_I32_TYPED, 0);
    ASSERT_TRUE(guard_index >= 0, "Guard should use OP_JUMP_IF_NOT_I32_TYPED");

    int increment_index = find_opcode(bytecode, OP_ADD_I32_TYPED, guard_index + 1);
    ASSERT_TRUE(increment_index >= 0, "Negative step loop should still emit OP_ADD_I32_TYPED for the counter");

    ASSERT_TRUE(verify_back_edge(bytecode, increment_index + 1, guard_index),
                "Negative step loop should jump back to the guard");

    destroy_program(&program);
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"non-fused positive step guard", test_positive_step_guard},
        {"continue targets increment before guard", test_continue_targets_increment},
        {"non-fused negative step guard", test_negative_step_guard},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i) {
        if (tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            return 1;
        }
    }

    printf("%d/%d for-loop bytecode tests passed\n", passed, total);
    return 0;
}
