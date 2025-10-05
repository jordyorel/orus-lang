#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/optimization/constantfold.h"
#include "compiler/typed_ast.h"
#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                        \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            exit(1);                                                                      \
        }                                                                                 \
    } while (0)

typedef struct {
    TypedASTNode* root;
    TypedASTNode* binaries[64];
    int binary_count;
} FoldFixture;

static bool tested_node_types[NODE_MATCH_EXPRESSION + 1];

static ASTNode* new_ast_node(NodeType type) {
    ASTNode* node = (ASTNode*)calloc(1, sizeof(ASTNode));
    ASSERT_TRUE(node != NULL, "allocate AST node");
    node->type = type;
    return node;
}

static ASTNode* make_literal_ast(int value) {
    ASTNode* lit = new_ast_node(NODE_LITERAL);
    lit->literal.value = I32_VAL(value);
    lit->literal.hasExplicitSuffix = false;
    return lit;
}

static TypedASTNode* make_binary_expr(int left_value, const char* op,
                                      int right_value) {
    ASTNode* left_ast = make_literal_ast(left_value);
    ASTNode* right_ast = make_literal_ast(right_value);
    ASTNode* binary_ast = new_ast_node(NODE_BINARY);
    binary_ast->binary.left = left_ast;
    binary_ast->binary.right = right_ast;
    binary_ast->binary.op = (char*)op;

    TypedASTNode* binary = create_typed_ast_node(binary_ast);
    ASSERT_TRUE(binary != NULL, "create typed binary node");

    binary->typed.binary.left = create_typed_ast_node(left_ast);
    binary->typed.binary.right = create_typed_ast_node(right_ast);

    ASSERT_TRUE(binary->typed.binary.left != NULL, "typed binary left child");
    ASSERT_TRUE(binary->typed.binary.right != NULL, "typed binary right child");

    return binary;
}

static TypedASTNode* make_block_with_child(TypedASTNode* child) {
    ASTNode* block_ast = new_ast_node(NODE_BLOCK);
    block_ast->block.count = 1;
    block_ast->block.statements = (ASTNode**)calloc(1, sizeof(ASTNode*));
    ASSERT_TRUE(block_ast->block.statements != NULL, "allocate block statements");
    block_ast->block.statements[0] = child->original;

    TypedASTNode* block = create_typed_ast_node(block_ast);
    ASSERT_TRUE(block != NULL, "create typed block");

    block->typed.block.count = 1;
    block->typed.block.statements =
        (TypedASTNode**)malloc(sizeof(TypedASTNode*));
    ASSERT_TRUE(block->typed.block.statements != NULL,
                "allocate typed block statements");
    block->typed.block.statements[0] = child;

    return block;
}

static void track_binary(FoldFixture* fixture, TypedASTNode* binary) {
    ASSERT_TRUE(fixture->binary_count < (int)(sizeof(fixture->binaries) /
                                              sizeof(fixture->binaries[0])),
                "binary tracking overflow");
    fixture->binaries[fixture->binary_count++] = binary;
}

