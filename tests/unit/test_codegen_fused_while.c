#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
#include "compiler/error_reporter.h"
#include "debug/debug_config.h"
#include "type/type.h"
#include "vm/vm_constants.h"
#include "tools/debug.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool build_context_from_source(const char* source,
                                      const char* file_name,
                                      CompilerContext** out_ctx,
                                      TypedASTNode** out_typed,
                                      ASTNode** out_ast) {
    if (!source || !file_name || !out_ctx || !out_typed || !out_ast) {
        return false;
    }

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

static void debug_disassemble_bytecode(const CompilerContext* ctx, const char* name) {
    if (!ctx || !ctx->bytecode) {
        return;
    }

    Chunk chunk = {0};
    chunk.count = ctx->bytecode->count;
    chunk.capacity = ctx->bytecode->capacity;
    chunk.code = ctx->bytecode->instructions;

    int* lines_stub = NULL;
    int* cols_stub = NULL;
    const char** files_stub = NULL;

    if (ctx->bytecode->source_lines) {
        chunk.lines = ctx->bytecode->source_lines;
    } else if (chunk.count > 0) {
        lines_stub = calloc((size_t)chunk.count, sizeof(int));
        if (!lines_stub) {
            return;
        }
        chunk.lines = lines_stub;
    }

    if (ctx->bytecode->source_columns) {
        chunk.columns = ctx->bytecode->source_columns;
    } else if (chunk.count > 0) {
        cols_stub = calloc((size_t)chunk.count, sizeof(int));
        if (!cols_stub) {
            free(lines_stub);
            return;
        }
        chunk.columns = cols_stub;
    }

    if (ctx->bytecode->source_files) {
        chunk.files = ctx->bytecode->source_files;
    } else if (chunk.count > 0) {
        files_stub = calloc((size_t)chunk.count, sizeof(const char*));
        if (!files_stub) {
            free(lines_stub);
            free(cols_stub);
            return;
        }
        chunk.files = files_stub;
    }

    if (ctx->constants) {
        chunk.constants.count = ctx->constants->count;
        chunk.constants.capacity = ctx->constants->capacity;
        chunk.constants.values = ctx->constants->values;
    } else {
        chunk.constants.count = 0;
        chunk.constants.capacity = 0;
        chunk.constants.values = NULL;
    }

    disassembleChunk(&chunk, name ? name : "bytecode");

    free(lines_stub);
    free(cols_stub);
    free(files_stub);
}

static bool test_fused_while_primes_once(void) {
    static const char* source =
        "mut limit = 5\n"
        "mut i = 0\n"
        "mut total = 0\n"
        "while i < limit:\n"
        "    total = total + i\n"
        "    i = i + 1\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_from_source(source, "fused_while.orus", &ctx, &typed, &ast)) {
        return false;
    }

    BytecodeBuffer* bytecode = ctx->bytecode;
    if (!bytecode || bytecode->count <= 0) {
        fprintf(stderr, "bytecode buffer unavailable or empty\n");
        destroy_context(ctx, typed, ast);
        return false;
    }

    int guard_index = -1;
    int loop_reg = -1;
    int limit_reg = -1;

    for (int i = 0; i < bytecode->count; ++i) {
        if (bytecode->instructions[i] == OP_JUMP_IF_NOT_I32_TYPED) {
            guard_index = i;
            if (i + 2 < bytecode->count) {
                loop_reg = bytecode->instructions[i + 1];
                limit_reg = bytecode->instructions[i + 2];
            }
            break;
        }
    }

    if (guard_index < 0 || loop_reg < 0 || limit_reg < 0) {
        fprintf(stderr,
                "failed to locate fused guard (guard_index=%d loop_reg=%d limit_reg=%d)\n",
                guard_index, loop_reg, limit_reg);
        int dump = bytecode->count < 64 ? bytecode->count : 64;
        fprintf(stderr, "bytecode dump (%d bytes):", dump);
        for (int i = 0; i < dump; ++i) {
            fprintf(stderr, " %02x", bytecode->instructions[i]);
        }
        fprintf(stderr, "\n");
        debug_disassemble_bytecode(ctx, "fused_while_debug");
        destroy_context(ctx, typed, ast);
        return false;
    }

    bool inc_cmp_found = false;
    for (int i = guard_index; i < bytecode->count; ++i) {
        if (bytecode->instructions[i] == OP_INC_CMP_JMP) {
            if (i + 2 >= bytecode->count) {
                destroy_context(ctx, typed, ast);
                return false;
            }
            if (bytecode->instructions[i + 1] != loop_reg ||
                bytecode->instructions[i + 2] != limit_reg) {
                fprintf(stderr,
                        "OP_INC_CMP_JMP registers mismatch (expected loop=%d limit=%d, got loop=%d limit=%d)\n",
                        loop_reg, limit_reg,
                        bytecode->instructions[i + 1],
                        bytecode->instructions[i + 2]);
                destroy_context(ctx, typed, ast);
                return false;
            }
            inc_cmp_found = true;
            break;
        }
    }

    if (!inc_cmp_found) {
        fprintf(stderr, "OP_INC_CMP_JMP not found in fused while bytecode\n");
        destroy_context(ctx, typed, ast);
        return false;
    }

    int loop_move_count = 0;
    int limit_move_count = 0;
    for (int i = 0; i + 2 < bytecode->count; ++i) {
        if (bytecode->instructions[i] == OP_MOVE_I32) {
            int dst = bytecode->instructions[i + 1];
            int src = bytecode->instructions[i + 2];
            if (dst == loop_reg && src == loop_reg) {
                loop_move_count++;
            }
            if (dst == limit_reg && src == limit_reg) {
                limit_move_count++;
            }
        }
    }

    bool success = (loop_move_count == 1) && (limit_move_count == 1);
    if (!success) {
        fprintf(stderr,
                "loop_move_count=%d limit_move_count=%d (loop_reg=%d limit_reg=%d)\n",
                loop_move_count, limit_move_count, loop_reg, limit_reg);
    }

    destroy_context(ctx, typed, ast);
    return success;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"fused while primes loop and limit once", test_fused_while_primes_once},
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

    printf("%d/%d fused while codegen tests passed\n", passed, total);
    return 0;
}
