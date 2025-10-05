#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
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
        case NODE_BLOCK:
            for (int i = 0; i < node->block.count; ++i) {
                annotate_ast_with_file(node->block.statements[i], file_name);
            }
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
        case NODE_UNARY:
            annotate_ast_with_file(node->unary.operand, file_name);
            break;
        default:
            break;
    }
}

static bool build_context_without_codegen(const char* source,
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

static bool chunk_contains_opcode(const BytecodeBuffer* bytecode, uint8_t opcode) {
    if (!bytecode) {
        return false;
    }
    for (int i = 0; i < bytecode->count; ++i) {
        if (bytecode->instructions[i] == opcode) {
            return true;
        }
    }
    return false;
}

static bool program_contains_opcode(const CompilerContext* ctx, uint8_t opcode) {
    if (!ctx) {
        return false;
    }
    if (chunk_contains_opcode(ctx->bytecode, opcode)) {
        return true;
    }
    if (ctx->function_chunks) {
        for (int i = 0; i < ctx->function_count; ++i) {
            if (chunk_contains_opcode(ctx->function_chunks[i], opcode)) {
                return true;
            }
        }
    }
    return false;
}

static TypedASTNode* find_first_node_by_type(TypedASTNode* node, NodeType target) {
    if (!node || !node->original) {
        return NULL;
    }
    if (node->original->type == target) {
        return node;
    }

    switch (node->original->type) {
        case NODE_PROGRAM:
            for (int i = 0; i < node->typed.program.count; ++i) {
                TypedASTNode* found = find_first_node_by_type(node->typed.program.declarations[i], target);
                if (found) {
                    return found;
                }
            }
            break;
        case NODE_BLOCK:
            for (int i = 0; i < node->typed.block.count; ++i) {
                TypedASTNode* found = find_first_node_by_type(node->typed.block.statements[i], target);
                if (found) {
                    return found;
                }
            }
            break;
        case NODE_VAR_DECL:
            return find_first_node_by_type(node->typed.varDecl.initializer, target);
        case NODE_WHILE:
            return find_first_node_by_type(node->typed.whileStmt.body, target);
        case NODE_FUNCTION:
            if (node->typed.function.body) {
                return find_first_node_by_type(node->typed.function.body, target);
            }
            break;
        case NODE_FOR_RANGE:
            return find_first_node_by_type(node->typed.forRange.body, target);
        default:
            break;
    }
    return NULL;
}

static bool test_while_loop_compound_increment_fuses(void) {
    static const char* source =
        "fn main():\n"
        "    mut i: i32 = 0\n"
        "    limit: i32 = 4\n"
        "    while i < limit:\n"
        "        i += 1\n"
        "        pass\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_without_codegen(source, "compound_increment.orus", &ctx, &typed, &ast)) {
        return false;
    }

    bool compiled = compile_to_bytecode(ctx);
    ASSERT_TRUE(compiled, "compilation should succeed");

    ASSERT_TRUE(program_contains_opcode(ctx, OP_INC_CMP_JMP),
                "expected OP_INC_CMP_JMP in bytecode for += loop");

    destroy_context(ctx, typed, ast);
    return true;
}

static bool replace_increment_with_unary(TypedASTNode* root) {
    TypedASTNode* while_node = find_first_node_by_type(root, NODE_WHILE);
    if (!while_node) {
        return false;
    }

    TypedASTNode* body = while_node->typed.whileStmt.body;
    bool body_is_block = body && body->original && body->original->type == NODE_BLOCK;
    TypedASTNode** target_slot = NULL;
    ASTNode** target_ast_slot = NULL;

    if (body_is_block) {
        if (body->typed.block.count <= 0) {
            return false;
        }
        int last_index = body->typed.block.count - 1;
        target_slot = &body->typed.block.statements[last_index];
        if (!target_slot) {
            return false;
        }
        if (body->original && body->original->type == NODE_BLOCK &&
            last_index < body->original->block.count) {
            target_ast_slot = &body->original->block.statements[last_index];
        }
    } else {
        if (!body) {
            return false;
        }
        target_slot = &while_node->typed.whileStmt.body;
        if (while_node->original && while_node->original->type == NODE_WHILE) {
            target_ast_slot = &while_node->original->whileStmt.body;
        }
    }

    if (!target_slot || !*target_slot) {
        return false;
    }

    TypedASTNode* last_stmt = *target_slot;

    TypedASTNode* condition = while_node->typed.whileStmt.condition;
    if (!condition || !condition->original || condition->original->type != NODE_BINARY) {
        return false;
    }

    ASTNode* condition_left = condition->original->binary.left;
    if (!condition_left || condition_left->type != NODE_IDENTIFIER) {
        return false;
    }
    const char* loop_name = condition_left->identifier.name;
    if (!loop_name) {
        return false;
    }

    ASTNode* unary_ast = (ASTNode*)calloc(1, sizeof(ASTNode));
    ASTNode* operand_ast = (ASTNode*)calloc(1, sizeof(ASTNode));
    TypedASTNode* unary_typed = (TypedASTNode*)calloc(1, sizeof(TypedASTNode));
    TypedASTNode* operand_typed = (TypedASTNode*)calloc(1, sizeof(TypedASTNode));
    if (!unary_ast || !operand_ast || !unary_typed || !operand_typed) {
        free(unary_ast);
        free(operand_ast);
        free(unary_typed);
        free(operand_typed);
        return false;
    }

    operand_ast->type = NODE_IDENTIFIER;
    char* loop_name_copy = NULL;
    if (loop_name) {
        size_t len = strlen(loop_name);
        loop_name_copy = (char*)malloc(len + 1);
        if (loop_name_copy) {
            memcpy(loop_name_copy, loop_name, len + 1);
        }
    }
    if (!loop_name_copy) {
        free(unary_ast);
        free(operand_ast);
        free(unary_typed);
        free(operand_typed);
        return false;
    }

    operand_ast->identifier.name = loop_name_copy;
    operand_ast->location = last_stmt->original ? last_stmt->original->location : condition_left->location;

    operand_typed->original = operand_ast;
    operand_typed->resolvedType = getPrimitiveType(TYPE_I32);
    operand_typed->typeResolved = true;

    unary_ast->type = NODE_UNARY;
    unary_ast->unary.op = "++";
    unary_ast->unary.operand = operand_ast;
    unary_ast->location = last_stmt->original ? last_stmt->original->location : condition_left->location;

    unary_typed->original = unary_ast;
    unary_typed->resolvedType = getPrimitiveType(TYPE_I32);
    unary_typed->typeResolved = true;
    unary_typed->typed.unary.operand = operand_typed;

    *target_slot = unary_typed;
    if (target_ast_slot) {
        *target_ast_slot = unary_ast;
    }
    return true;
}

