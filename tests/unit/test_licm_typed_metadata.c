#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
#include "debug/debug_config.h"
#include "type/type.h"

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

static bool is_power_of_two(uint32_t value) {
    return value != 0u && (value & (value - 1u)) == 0u;
}

static bool test_single_loop_guard_metadata(void) {
    static const char* source =
        "mut threshold: i32 = 6\n"
        "mut base_guard: bool = threshold < 12\n"
        "mut result: i32 = 0\n"
        "mut index: i32 = 0\n"
        "while index < threshold:\n"
        "    mut typed_guard: bool = base_guard\n"
        "    mut fused_guard: bool = typed_guard and base_guard\n"
        "    if fused_guard:\n"
        "        result = result + index\n"
        "    index = index + 1\n"
        "print(result)\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;
    if (!build_context_from_source(source, "licm_metadata.orus", &ctx, &typed, &ast)) {
        return false;
    }

    TypedASTNode* program = ctx->optimized_ast;
    ASSERT_TRUE(program != NULL, "optimized program should exist");
    ASSERT_TRUE(program->original && program->original->type == NODE_PROGRAM,
                "optimized root must be program node");

    TypedASTNode** decls = program->typed.program.declarations;
    int count = program->typed.program.count;
    ASSERT_TRUE(decls != NULL && count > 0, "program should contain declarations");

    TypedASTNode* guard_primary = NULL;
    TypedASTNode* guard_secondary = NULL;
    TypedASTNode* loop = NULL;

    for (int i = 0; i < count; ++i) {
        ASTNode* original = decls[i] ? decls[i]->original : NULL;
        if (!original) {
            continue;
        }
        if (original->type == NODE_VAR_DECL && original->varDecl.name) {
            if (strcmp(original->varDecl.name, "typed_guard") == 0) {
                guard_primary = decls[i];
            } else if (strcmp(original->varDecl.name, "fused_guard") == 0) {
                guard_secondary = decls[i];
            }
        } else if (original->type == NODE_WHILE && !loop) {
            loop = decls[i];
        }
    }

    ASSERT_TRUE(guard_primary != NULL, "primary guard should be hoisted to program level");
    ASSERT_TRUE(guard_secondary != NULL, "secondary guard should be hoisted to program level");
    ASSERT_TRUE(loop != NULL, "loop should remain in program declarations");

    uint32_t primary_mask = guard_primary->typed_escape_mask;
    uint32_t secondary_mask = guard_secondary->typed_escape_mask;
    ASSERT_TRUE(is_power_of_two(primary_mask), "primary guard mask must be power-of-two");
    ASSERT_TRUE(is_power_of_two(secondary_mask), "secondary guard mask must be power-of-two");
    ASSERT_TRUE(primary_mask != secondary_mask, "guard masks must be unique");

    ASSERT_TRUE(guard_primary->typed_guard_witness, "primary guard witness must survive");
    ASSERT_TRUE(guard_primary->typed_metadata_stable, "primary guard metadata should be stable");
    ASSERT_TRUE(guard_secondary->typed_guard_witness, "secondary guard witness must survive");
    ASSERT_TRUE(guard_secondary->typed_metadata_stable, "secondary guard metadata should be stable");

    ASSERT_TRUE(guard_secondary->typed.varDecl.initializer != NULL,
                "fused guard initializer should remain");
    ASSERT_TRUE(guard_secondary->typed.varDecl.initializer->original &&
                    guard_secondary->typed.varDecl.initializer->original->type == NODE_IDENTIFIER,
                "fused guard initializer should collapse to identifier");
    ASSERT_TRUE(strcmp(guard_secondary->typed.varDecl.initializer->original->identifier.name,
                       "typed_guard") == 0,
                "fused guard initializer must reference primary guard");

    ASSERT_TRUE(loop->typed_guard_witness, "loop should expose guard witness");
    ASSERT_TRUE(loop->typed_metadata_stable, "loop metadata should be marked stable");
    ASSERT_TRUE(loop->typed_escape_mask == (primary_mask | secondary_mask),
                "loop escape mask should fuse guard masks");

    TypedASTNode* loop_body = loop->typed.whileStmt.body;
    ASSERT_TRUE(loop_body != NULL && loop_body->original &&
                    loop_body->original->type == NODE_BLOCK,
                "loop body should remain a block");

    if (loop_body->typed.block.statements && loop_body->typed.block.count > 0) {
        for (int i = 0; i < loop_body->typed.block.count; ++i) {
            TypedASTNode* stmt = loop_body->typed.block.statements[i];
            if (!stmt || !stmt->original) {
                continue;
            }
            ASSERT_TRUE(!(stmt->original->type == NODE_VAR_DECL &&
                          stmt->original->varDecl.name &&
                          (strcmp(stmt->original->varDecl.name, "typed_guard") == 0 ||
                           strcmp(stmt->original->varDecl.name, "fused_guard") == 0)),
                        "hoisted guards should be removed from loop body");
        }
    }

    destroy_context(ctx, typed, ast);
    return true;
}

