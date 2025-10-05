#include <stdbool.h>
#include <stdio.h>
#include <string.h>

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

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"fused while/for bytecode identity", test_fused_loop_bytecode_identity},
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
