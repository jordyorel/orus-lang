#include <stdbool.h>
#include <stdio.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
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

static void annotate_ast_with_file(ASTNode* node, const char* file_name) {
    if (!node) {
        return;
    }

    node->location.file = file_name;

    switch (node->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->program.count; ++i) {
                annotate_ast_with_file(node->program.declarations[i], file_name);
            }
            break;
        case NODE_VAR_DECL:
            annotate_ast_with_file(node->varDecl.initializer, file_name);
            annotate_ast_with_file(node->varDecl.typeAnnotation, file_name);
            break;
        case NODE_ASSIGN:
            annotate_ast_with_file(node->assign.value, file_name);
            break;
        case NODE_PRINT:
            for (int i = 0; i < node->print.count; ++i) {
                annotate_ast_with_file(node->print.values[i], file_name);
            }
            annotate_ast_with_file(node->print.separator, file_name);
            break;
        case NODE_BINARY:
            annotate_ast_with_file(node->binary.left, file_name);
            annotate_ast_with_file(node->binary.right, file_name);
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; ++i) {
                annotate_ast_with_file(node->block.statements[i], file_name);
            }
            break;
        case NODE_IF:
            annotate_ast_with_file(node->ifStmt.condition, file_name);
            annotate_ast_with_file(node->ifStmt.thenBranch, file_name);
            annotate_ast_with_file(node->ifStmt.elseBranch, file_name);
            break;
        case NODE_WHILE:
            annotate_ast_with_file(node->whileStmt.condition, file_name);
            annotate_ast_with_file(node->whileStmt.body, file_name);
            break;
        case NODE_FOR_RANGE:
            annotate_ast_with_file(node->forRange.start, file_name);
            annotate_ast_with_file(node->forRange.end, file_name);
            annotate_ast_with_file(node->forRange.step, file_name);
            annotate_ast_with_file(node->forRange.body, file_name);
            break;
        case NODE_FOR_ITER:
            annotate_ast_with_file(node->forIter.iterable, file_name);
            annotate_ast_with_file(node->forIter.body, file_name);
            break;
        case NODE_TERNARY:
            annotate_ast_with_file(node->ternary.condition, file_name);
            annotate_ast_with_file(node->ternary.trueExpr, file_name);
            annotate_ast_with_file(node->ternary.falseExpr, file_name);
            break;
        case NODE_UNARY:
            annotate_ast_with_file(node->unary.operand, file_name);
            break;
        case NODE_FUNCTION:
            annotate_ast_with_file(node->function.body, file_name);
            break;
        case NODE_CALL:
            annotate_ast_with_file(node->call.callee, file_name);
            for (int i = 0; i < node->call.argCount; ++i) {
                annotate_ast_with_file(node->call.args[i], file_name);
            }
            break;
        case NODE_RETURN:
            annotate_ast_with_file(node->returnStmt.value, file_name);
            break;
        case NODE_CAST:
            annotate_ast_with_file(node->cast.expression, file_name);
            annotate_ast_with_file(node->cast.targetType, file_name);
            break;
        default:
            break;
    }
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

    annotate_ast_with_file(ast, file_name);

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

static bool test_source_mapping_tracks_original_locations(void) {
    static const char* source =
        "x = 42\n"
        "print(x)\n";
    static const char* file_name = "test_source.orus";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_from_source(source, file_name, &ctx, &typed, &ast)) {
        return false;
    }

    BytecodeBuffer* bytecode = ctx->bytecode;
    ASSERT_TRUE(bytecode != NULL, "bytecode buffer should be initialized");
    ASSERT_TRUE(bytecode->count > 0, "bytecode buffer must contain instructions");

    bool saw_line_one = false;
    bool saw_line_two = false;
    bool halt_has_sentinel = false;

    for (int i = 0; i < bytecode->count; ++i) {
        int line = bytecode->source_lines ? bytecode->source_lines[i] : -1;
        int column = bytecode->source_columns ? bytecode->source_columns[i] : -1;
        const char* file = bytecode->source_files ? bytecode->source_files[i] : NULL;

        if (line == 1) {
            saw_line_one = true;
            ASSERT_TRUE(column >= 0, "line 1 instructions should record a column");
            ASSERT_TRUE(file == file_name, "line 1 metadata should retain the source file");
        } else if (line == 2) {
            saw_line_two = true;
            ASSERT_TRUE(column >= 0, "line 2 instructions should record a column");
            ASSERT_TRUE(file == file_name, "line 2 metadata should retain the source file");
        }

        if (bytecode->instructions[i] == OP_HALT) {
            halt_has_sentinel = (line == -1) && (column == -1) && (file == NULL);
        }
    }

    ASSERT_TRUE(saw_line_one, "expected at least one instruction attributed to line 1");
    ASSERT_TRUE(saw_line_two, "expected at least one instruction attributed to line 2");
    ASSERT_TRUE(halt_has_sentinel, "HALT instruction should use synthetic sentinel metadata");

    destroy_context(ctx, typed, ast);
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"source mapping retains line and column information", test_source_mapping_tracks_original_locations},
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

    printf("%d/%d source mapping tests passed\n", passed, total);
    return 0;
}