static bool test_nested_loop_guard_metadata(void) {
    static const char* source =
        "mut limit: i32 = 4\n"
        "mut base_guard: bool = limit < 10\n"
        "mut total: i32 = 0\n"
        "mut outer: i32 = 0\n"
        "while outer < limit:\n"
        "    mut outer_guard: bool = base_guard\n"
        "    mut fused_outer: bool = outer_guard and base_guard\n"
        "    mut inner: i32 = 0\n"
        "    while inner < limit:\n"
        "        mut inner_guard: bool = fused_outer\n"
        "        if inner_guard:\n"
        "            total = total + outer + inner\n"
        "        inner = inner + 1\n"
        "    outer = outer + 1\n"
        "print(total)\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;
    if (!build_context_from_source(source, "licm_nested.orus", &ctx, &typed, &ast)) {
        return false;
    }

    TypedASTNode* program = ctx->optimized_ast;
    ASSERT_TRUE(program && program->original && program->original->type == NODE_PROGRAM,
                "program root must exist");

    TypedASTNode** decls = program->typed.program.declarations;
    int count = program->typed.program.count;
    ASSERT_TRUE(decls && count > 0, "program should have declarations after optimization");

    TypedASTNode* outer_guard = NULL;
    TypedASTNode* fused_outer = NULL;
    TypedASTNode* outer_loop = NULL;

    for (int i = 0; i < count; ++i) {
        ASTNode* original = decls[i] ? decls[i]->original : NULL;
        if (!original) {
            continue;
        }
        if (original->type == NODE_VAR_DECL && original->varDecl.name) {
            if (strcmp(original->varDecl.name, "outer_guard") == 0) {
                outer_guard = decls[i];
            } else if (strcmp(original->varDecl.name, "fused_outer") == 0) {
                fused_outer = decls[i];
            }
        } else if (original->type == NODE_WHILE && !outer_loop) {
            outer_loop = decls[i];
        }
    }

    ASSERT_TRUE(outer_guard && fused_outer && outer_loop,
                "outer loop and hoisted guards should be discoverable");

    ASSERT_TRUE(fused_outer->typed.varDecl.initializer &&
                    fused_outer->typed.varDecl.initializer->original &&
                    fused_outer->typed.varDecl.initializer->original->type == NODE_IDENTIFIER,
                "fused outer guard initializer should collapse to identifier");
    ASSERT_TRUE(strcmp(fused_outer->typed.varDecl.initializer->original->identifier.name,
                       "outer_guard") == 0,
                "fused outer guard should reference primary guard binding");

    uint32_t outer_primary_mask = outer_guard->typed_escape_mask;
    uint32_t outer_secondary_mask = fused_outer->typed_escape_mask;
    ASSERT_TRUE(is_power_of_two(outer_primary_mask), "outer guard mask should be power-of-two");
    ASSERT_TRUE(is_power_of_two(outer_secondary_mask), "outer fused guard mask should be power-of-two");
    ASSERT_TRUE(outer_primary_mask != outer_secondary_mask,
                "outer guard masks should be distinct");

    ASSERT_TRUE(outer_loop->typed_escape_mask ==
                    (outer_primary_mask | outer_secondary_mask),
                "outer loop mask must combine hoisted guard masks");

    TypedASTNode* outer_body = outer_loop->typed.whileStmt.body;
    ASSERT_TRUE(outer_body && outer_body->original && outer_body->original->type == NODE_BLOCK,
                "outer loop body should remain a block");

    TypedASTNode** outer_statements = outer_body->typed.block.statements;
    int outer_count = outer_body->typed.block.count;
    ASSERT_TRUE(outer_statements && outer_count >= 2,
                "outer loop body should contain inner initialization and loop");

    TypedASTNode* hoisted_inner_guard = NULL;
    TypedASTNode* inner_loop = NULL;

    for (int i = 0; i < outer_count; ++i) {
        TypedASTNode* stmt = outer_statements[i];
        if (!stmt || !stmt->original) {
            continue;
        }
        if (stmt->original->type == NODE_VAR_DECL && stmt->original->varDecl.name &&
            strcmp(stmt->original->varDecl.name, "inner_guard") == 0) {
            hoisted_inner_guard = stmt;
        } else if (stmt->original->type == NODE_WHILE && !inner_loop) {
            inner_loop = stmt;
        }
    }

    ASSERT_TRUE(hoisted_inner_guard && inner_loop,
                "inner guard should be hoisted directly before inner loop");
    ASSERT_TRUE(hoisted_inner_guard->typed.varDecl.initializer &&
                    hoisted_inner_guard->typed.varDecl.initializer->original &&
                    hoisted_inner_guard->typed.varDecl.initializer->original->type == NODE_IDENTIFIER,
                "inner guard initializer should collapse to identifier");
    ASSERT_TRUE(strcmp(hoisted_inner_guard->typed.varDecl.initializer->original->identifier.name,
                       "fused_outer") == 0,
                "inner guard should reference fused outer guard binding");
    ASSERT_TRUE(is_power_of_two(hoisted_inner_guard->typed_escape_mask),
                "inner guard mask should be representable");
    ASSERT_TRUE(inner_loop->typed_escape_mask == hoisted_inner_guard->typed_escape_mask,
                "inner loop mask should match hoisted guard mask");
    ASSERT_TRUE(inner_loop->typed_guard_witness,
                "inner loop must retain typed guard witness");
    ASSERT_TRUE(inner_loop->typed_metadata_stable,
                "inner loop metadata should be stable after LICM");

    TypedASTNode* inner_body = inner_loop->typed.whileStmt.body;
    ASSERT_TRUE(inner_body && inner_body->original && inner_body->original->type == NODE_BLOCK,
                "inner loop body should remain a block");

    if (inner_body->typed.block.statements && inner_body->typed.block.count > 0) {
        for (int i = 0; i < inner_body->typed.block.count; ++i) {
            TypedASTNode* stmt = inner_body->typed.block.statements[i];
            if (!stmt || !stmt->original) {
                continue;
            }
            ASSERT_TRUE(!(stmt->original->type == NODE_VAR_DECL &&
                          stmt->original->varDecl.name &&
                          strcmp(stmt->original->varDecl.name, "inner_guard") == 0),
                        "inner guard should not remain inside loop body after hoisting");
        }
    }

    destroy_context(ctx, typed, ast);
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"single loop guard metadata survives LICM", test_single_loop_guard_metadata},
        {"nested loop guard metadata survives LICM", test_nested_loop_guard_metadata},
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

    printf("%d/%d LICM metadata tests passed\n", passed, total);
    return 0;
}