static FoldFixture build_fixture(NodeType type) {
    FoldFixture fixture = {0};
    tested_node_types[type] = true;

    switch (type) {
        case NODE_PROGRAM: {
            ASTNode* program_ast = new_ast_node(NODE_PROGRAM);
            TypedASTNode* program = create_typed_ast_node(program_ast);
            ASSERT_TRUE(program != NULL, "create typed program");
            fixture.root = program;

            TypedASTNode* decl = make_binary_expr(1, "+", 1);
            track_binary(&fixture, decl);

            program->typed.program.count = 1;
            program->typed.program.declarations =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*));
            ASSERT_TRUE(program->typed.program.declarations != NULL,
                        "allocate typed program declarations");
            program->typed.program.declarations[0] = decl;

            program_ast->program.count = 1;
            program_ast->program.declarations =
                (ASTNode**)calloc(1, sizeof(ASTNode*));
            ASSERT_TRUE(program_ast->program.declarations != NULL,
                        "allocate program declarations");
            program_ast->program.declarations[0] = decl->original;
            break;
        }
        case NODE_VAR_DECL: {
            ASTNode* var_ast = new_ast_node(NODE_VAR_DECL);
            TypedASTNode* var = create_typed_ast_node(var_ast);
            ASSERT_TRUE(var != NULL, "create typed var decl");
            fixture.root = var;

            TypedASTNode* init = make_binary_expr(2, "+", 3);
            track_binary(&fixture, init);
            var->typed.varDecl.initializer = init;
            var_ast->varDecl.initializer = init->original;

            TypedASTNode* annotation = make_binary_expr(4, "+", 5);
            track_binary(&fixture, annotation);
            var->typed.varDecl.typeAnnotation = annotation;
            var_ast->varDecl.typeAnnotation = annotation->original;
            break;
        }
        case NODE_IDENTIFIER: {
            ASTNode* id_ast = new_ast_node(NODE_IDENTIFIER);
            TypedASTNode* id = create_typed_ast_node(id_ast);
            ASSERT_TRUE(id != NULL, "create typed identifier");
            fixture.root = id;
            break;
        }
        case NODE_LITERAL: {
            ASTNode* lit_ast = make_literal_ast(42);
            TypedASTNode* lit = create_typed_ast_node(lit_ast);
            ASSERT_TRUE(lit != NULL, "create typed literal");
            fixture.root = lit;
            break;
        }
        case NODE_ARRAY_LITERAL: {
            ASTNode* array_ast = new_ast_node(NODE_ARRAY_LITERAL);
            TypedASTNode* array = create_typed_ast_node(array_ast);
            ASSERT_TRUE(array != NULL, "create typed array literal");
            fixture.root = array;

            array->typed.arrayLiteral.count = 2;
            array->typed.arrayLiteral.elements =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*) * 2);
            ASSERT_TRUE(array->typed.arrayLiteral.elements != NULL,
                        "allocate typed array elements");

            ASTNode** original_elements =
                (ASTNode**)calloc(2, sizeof(ASTNode*));
            ASSERT_TRUE(original_elements != NULL, "allocate array elements");
            array_ast->arrayLiteral.count = 2;
            array_ast->arrayLiteral.elements = original_elements;

            for (int i = 0; i < 2; i++) {
                TypedASTNode* element = make_binary_expr(i + 1, "+", i + 2);
                track_binary(&fixture, element);
                array->typed.arrayLiteral.elements[i] = element;
                original_elements[i] = element->original;
            }
            break;
        }
        case NODE_ARRAY_FILL: {
            ASTNode* fill_ast = new_ast_node(NODE_ARRAY_FILL);
            TypedASTNode* fill = create_typed_ast_node(fill_ast);
            ASSERT_TRUE(fill != NULL, "create typed array fill");
            fixture.root = fill;

            TypedASTNode* value = make_binary_expr(3, "+", 4);
            track_binary(&fixture, value);
            fill->typed.arrayFill.value = value;
            fill_ast->arrayFill.value = value->original;

            TypedASTNode* length = make_binary_expr(5, "+", 6);
            track_binary(&fixture, length);
            fill->typed.arrayFill.lengthExpr = length;
            fill_ast->arrayFill.lengthExpr = length->original;
            break;
        }
        case NODE_INDEX_ACCESS: {
            ASTNode* access_ast = new_ast_node(NODE_INDEX_ACCESS);
            TypedASTNode* access = create_typed_ast_node(access_ast);
            ASSERT_TRUE(access != NULL, "create typed index access");
            fixture.root = access;

            TypedASTNode* array = make_binary_expr(1, "+", 2);
            track_binary(&fixture, array);
            access->typed.indexAccess.array = array;
            access_ast->indexAccess.array = array->original;

            TypedASTNode* index = make_binary_expr(3, "+", 4);
            track_binary(&fixture, index);
            access->typed.indexAccess.index = index;
            access_ast->indexAccess.index = index->original;
            break;
        }
        case NODE_BINARY: {
            TypedASTNode* binary = make_binary_expr(7, "+", 8);
            fixture.root = binary;
            track_binary(&fixture, binary);
            break;
        }
        case NODE_ASSIGN: {
            ASTNode* assign_ast = new_ast_node(NODE_ASSIGN);
            TypedASTNode* assign = create_typed_ast_node(assign_ast);
            ASSERT_TRUE(assign != NULL, "create typed assign");
            fixture.root = assign;

            TypedASTNode* value = make_binary_expr(5, "+", 5);
            track_binary(&fixture, value);
            assign->typed.assign.value = value;
            assign_ast->assign.value = value->original;
            break;
        }
        case NODE_ARRAY_ASSIGN: {
            ASTNode* array_assign_ast = new_ast_node(NODE_ARRAY_ASSIGN);
            TypedASTNode* array_assign = create_typed_ast_node(array_assign_ast);
            ASSERT_TRUE(array_assign != NULL, "create typed array assign");
            fixture.root = array_assign;

            TypedASTNode* target = make_binary_expr(1, "+", 0);
            track_binary(&fixture, target);
            array_assign->typed.arrayAssign.target = target;
            array_assign_ast->arrayAssign.target = target->original;

            TypedASTNode* value = make_binary_expr(10, "+", 1);
            track_binary(&fixture, value);
            array_assign->typed.arrayAssign.value = value;
            array_assign_ast->arrayAssign.value = value->original;
            break;
        }
        case NODE_ARRAY_SLICE: {
            ASTNode* slice_ast = new_ast_node(NODE_ARRAY_SLICE);
            TypedASTNode* slice = create_typed_ast_node(slice_ast);
            ASSERT_TRUE(slice != NULL, "create typed array slice");
            fixture.root = slice;

            TypedASTNode* array = make_binary_expr(1, "+", 1);
            track_binary(&fixture, array);
            slice->typed.arraySlice.array = array;
            slice_ast->arraySlice.array = array->original;

            TypedASTNode* start = make_binary_expr(0, "+", 0);
            track_binary(&fixture, start);
            slice->typed.arraySlice.start = start;
            slice_ast->arraySlice.start = start->original;

            TypedASTNode* end = make_binary_expr(9, "+", 9);
            track_binary(&fixture, end);
            slice->typed.arraySlice.end = end;
            slice_ast->arraySlice.end = end->original;
            break;
        }
        case NODE_PRINT: {
            ASTNode* print_ast = new_ast_node(NODE_PRINT);
            TypedASTNode* print = create_typed_ast_node(print_ast);
            ASSERT_TRUE(print != NULL, "create typed print");
            fixture.root = print;

            print->typed.print.count = 2;
            print->typed.print.values =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*) * 2);
            ASSERT_TRUE(print->typed.print.values != NULL,
                        "allocate print values");

            ASTNode** original_values =
                (ASTNode**)calloc(2, sizeof(ASTNode*));
            ASSERT_TRUE(original_values != NULL, "allocate original print values");
            print_ast->print.count = 2;
            print_ast->print.values = original_values;

            for (int i = 0; i < 2; i++) {
                TypedASTNode* value = make_binary_expr(i + 2, "+", i + 3);
                track_binary(&fixture, value);
                print->typed.print.values[i] = value;
                original_values[i] = value->original;
            }
            break;
        }
        case NODE_TIME_STAMP:
        case NODE_TYPE:
        case NODE_BREAK:
        case NODE_CONTINUE:
        case NODE_PASS:
        case NODE_IMPORT: {
            ASTNode* leaf_ast = new_ast_node(type);
            TypedASTNode* leaf = create_typed_ast_node(leaf_ast);
            ASSERT_TRUE(leaf != NULL, "create leaf typed node");
            fixture.root = leaf;
            break;
        }
        case NODE_IF: {
            ASTNode* if_ast = new_ast_node(NODE_IF);
            TypedASTNode* if_node = create_typed_ast_node(if_ast);
            ASSERT_TRUE(if_node != NULL, "create typed if");
            fixture.root = if_node;

            TypedASTNode* condition = make_binary_expr(1, "+", 0);
            track_binary(&fixture, condition);
            if_node->typed.ifStmt.condition = condition;
            if_ast->ifStmt.condition = condition->original;

            TypedASTNode* then_expr = make_binary_expr(2, "+", 2);
            track_binary(&fixture, then_expr);
            TypedASTNode* then_block = make_block_with_child(then_expr);
            if_node->typed.ifStmt.thenBranch = then_block;
            if_ast->ifStmt.thenBranch = then_block->original;

            TypedASTNode* else_expr = make_binary_expr(3, "+", 3);
            track_binary(&fixture, else_expr);
            TypedASTNode* else_block = make_block_with_child(else_expr);
            if_node->typed.ifStmt.elseBranch = else_block;
            if_ast->ifStmt.elseBranch = else_block->original;
            break;
        }
        case NODE_WHILE: {
            ASTNode* while_ast = new_ast_node(NODE_WHILE);
            TypedASTNode* while_node = create_typed_ast_node(while_ast);
            ASSERT_TRUE(while_node != NULL, "create typed while");
            fixture.root = while_node;

            TypedASTNode* condition = make_binary_expr(4, "+", 4);
            track_binary(&fixture, condition);
            while_node->typed.whileStmt.condition = condition;
            while_ast->whileStmt.condition = condition->original;

            TypedASTNode* body_expr = make_binary_expr(5, "+", 5);
            track_binary(&fixture, body_expr);
            TypedASTNode* body_block = make_block_with_child(body_expr);
            while_node->typed.whileStmt.body = body_block;
            while_ast->whileStmt.body = body_block->original;
            break;
        }
        case NODE_FOR_RANGE: {
            ASTNode* for_ast = new_ast_node(NODE_FOR_RANGE);
            TypedASTNode* for_node = create_typed_ast_node(for_ast);
            ASSERT_TRUE(for_node != NULL, "create typed for range");
            fixture.root = for_node;

            TypedASTNode* start = make_binary_expr(0, "+", 1);
            track_binary(&fixture, start);
            for_node->typed.forRange.start = start;
            for_ast->forRange.start = start->original;

            TypedASTNode* end = make_binary_expr(10, "+", 11);
            track_binary(&fixture, end);
            for_node->typed.forRange.end = end;
            for_ast->forRange.end = end->original;

            TypedASTNode* step = make_binary_expr(1, "+", 0);
            track_binary(&fixture, step);
            for_node->typed.forRange.step = step;
            for_ast->forRange.step = step->original;

            TypedASTNode* body_expr = make_binary_expr(6, "+", 7);
            track_binary(&fixture, body_expr);
            TypedASTNode* body_block = make_block_with_child(body_expr);
            for_node->typed.forRange.body = body_block;
            for_ast->forRange.body = body_block->original;
            break;
        }
        case NODE_FOR_ITER: {
            ASTNode* for_iter_ast = new_ast_node(NODE_FOR_ITER);
            TypedASTNode* for_iter = create_typed_ast_node(for_iter_ast);
            ASSERT_TRUE(for_iter != NULL, "create typed for iter");
            fixture.root = for_iter;

            TypedASTNode* iterable = make_binary_expr(1, "+", 2);
            track_binary(&fixture, iterable);
            for_iter->typed.forIter.iterable = iterable;
            for_iter_ast->forIter.iterable = iterable->original;

            TypedASTNode* body_expr = make_binary_expr(8, "+", 9);
            track_binary(&fixture, body_expr);
            TypedASTNode* body_block = make_block_with_child(body_expr);
            for_iter->typed.forIter.body = body_block;
            for_iter_ast->forIter.body = body_block->original;
            break;
        }
        case NODE_TRY: {
            ASTNode* try_ast = new_ast_node(NODE_TRY);
            TypedASTNode* try_node = create_typed_ast_node(try_ast);
            ASSERT_TRUE(try_node != NULL, "create typed try");
            fixture.root = try_node;

            TypedASTNode* try_expr = make_binary_expr(11, "+", 12);
            track_binary(&fixture, try_expr);
            TypedASTNode* try_block = make_block_with_child(try_expr);
            try_node->typed.tryStmt.tryBlock = try_block;
            try_ast->tryStmt.tryBlock = try_block->original;

            TypedASTNode* catch_expr = make_binary_expr(13, "+", 14);
            track_binary(&fixture, catch_expr);
            TypedASTNode* catch_block = make_block_with_child(catch_expr);
            try_node->typed.tryStmt.catchBlock = catch_block;
            try_ast->tryStmt.catchBlock = catch_block->original;
            break;
        }
        case NODE_BLOCK: {
            TypedASTNode* stmt = make_binary_expr(2, "+", 2);
            track_binary(&fixture, stmt);
            fixture.root = make_block_with_child(stmt);
            break;
        }
        case NODE_TERNARY: {
            ASTNode* ternary_ast = new_ast_node(NODE_TERNARY);
            TypedASTNode* ternary = create_typed_ast_node(ternary_ast);
            ASSERT_TRUE(ternary != NULL, "create typed ternary");
            fixture.root = ternary;

            TypedASTNode* condition = make_binary_expr(1, "+", 2);
            track_binary(&fixture, condition);
            ternary->typed.ternary.condition = condition;
            ternary_ast->ternary.condition = condition->original;

            TypedASTNode* true_expr = make_binary_expr(3, "+", 4);
            track_binary(&fixture, true_expr);
            ternary->typed.ternary.trueExpr = true_expr;
            ternary_ast->ternary.trueExpr = true_expr->original;

            TypedASTNode* false_expr = make_binary_expr(5, "+", 6);
            track_binary(&fixture, false_expr);
            ternary->typed.ternary.falseExpr = false_expr;
            ternary_ast->ternary.falseExpr = false_expr->original;
            break;
        }
        case NODE_UNARY: {
            ASTNode* unary_ast = new_ast_node(NODE_UNARY);
            unary_ast->unary.op = (char*)"-";
            TypedASTNode* unary = create_typed_ast_node(unary_ast);
            ASSERT_TRUE(unary != NULL, "create typed unary");
            fixture.root = unary;

            TypedASTNode* operand = make_binary_expr(9, "+", 9);
            track_binary(&fixture, operand);
            unary->typed.unary.operand = operand;
            unary_ast->unary.operand = operand->original;
            break;
        }
        case NODE_FUNCTION: {
            ASTNode* func_ast = new_ast_node(NODE_FUNCTION);
            TypedASTNode* func = create_typed_ast_node(func_ast);
            ASSERT_TRUE(func != NULL, "create typed function");
            fixture.root = func;

            TypedASTNode* return_type = make_binary_expr(0, "+", 0);
            track_binary(&fixture, return_type);
            func->typed.function.returnType = return_type;
            func_ast->function.returnType = return_type->original;

            TypedASTNode* body_expr = make_binary_expr(1, "+", 1);
            track_binary(&fixture, body_expr);
            TypedASTNode* body_block = make_block_with_child(body_expr);
            func->typed.function.body = body_block;
            func_ast->function.body = body_block->original;
            break;
        }
        case NODE_CALL: {
            ASTNode* call_ast = new_ast_node(NODE_CALL);
            TypedASTNode* call = create_typed_ast_node(call_ast);
            ASSERT_TRUE(call != NULL, "create typed call");
            fixture.root = call;

            TypedASTNode* callee = make_binary_expr(2, "+", 0);
            track_binary(&fixture, callee);
            call->typed.call.callee = callee;
            call_ast->call.callee = callee->original;

            call->typed.call.argCount = 2;
            call->typed.call.args =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*) * 2);
            ASSERT_TRUE(call->typed.call.args != NULL, "allocate call args");

            ASTNode** original_args =
                (ASTNode**)calloc(2, sizeof(ASTNode*));
            ASSERT_TRUE(original_args != NULL, "allocate original call args");
            call_ast->call.argCount = 2;
            call_ast->call.args = original_args;

            for (int i = 0; i < 2; i++) {
                TypedASTNode* arg = make_binary_expr(i + 3, "+", i + 4);
                track_binary(&fixture, arg);
                call->typed.call.args[i] = arg;
                original_args[i] = arg->original;
            }
            break;
        }
        case NODE_RETURN: {
            ASTNode* return_ast = new_ast_node(NODE_RETURN);
            TypedASTNode* ret = create_typed_ast_node(return_ast);
            ASSERT_TRUE(ret != NULL, "create typed return");
            fixture.root = ret;

            TypedASTNode* value = make_binary_expr(7, "+", 1);
            track_binary(&fixture, value);
            ret->typed.returnStmt.value = value;
            return_ast->returnStmt.value = value->original;
            break;
        }
        case NODE_CAST: {
            ASTNode* cast_ast = new_ast_node(NODE_CAST);
            TypedASTNode* cast = create_typed_ast_node(cast_ast);
            ASSERT_TRUE(cast != NULL, "create typed cast");
            fixture.root = cast;

            TypedASTNode* expr = make_binary_expr(1, "+", 2);
            track_binary(&fixture, expr);
            cast->typed.cast.expression = expr;
            cast_ast->cast.expression = expr->original;

            TypedASTNode* target = make_binary_expr(3, "+", 4);
            track_binary(&fixture, target);
            cast->typed.cast.targetType = target;
            cast_ast->cast.targetType = target->original;
            break;
        }
        case NODE_STRUCT_DECL: {
            ASTNode* struct_ast = new_ast_node(NODE_STRUCT_DECL);
            TypedASTNode* struct_node = create_typed_ast_node(struct_ast);
            ASSERT_TRUE(struct_node != NULL, "create typed struct decl");
            fixture.root = struct_node;

            struct_node->typed.structDecl.fieldCount = 1;
            struct_node->typed.structDecl.fields =
                (TypedStructField*)malloc(sizeof(TypedStructField));
            ASSERT_TRUE(struct_node->typed.structDecl.fields != NULL,
                        "allocate typed struct fields");

            TypedStructField* field = &struct_node->typed.structDecl.fields[0];
            TypedASTNode* field_type = make_binary_expr(1, "+", 1);
            track_binary(&fixture, field_type);
            field->typeAnnotation = field_type;

            TypedASTNode* field_default = make_binary_expr(2, "+", 2);
            track_binary(&fixture, field_default);
            field->defaultValue = field_default;

            StructField* original_fields =
                (StructField*)calloc(1, sizeof(StructField));
            ASSERT_TRUE(original_fields != NULL, "allocate original struct fields");
            struct_ast->structDecl.fieldCount = 1;
            struct_ast->structDecl.fields = original_fields;
            original_fields[0].typeAnnotation = field_type->original;
            original_fields[0].defaultValue = field_default->original;
            break;
        }
        case NODE_IMPL_BLOCK: {
            ASTNode* impl_ast = new_ast_node(NODE_IMPL_BLOCK);
            TypedASTNode* impl = create_typed_ast_node(impl_ast);
            ASSERT_TRUE(impl != NULL, "create typed impl block");
            fixture.root = impl;

            impl->typed.implBlock.methodCount = 1;
            impl->typed.implBlock.methods =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*));
            ASSERT_TRUE(impl->typed.implBlock.methods != NULL,
                        "allocate impl methods");

            TypedASTNode* method = make_binary_expr(5, "+", 5);
            track_binary(&fixture, method);
            impl->typed.implBlock.methods[0] = method;
            break;
        }
        case NODE_STRUCT_LITERAL: {
            ASTNode* literal_ast = new_ast_node(NODE_STRUCT_LITERAL);
            TypedASTNode* literal = create_typed_ast_node(literal_ast);
            ASSERT_TRUE(literal != NULL, "create typed struct literal");
            fixture.root = literal;

            literal->typed.structLiteral.fieldCount = 2;
            literal->typed.structLiteral.values =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*) * 2);
            ASSERT_TRUE(literal->typed.structLiteral.values != NULL,
                        "allocate struct literal values");

            for (int i = 0; i < 2; i++) {
                TypedASTNode* value = make_binary_expr(i + 1, "+", i + 2);
                track_binary(&fixture, value);
                literal->typed.structLiteral.values[i] = value;
            }
            break;
        }
        case NODE_MEMBER_ACCESS: {
            ASTNode* member_ast = new_ast_node(NODE_MEMBER_ACCESS);
            TypedASTNode* member = create_typed_ast_node(member_ast);
            ASSERT_TRUE(member != NULL, "create typed member access");
            fixture.root = member;

            TypedASTNode* object = make_binary_expr(1, "+", 2);
            track_binary(&fixture, object);
            member->typed.member.object = object;
            member_ast->member.object = object->original;
            break;
        }
        case NODE_MEMBER_ASSIGN: {
            ASTNode* member_assign_ast = new_ast_node(NODE_MEMBER_ASSIGN);
            TypedASTNode* member_assign = create_typed_ast_node(member_assign_ast);
            ASSERT_TRUE(member_assign != NULL, "create typed member assign");
            fixture.root = member_assign;

            TypedASTNode* target = make_binary_expr(2, "+", 3);
            track_binary(&fixture, target);
            member_assign->typed.memberAssign.target = target;
            member_assign_ast->memberAssign.target = target->original;

            TypedASTNode* value = make_binary_expr(4, "+", 5);
            track_binary(&fixture, value);
            member_assign->typed.memberAssign.value = value;
            member_assign_ast->memberAssign.value = value->original;
            break;
        }
        case NODE_ENUM_DECL: {
            ASTNode* enum_ast = new_ast_node(NODE_ENUM_DECL);
            TypedASTNode* enum_node = create_typed_ast_node(enum_ast);
            ASSERT_TRUE(enum_node != NULL, "create typed enum decl");
            fixture.root = enum_node;

            enum_node->typed.enumDecl.variantCount = 1;
            enum_node->typed.enumDecl.variants =
                (TypedEnumVariant*)malloc(sizeof(TypedEnumVariant));
            ASSERT_TRUE(enum_node->typed.enumDecl.variants != NULL,
                        "allocate typed enum variants");

            TypedEnumVariant* variant = &enum_node->typed.enumDecl.variants[0];
            variant->fieldCount = 1;
            variant->fields =
                (TypedEnumVariantField*)malloc(sizeof(TypedEnumVariantField));
            ASSERT_TRUE(variant->fields != NULL,
                        "allocate typed enum variant fields");

            TypedASTNode* field_type = make_binary_expr(6, "+", 7);
            track_binary(&fixture, field_type);
            variant->fields[0].typeAnnotation = field_type;

            EnumVariant* original_variants =
                (EnumVariant*)calloc(1, sizeof(EnumVariant));
            ASSERT_TRUE(original_variants != NULL, "allocate original enum variants");
            enum_ast->enumDecl.variantCount = 1;
            enum_ast->enumDecl.variants = original_variants;
            EnumVariantField* original_fields =
                (EnumVariantField*)calloc(1, sizeof(EnumVariantField));
            ASSERT_TRUE(original_fields != NULL, "allocate original enum fields");
            original_variants[0].fieldCount = 1;
            original_variants[0].fields = original_fields;
            original_fields[0].typeAnnotation = field_type->original;
            break;
        }
        case NODE_ENUM_MATCH_TEST: {
            ASTNode* match_test_ast = new_ast_node(NODE_ENUM_MATCH_TEST);
            TypedASTNode* match_test = create_typed_ast_node(match_test_ast);
            ASSERT_TRUE(match_test != NULL, "create typed enum match test");
            fixture.root = match_test;

            TypedASTNode* value = make_binary_expr(1, "+", 1);
            track_binary(&fixture, value);
            match_test->typed.enumMatchTest.value = value;
            match_test_ast->enumMatchTest.value = value->original;
            break;
        }
        case NODE_ENUM_PAYLOAD: {
            ASTNode* payload_ast = new_ast_node(NODE_ENUM_PAYLOAD);
            TypedASTNode* payload = create_typed_ast_node(payload_ast);
            ASSERT_TRUE(payload != NULL, "create typed enum payload");
            fixture.root = payload;

            TypedASTNode* value = make_binary_expr(2, "+", 2);
            track_binary(&fixture, value);
            payload->typed.enumPayload.value = value;
            payload_ast->enumPayload.value = value->original;
            break;
        }
        case NODE_ENUM_MATCH_CHECK: {
            ASTNode* check_ast = new_ast_node(NODE_ENUM_MATCH_CHECK);
            TypedASTNode* check = create_typed_ast_node(check_ast);
            ASSERT_TRUE(check != NULL, "create typed enum match check");
            fixture.root = check;

            TypedASTNode* value = make_binary_expr(3, "+", 3);
            track_binary(&fixture, value);
            check->typed.enumMatchCheck.value = value;
            check_ast->enumMatchCheck.value = value->original;
            break;
        }
        case NODE_MATCH_EXPRESSION: {
            ASTNode* match_ast = new_ast_node(NODE_MATCH_EXPRESSION);
            TypedASTNode* match = create_typed_ast_node(match_ast);
            ASSERT_TRUE(match != NULL, "create typed match expression");
            fixture.root = match;

            TypedASTNode* subject = make_binary_expr(4, "+", 4);
            track_binary(&fixture, subject);
            match->typed.matchExpr.subject = subject;
            match_ast->matchExpr.subject = subject->original;

            match->typed.matchExpr.armCount = 1;
            match->typed.matchExpr.arms =
                (TypedMatchArm*)calloc(1, sizeof(TypedMatchArm));
            ASSERT_TRUE(match->typed.matchExpr.arms != NULL,
                        "allocate typed match arms");

            TypedMatchArm* arm = &match->typed.matchExpr.arms[0];
            arm->payloadCount = 1;
            arm->payloadAccesses =
                (TypedASTNode**)malloc(sizeof(TypedASTNode*));
            ASSERT_TRUE(arm->payloadAccesses != NULL,
                        "allocate payload accesses");

            TypedASTNode* pattern = make_binary_expr(5, "+", 5);
            track_binary(&fixture, pattern);
            arm->valuePattern = pattern;

            TypedASTNode* body_expr = make_binary_expr(6, "+", 6);
            track_binary(&fixture, body_expr);
            TypedASTNode* body_block = make_block_with_child(body_expr);
            arm->body = body_block;

            TypedASTNode* condition = make_binary_expr(7, "+", 7);
            track_binary(&fixture, condition);
            arm->condition = condition;

            TypedASTNode* payload_access = make_binary_expr(8, "+", 8);
            track_binary(&fixture, payload_access);
            arm->payloadAccesses[0] = payload_access;

            MatchArm* original_arms =
                (MatchArm*)calloc(1, sizeof(MatchArm));
            ASSERT_TRUE(original_arms != NULL, "allocate original match arms");
            match_ast->matchExpr.armCount = 1;
            match_ast->matchExpr.arms = original_arms;
            original_arms[0].valuePattern = pattern->original;
            original_arms[0].body = body_block->original;
            original_arms[0].condition = condition->original;
            original_arms[0].payloadAccesses =
                (ASTNode**)calloc(1, sizeof(ASTNode*));
            ASSERT_TRUE(original_arms[0].payloadAccesses != NULL,
                        "allocate original payload accesses");
            original_arms[0].payloadAccesses[0] = payload_access->original;
            original_arms[0].payloadCount = 1;
            break;
        }
    }

    return fixture;
}

