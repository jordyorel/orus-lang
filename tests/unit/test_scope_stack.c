#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/scope_stack.h"
#include "compiler/typed_ast.h"
#include "compiler/error_reporter.h"
#include "debug/debug_config.h"
#include "type/type.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool test_scope_stack_push_and_pop(void) {
    ScopeStack* stack = scope_stack_create();
    ASSERT_TRUE(stack != NULL, "scope stack should be created");
    ASSERT_TRUE(scope_stack_depth(stack) == 0, "new stack has depth 0");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 0, "new stack has no loops");

    ScopeFrame* lexical = scope_stack_push(stack, SCOPE_KIND_LEXICAL);
    ASSERT_TRUE(lexical != NULL, "able to push lexical scope");
    ASSERT_TRUE(scope_stack_depth(stack) == 1, "lexical push increments depth");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 0, "lexical scope does not change loop depth");

    ScopeFrame* loop_one = scope_stack_push(stack, SCOPE_KIND_LOOP);
    ASSERT_TRUE(loop_one != NULL, "able to push first loop scope");
    ASSERT_TRUE(scope_stack_depth(stack) == 2, "depth reflects lexical + loop");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 1, "loop depth increments when loop pushed");
    ASSERT_TRUE(loop_one->start_offset == -1, "loop frame initializes start offset");
    ASSERT_TRUE(loop_one->continue_offset == -1, "loop frame initializes continue offset");
    ASSERT_TRUE(loop_one->end_offset == -1, "loop frame initializes end offset");

    ScopeFrame* loop_two = scope_stack_push(stack, SCOPE_KIND_LOOP);
    ASSERT_TRUE(loop_two != NULL, "able to push nested loop scope");
    ASSERT_TRUE(scope_stack_depth(stack) == 3, "depth reflects nested loops");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 2, "loop depth increments for nested loop");
    ASSERT_TRUE(scope_stack_current(stack) == loop_two, "current frame is innermost loop");

    scope_stack_pop(stack); // pop loop_two
    ASSERT_TRUE(scope_stack_depth(stack) == 2, "popping nested loop reduces depth");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 1, "loop depth decremented after pop");

    scope_stack_pop(stack); // pop loop_one
    ASSERT_TRUE(scope_stack_depth(stack) == 1, "lexical scope remains after popping loops");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 0, "loop depth returns to zero");

    scope_stack_pop(stack); // pop lexical
    ASSERT_TRUE(scope_stack_depth(stack) == 0, "all scopes removed");
    ASSERT_TRUE(scope_stack_loop_depth(stack) == 0, "loop depth stays zero");

    scope_stack_destroy(stack);
    return true;
}

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

static bool test_compiler_loop_context_cleanup(void) {
    static const char* source =
        "mut total = 0\n"
        "for outer in 0..3:\n"
        "    mut running = outer\n"
        "    for inner in 0..4:\n"
        "        if inner == 1:\n"
        "            continue\n"
        "        if inner == 3:\n"
        "            break\n"
        "        running = running + inner\n"
        "    total = total + running\n"
        "print(total)\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_from_source(source, "scope_tracking.orus", &ctx, &typed, &ast)) {
        return false;
    }

    ASSERT_TRUE(ctx->scopes != NULL, "compiler context should own a scope stack");
    ASSERT_TRUE(scope_stack_depth(ctx->scopes) == 0, "no scopes remain after compilation");
    ASSERT_TRUE(scope_stack_loop_depth(ctx->scopes) == 0, "loop depth resets after compilation");
    ASSERT_TRUE(ctx->current_loop_start == -1, "current_loop_start reset after compilation");
    ASSERT_TRUE(ctx->current_loop_end == -1, "current_loop_end reset after compilation");
    ASSERT_TRUE(ctx->current_loop_continue == -1, "current_loop_continue reset after compilation");
    ASSERT_TRUE(ctx->break_count == 0, "break patch list cleared");
    ASSERT_TRUE(ctx->continue_count == 0, "continue patch list cleared");
    ASSERT_TRUE(ctx->errors != NULL, "error reporter should exist");
    ASSERT_TRUE(error_reporter_count(ctx->errors) == 0, "successful program should have no diagnostics");

    destroy_context(ctx, typed, ast);
    return true;
}