static bool test_while_loop_unary_increment_fuses(void) {
    static const char* source =
        "fn main():\n"
        "    mut i: i32 = 0\n"
        "    limit: i32 = 3\n"
        "    while i < limit:\n"
        "        i += 1\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_without_codegen(source, "unary_increment.orus", &ctx, &typed, &ast)) {
        return false;
    }

    ASSERT_TRUE(replace_increment_with_unary(typed), "failed to replace increment with unary node");

    bool compiled = compile_to_bytecode(ctx);
    ASSERT_TRUE(compiled, "compilation should succeed after unary rewrite");

    ASSERT_TRUE(program_contains_opcode(ctx, OP_INC_CMP_JMP),
                "expected OP_INC_CMP_JMP in bytecode for unary ++ loop");

    destroy_context(ctx, typed, ast);
    return true;
}

static bool test_reverse_range_fuses(void) {
    static const char* source =
        "fn main():\n"
        "    mut total: i32 = 0\n"
        "    for i in 5..1..-1:\n"
        "        total = total + i\n"
        "    return total\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_without_codegen(source, "reverse_range.orus", &ctx, &typed, &ast)) {
        return false;
    }

    bool compiled = compile_to_bytecode(ctx);
    ASSERT_TRUE(compiled, "compilation should succeed for reverse range");

    ASSERT_TRUE(program_contains_opcode(ctx, OP_DEC_CMP_JMP),
                "expected OP_DEC_CMP_JMP in bytecode for reverse range loop");

    destroy_context(ctx, typed, ast);
    return true;
}

static bool test_descending_while_loop_fuses(void) {
    static const char* source =
        "fn main():\n"
        "    mut i: i32 = 5\n"
        "    while i > 0:\n"
        "        pass\n"
        "        i -= 1\n";

    CompilerContext* ctx = NULL;
    TypedASTNode* typed = NULL;
    ASTNode* ast = NULL;

    if (!build_context_without_codegen(source, "descending_while.orus", &ctx, &typed, &ast)) {
        return false;
    }

    bool compiled = compile_to_bytecode(ctx);
    ASSERT_TRUE(compiled, "compilation should succeed for descending while loop");

    ASSERT_TRUE(program_contains_opcode(ctx, OP_DEC_CMP_JMP),
                "expected OP_DEC_CMP_JMP in bytecode for while loop with -= 1");

    destroy_context(ctx, typed, ast);
    return true;
}

int main(void) {
    int passed = 0;
    int total = 4;

    if (test_while_loop_compound_increment_fuses()) {
        passed++;
    } else {
        fprintf(stderr, "test_while_loop_compound_increment_fuses failed\n");
    }

    if (test_while_loop_unary_increment_fuses()) {
        passed++;
    } else {
        fprintf(stderr, "test_while_loop_unary_increment_fuses failed\n");
    }

    if (test_reverse_range_fuses()) {
        passed++;
    } else {
        fprintf(stderr, "test_reverse_range_fuses failed\n");
    }

    if (test_descending_while_loop_fuses()) {
        passed++;
    } else {
        fprintf(stderr, "test_descending_while_loop_fuses failed\n");
    }

    printf("%d/%d fused loop codegen tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}