static bool run_fixture_for_type(NodeType type) {
    FoldFixture fixture = build_fixture(type);
    ASSERT_TRUE(fixture.root != NULL, "fixture root");

    ConstantFoldContext ctx;
    init_constant_fold_context(&ctx);
    ASSERT_TRUE(apply_constant_folding_recursive(fixture.root, &ctx),
                "apply constant folding");

    for (int i = 0; i < fixture.binary_count; i++) {
        TypedASTNode* binary = fixture.binaries[i];
        ASSERT_TRUE(binary->original->type == NODE_LITERAL,
                    "binary expression folded to literal");
    }

    free_typed_ast_node(fixture.root);
    return true;
}

static bool verify_all_node_types_tested(void) {
    for (int i = 0; i <= NODE_MATCH_EXPRESSION; i++) {
        if (!tested_node_types[i]) {
            fprintf(stderr,
                    "Missing constant folding coverage for node type %d\n", i);
            return false;
        }
    }
    return true;
}

int main(void) {
    const NodeType node_types[] = {
        NODE_PROGRAM,
        NODE_VAR_DECL,
        NODE_IDENTIFIER,
        NODE_LITERAL,
        NODE_ARRAY_LITERAL,
        NODE_ARRAY_FILL,
        NODE_INDEX_ACCESS,
        NODE_BINARY,
        NODE_ASSIGN,
        NODE_ARRAY_ASSIGN,
        NODE_ARRAY_SLICE,
        NODE_PRINT,
        NODE_TIME_STAMP,
        NODE_IF,
        NODE_WHILE,
        NODE_FOR_RANGE,
        NODE_FOR_ITER,
        NODE_TRY,
        NODE_BLOCK,
        NODE_TERNARY,
        NODE_UNARY,
        NODE_TYPE,
        NODE_BREAK,
        NODE_CONTINUE,
        NODE_PASS,
        NODE_FUNCTION,
        NODE_CALL,
        NODE_RETURN,
        NODE_CAST,
        NODE_STRUCT_DECL,
        NODE_IMPL_BLOCK,
        NODE_STRUCT_LITERAL,
        NODE_MEMBER_ACCESS,
        NODE_MEMBER_ASSIGN,
        NODE_ENUM_DECL,
        NODE_IMPORT,
        NODE_ENUM_MATCH_TEST,
        NODE_ENUM_PAYLOAD,
        NODE_ENUM_MATCH_CHECK,
        NODE_MATCH_EXPRESSION,
    };

    int total = (int)(sizeof(node_types) / sizeof(node_types[0]));
    int passed = 0;

    for (int i = 0; i < total; i++) {
        NodeType type = node_types[i];
        if (run_fixture_for_type(type)) {
            printf("[PASS] constant folding coverage for node type %d\n", type);
            passed++;
        } else {
            printf("[FAIL] constant folding coverage for node type %d\n", type);
            return 1;
        }
    }

    if (!verify_all_node_types_tested()) {
        printf("[FAIL] missing node type coverage\n");
        return 1;
    }

    printf("%d/%d constant folding fixtures passed\n", passed, total);
    return 0;
}