static bool test_core_attribute_preserves_intrinsic_metadata(void) {
    static const char* source =
        "@[core(\"__c_sin\")]\n"
        "fn __c_sin(x: f64) -> f64\n";

    ASTNode* ast = parseSource(source);
    if (!ast) {
        fprintf(stderr, "Failed to parse core intrinsic declaration\n");
        return false;
    }

    init_type_inference();
    TypeEnv* env = type_env_new(NULL);
    if (!env) {
        fprintf(stderr, "Failed to allocate type environment for intrinsic declaration\n");
        freeAST(ast);
        cleanup_type_inference();
        return false;
    }

    TypedASTNode* typed = generate_typed_ast(ast, env);
    if (!typed) {
        fprintf(stderr, "Failed to build typed AST for intrinsic declaration\n");
        freeAST(ast);
        cleanup_type_inference();
        return false;
    }

    bool success = true;

    if (ast->type != NODE_PROGRAM || ast->program.count != 1) {
        fprintf(stderr, "Intrinsic test expected exactly one top-level declaration\n");
        success = false;
        goto cleanup;
    }

    ASTNode* fn = ast->program.declarations[0];
    if (!fn || fn->type != NODE_FUNCTION) {
        fprintf(stderr, "Intrinsic test expected a function declaration\n");
        success = false;
        goto cleanup;
    }

    if (!fn->function.hasCoreIntrinsic) {
        fprintf(stderr, "Parser did not record @[core] attribute on function\n");
        success = false;
        goto cleanup;
    }

    if (!fn->function.coreIntrinsicSymbol || strcmp(fn->function.coreIntrinsicSymbol, "__c_sin") != 0) {
        fprintf(stderr, "Parser stored incorrect intrinsic symbol (got '%s')\n",
                fn->function.coreIntrinsicSymbol ? fn->function.coreIntrinsicSymbol : "<null>");
        success = false;
        goto cleanup;
    }

    if (!typed->original || typed->original->type != NODE_PROGRAM || typed->typed.program.count != 1) {
        fprintf(stderr, "Typed AST expected exactly one declaration\n");
        success = false;
        goto cleanup;
    }

    TypedASTNode* typed_fn = typed->typed.program.declarations[0];
    if (!typed_fn || !typed_fn->original || typed_fn->original != fn) {
        fprintf(stderr, "Typed AST did not map function node correctly\n");
        success = false;
        goto cleanup;
    }

    if (!typed_fn->typed.function.isCoreIntrinsic) {
        fprintf(stderr, "Typed AST lost intrinsic attribute metadata\n");
        success = false;
        goto cleanup;
    }

    if (!typed_fn->typed.function.coreIntrinsicSymbol ||
        strcmp(typed_fn->typed.function.coreIntrinsicSymbol, "__c_sin") != 0) {
        fprintf(stderr, "Typed AST stored incorrect intrinsic symbol (got '%s')\n",
                typed_fn->typed.function.coreIntrinsicSymbol ?
                    typed_fn->typed.function.coreIntrinsicSymbol : "<null>");
        success = false;
        goto cleanup;
    }

    if (!typed_fn->typed.function.intrinsicSignature) {
        fprintf(stderr, "Typed AST missing intrinsic signature metadata\n");
        success = false;
        goto cleanup;
    }

    if (strcmp(typed_fn->typed.function.intrinsicSignature->symbol, "__c_sin") != 0) {
        fprintf(stderr, "Intrinsic signature stored unexpected symbol '%s'\n",
                typed_fn->typed.function.intrinsicSignature->symbol);
        success = false;
        goto cleanup;
    }

    if (typed_fn->typed.function.intrinsicSignature->paramCount != 1 ||
        typed_fn->typed.function.intrinsicSignature->paramTypes[0] != TYPE_F64 ||
        typed_fn->typed.function.intrinsicSignature->returnType != TYPE_F64) {
        fprintf(stderr, "Intrinsic signature did not record expected f64 -> f64 mapping\n");
        success = false;
        goto cleanup;
    }

cleanup:
    free_typed_ast_node(typed);
    freeAST(ast);
    cleanup_type_inference();
    return success;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"scope stack push/pop maintains loop depth", test_scope_stack_push_and_pop},
        {"compiler loop context resets after nested loops", test_compiler_loop_context_cleanup},
        {"core attribute metadata survives parsing and typing", test_core_attribute_preserves_intrinsic_metadata},
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

    printf("%d/%d scope tracking tests passed\n", passed, total);
    return 0;
}
