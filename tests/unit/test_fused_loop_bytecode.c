#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
#include "compiler/error_reporter.h"
#include "debug/debug_config.h"
#include "type/type.h"
#include "vm/vm_constants.h"

#define ASSERT_TRUE(cond, message)                                                      \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                              \
        }                                                                              \
    } while (0)

static bool build_context_from_source(const char* source,
                                      const char* file_name,
                                      CompilerContext** out_ctx,
                                      TypedASTNode** out_typed,
                                      ASTNode** out_ast) {
    ASTNode* ast = parseSource(source);
    if (!ast) {
        return false;
    }

    ast->location.file = file_name;

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

    *out_ctx = ctx;
    *out_typed = typed;
    *out_ast = ast;
    return true;
}

static void destroy_context(CompilerContext* ctx, TypedASTNode* typed, ASTNode* ast) {
    free_compiler_context(ctx);
    free_typed_ast_node(typed);
    freeAST(ast);
    cleanup_type_inference();
}

static bool extract_guard_and_inc(BytecodeBuffer* bytecode,
                                  uint8_t guard_out[5],
                                  uint8_t inc_out[5]) {
    ASSERT_TRUE(bytecode != NULL, "bytecode buffer must exist");
    ASSERT_TRUE(bytecode->instructions != NULL, "bytecode must contain instructions");

    int guard_index = -1;
    int inc_index = -1;

    for (int i = 0; i < bytecode->count; i++) {
        if (bytecode->instructions[i] == OP_JUMP_IF_NOT_I32_TYPED) {
            guard_index = i;
            break;
        }
    }

    for (int i = 0; i < bytecode->count; i++) {
        if (bytecode->instructions[i] == OP_INC_CMP_JMP) {
            inc_index = i;
            break;
        }
    }

    ASSERT_TRUE(guard_index >= 0, "failed to find fused guard opcode");
    ASSERT_TRUE(inc_index >= 0, "failed to find fused increment opcode");
    ASSERT_TRUE(guard_index + 5 <= bytecode->count, "guard instruction truncated");
    ASSERT_TRUE(inc_index + 5 <= bytecode->count, "increment instruction truncated");

    memcpy(guard_out, &bytecode->instructions[guard_index], 5);
    memcpy(inc_out, &bytecode->instructions[inc_index], 5);
    return true;
}

static bool test_fused_loop_bytecode_identity(void) {
    static const char* while_source =
        "mut limit = 10\n"
        "mut i = 0\n"
        "while i < limit:\n"
        "    i = i + 1\n";

    static const char* for_source =
        "mut limit = 10\n"
        "for i in 0..limit:\n"
        "    i = i";

    CompilerContext* while_ctx = NULL;
    TypedASTNode* while_typed = NULL;
    ASTNode* while_ast = NULL;

    CompilerContext* for_ctx = NULL;
    TypedASTNode* for_typed = NULL;
    ASTNode* for_ast = NULL;

    if (!build_context_from_source(while_source, "fused_while.orus",
                                   &while_ctx, &while_typed, &while_ast)) {
        return false;
    }

    if (!build_context_from_source(for_source, "fused_for.orus",
                                   &for_ctx, &for_typed, &for_ast)) {
        destroy_context(while_ctx, while_typed, while_ast);
        return false;
    }

    BytecodeBuffer* while_bc = while_ctx->bytecode;
    BytecodeBuffer* for_bc = for_ctx->bytecode;

    uint8_t while_guard[5];
    uint8_t while_inc[5];
    uint8_t for_guard[5];
    uint8_t for_inc[5];

    bool extracted_while = extract_guard_and_inc(while_bc, while_guard, while_inc);
    bool extracted_for = extract_guard_and_inc(for_bc, for_guard, for_inc);

    destroy_context(for_ctx, for_typed, for_ast);
    destroy_context(while_ctx, while_typed, while_ast);

    ASSERT_TRUE(extracted_while, "failed to extract fused while sequence");
    ASSERT_TRUE(extracted_for, "failed to extract fused for sequence");

    ASSERT_TRUE(while_guard[0] == OP_JUMP_IF_NOT_I32_TYPED,
                "while guard opcode mismatch");
    ASSERT_TRUE(while_inc[0] == OP_INC_CMP_JMP, "while increment opcode mismatch");
    ASSERT_TRUE(while_guard[1] == while_inc[1],
                "while loop register inconsistent between guard and increment");
    ASSERT_TRUE(while_guard[2] == while_inc[2],
                "while limit register inconsistent between guard and increment");

    ASSERT_TRUE(for_guard[0] == OP_JUMP_IF_NOT_I32_TYPED,
                "for guard opcode mismatch");
    ASSERT_TRUE(for_inc[0] == OP_INC_CMP_JMP, "for increment opcode mismatch");
    ASSERT_TRUE(for_guard[1] == for_inc[1],
                "for loop register inconsistent between guard and increment");
    ASSERT_TRUE(for_guard[2] == for_inc[2],
                "for limit register inconsistent between guard and increment");

    ASSERT_TRUE(while_guard[0] == for_guard[0],
                "guard opcode differs between while and for loops");
    ASSERT_TRUE(while_inc[0] == for_inc[0],
                "increment opcode differs between while and for loops");

    return true;
}

static bool test_fused_loop_back_edge_fallback(void) {
    const int repeat_count = 2048;
    const char* header =
        "mut limit = 100000\n"
        "mut i = 0\n"
        "mut acc = 0\n"
        "while i < limit:\n";
    const char* body_line = "    acc = acc + i + limit + acc + i + limit\n";
    const char* increment_line = "    i = i + 1\n";

    size_t total_length = strlen(header) + (size_t)repeat_count * strlen(body_line) +
                          strlen(increment_line) + 1;
    char* source = (char*)malloc(total_length);
    ASSERT_TRUE(source != NULL, "failed to allocate large loop source");

    char* cursor = source;
    size_t header_len = strlen(header);
    memcpy(cursor, header, header_len);
    cursor += header_len;
    size_t body_len = strlen(body_line);
    for (int i = 0; i < repeat_count; ++i) {
        memcpy(cursor, body_line, body_len);
        cursor += body_len;
    }
    size_t inc_len = strlen(increment_line);
    memcpy(cursor, increment_line, inc_len);
    cursor += inc_len;
    *cursor = '\0';

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    bool build_ok = build_context_from_source(source, "fused_while_large.orus",
                                              &ctx, &typed, &ast);
    free(source);
    ASSERT_TRUE(build_ok, "failed to compile large fused while source");

    BytecodeBuffer* bc = ctx->bytecode;
    ASSERT_TRUE(bc != NULL, "bytecode buffer missing for large while");
    ASSERT_TRUE(bc->count > (INT16_MAX + 1024), "bytecode not large enough to test back edge");

    bool has_fused_increment = false;
    for (int i = 0; i < bc->count; ++i) {
        if (bc->instructions[i] == OP_INC_CMP_JMP || bc->instructions[i] == OP_DEC_CMP_JMP) {
            has_fused_increment = true;
            break;
        }
    }

    destroy_context(ctx, typed, ast);

    ASSERT_TRUE(!has_fused_increment, "compiler emitted fused increment for oversized loop");
    return true;
}

static bool test_for_range_back_edge_fallback(void) {
    const int repeat_count = 2048;
    const char* header =
        "mut limit = 100000\n"
        "mut acc = 0\n"
        "for i in 0..limit:\n";
    const char* body_line = "    acc = acc + i + limit + acc + i + limit\n";

    size_t total_length = strlen(header) + (size_t)repeat_count * strlen(body_line) + 1;
    char* source = (char*)malloc(total_length);
    ASSERT_TRUE(source != NULL, "failed to allocate large for-range source");

    char* cursor = source;
    size_t header_len = strlen(header);
    memcpy(cursor, header, header_len);
    cursor += header_len;

    size_t body_len = strlen(body_line);
    for (int i = 0; i < repeat_count; ++i) {
        memcpy(cursor, body_line, body_len);
        cursor += body_len;
    }

    *cursor = '\0';

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    bool build_ok = build_context_from_source(source, "fused_for_large.orus",
                                              &ctx, &typed, &ast);
    free(source);
    ASSERT_TRUE(build_ok, "failed to compile large for-range source");

    BytecodeBuffer* bc = ctx->bytecode;
    ASSERT_TRUE(bc != NULL, "bytecode buffer missing for large for-range");

    bool has_fused_increment = false;
    for (int i = 0; i < bc->count; ++i) {
        if (bc->instructions[i] == OP_INC_CMP_JMP || bc->instructions[i] == OP_DEC_CMP_JMP) {
            has_fused_increment = true;
            break;
        }
    }

    destroy_context(ctx, typed, ast);

    ASSERT_TRUE(!has_fused_increment, "compiler emitted fused increment for oversized for-range loop");
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"fused while/for bytecode identity", test_fused_loop_bytecode_identity},
        {"fused while falls back when back edge exceeds INT16", test_fused_loop_back_edge_fallback},
        {"fused for-range falls back when back edge exceeds INT16", test_for_range_back_edge_fallback},
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

    printf("%d/%d fused loop bytecode tests passed\n", passed, total);
    return 0;
}
