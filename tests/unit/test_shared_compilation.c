// test_shared_compilation.c
// Unit tests for shared node compilation utilities

#include "unity.h"
#include "compiler/shared_node_compilation.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "type/type.h"

// Helper function to create a simple compiler for testing
static Compiler* createTestCompiler(void) {
    Compiler* compiler = malloc(sizeof(Compiler));
    Chunk* chunk = malloc(sizeof(Chunk));
    initChunk(chunk);
    initCompiler(compiler, chunk, "test.orus", "");
    return compiler;
}

static void freeTestCompiler(Compiler* compiler) {
    if (compiler) {
        freeChunk(compiler->chunk);
        free(compiler->chunk);
        freeCompiler(compiler);
        free(compiler);
    }
}

// Test literal compilation
void test_compile_simple_literal(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create a simple integer literal node
    ASTNode node = {0};
    node.type = NODE_LITERAL;
    node.literal.value = I32_VAL(42);
    
    int reg = compileSharedLiteral(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    freeTestCompiler(compiler);
}

// Test string literal compilation
void test_compile_string_literal(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    ASTNode node = {0};
    node.type = NODE_LITERAL;
    // Note: In a real implementation, this would need proper string object creation
    // For the test, we'll use a simplified approach
    node.literal.value = STRING_VAL(NULL); // Simplified for testing
    
    int reg = compileSharedLiteral(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    freeTestCompiler(compiler);
}

// Test binary operation compilation
void test_compile_binary_addition(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create left operand (literal 10)
    ASTNode* left = malloc(sizeof(ASTNode));
    left->type = NODE_LITERAL;
    left->literal.value = I32_VAL(10);
    
    // Create right operand (literal 20)
    ASTNode* right = malloc(sizeof(ASTNode));
    right->type = NODE_LITERAL;
    right->literal.value = I32_VAL(20);
    
    // Create binary node
    ASTNode node = {0};
    node.type = NODE_BINARY;
    node.binary.left = left;
    node.binary.right = right;
    node.binary.op = "+";
    
    int reg = compileSharedBinaryOp(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    free(left);
    free(right);
    freeTestCompiler(compiler);
}

// Test variable declaration compilation
void test_compile_variable_declaration(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create initializer (literal 42)
    ASTNode* initializer = malloc(sizeof(ASTNode));
    initializer->type = NODE_LITERAL;
    initializer->literal.value = I32_VAL(42);
    
    // Create variable declaration node
    ASTNode node = {0};
    node.type = NODE_VAR_DECL;
    node.varDecl.name = "x";
    node.varDecl.initializer = initializer;
    
    int reg = compileSharedVarDecl(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->localCount == 1);
    TEST_ASSERT_TRUE(strcmp(compiler->locals[0].name, "x") == 0);
    
    free(initializer);
    freeTestCompiler(compiler);
}

// Test if statement compilation
void test_compile_if_statement(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create condition (literal true)
    ASTNode* condition = malloc(sizeof(ASTNode));
    condition->type = NODE_LITERAL;
    condition->literal.value = BOOL_VAL(true);
    
    // Create then branch (empty block)
    ASTNode* thenBranch = malloc(sizeof(ASTNode));
    thenBranch->type = NODE_BLOCK;
    thenBranch->block.count = 0;
    thenBranch->block.statements = NULL;
    
    // Create if statement node
    ASTNode node = {0};
    node.type = NODE_IF;
    node.ifStmt.condition = condition;
    node.ifStmt.thenBranch = thenBranch;
    node.ifStmt.elseBranch = NULL;
    
    int result = compileSharedIfStatement(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(result >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    free(condition);
    free(thenBranch);
    freeTestCompiler(compiler);
}

// Test cast compilation
void test_compile_cast_to_string(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create expression to cast (literal 42)
    ASTNode* expression = malloc(sizeof(ASTNode));
    expression->type = NODE_LITERAL;
    expression->literal.value = I32_VAL(42);
    
    // Create target type node
    ASTNode* targetType = malloc(sizeof(ASTNode));
    targetType->type = NODE_TYPE;
    targetType->typeAnnotation.name = "string";
    
    // Create cast node
    ASTNode node = {0};
    node.type = NODE_CAST;
    node.cast.expression = expression;
    node.cast.targetType = targetType;
    
    int reg = compileSharedCast(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    free(expression);
    free(targetType);
    freeTestCompiler(compiler);
}

// Test context creation functions
void test_create_single_pass_context(void) {
    CompilerContext ctx = createSinglePassContext();
    
    TEST_ASSERT_FALSE(ctx.supportsBreakContinue);
    TEST_ASSERT_FALSE(ctx.supportsFunctions);
    TEST_ASSERT_FALSE(ctx.enableOptimizations);
    TEST_ASSERT_NULL(ctx.vmOptCtx);
}

void test_create_multi_pass_context(void) {
    VMOptimizationContext vmCtx = createVMOptimizationContext(BACKEND_OPTIMIZED);
    CompilerContext ctx = createMultiPassContext(&vmCtx);
    
    TEST_ASSERT_TRUE(ctx.supportsBreakContinue);
    TEST_ASSERT_TRUE(ctx.supportsFunctions);
    TEST_ASSERT_TRUE(ctx.enableOptimizations);
    TEST_ASSERT_NOT_NULL(ctx.vmOptCtx);
}

// Test error handling
void test_compile_invalid_node_type(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create a node with invalid type
    ASTNode node = {0};
    node.type = (NodeType)999; // Invalid type
    
    bool result = compileSharedNode(&node, compiler, &ctx);
    
    TEST_ASSERT_FALSE(result);
    
    freeTestCompiler(compiler);
}

// Test nested expressions
void test_compile_nested_binary_expressions(void) {
    Compiler* compiler = createTestCompiler();
    CompilerContext ctx = createSinglePassContext();
    
    // Create: (10 + 20) * 30
    
    // Inner left: 10
    ASTNode* innerLeft = malloc(sizeof(ASTNode));
    innerLeft->type = NODE_LITERAL;
    innerLeft->literal.value = I32_VAL(10);
    
    // Inner right: 20
    ASTNode* innerRight = malloc(sizeof(ASTNode));
    innerRight->type = NODE_LITERAL;
    innerRight->literal.value = I32_VAL(20);
    
    // Inner binary: 10 + 20
    ASTNode* innerBinary = malloc(sizeof(ASTNode));
    innerBinary->type = NODE_BINARY;
    innerBinary->binary.left = innerLeft;
    innerBinary->binary.right = innerRight;
    innerBinary->binary.op = "+";
    
    // Outer right: 30
    ASTNode* outerRight = malloc(sizeof(ASTNode));
    outerRight->type = NODE_LITERAL;
    outerRight->literal.value = I32_VAL(30);
    
    // Outer binary: (10 + 20) * 30
    ASTNode node = {0};
    node.type = NODE_BINARY;
    node.binary.left = innerBinary;
    node.binary.right = outerRight;
    node.binary.op = "*";
    
    int reg = compileSharedBinaryOp(&node, compiler, &ctx);
    
    TEST_ASSERT_TRUE(reg >= 0);
    TEST_ASSERT_TRUE(compiler->chunk->count > 0);
    
    free(innerLeft);
    free(innerRight);
    free(innerBinary);
    free(outerRight);
    freeTestCompiler(compiler);
}

int main(void) {
    UNITY_BEGIN();
    
    // Basic compilation tests
    RUN_TEST(test_compile_simple_literal);
    RUN_TEST(test_compile_string_literal);
    RUN_TEST(test_compile_binary_addition);
    RUN_TEST(test_compile_variable_declaration);
    RUN_TEST(test_compile_if_statement);
    RUN_TEST(test_compile_cast_to_string);
    
    // Context tests
    RUN_TEST(test_create_single_pass_context);
    RUN_TEST(test_create_multi_pass_context);
    
    // Error handling tests
    RUN_TEST(test_compile_invalid_node_type);
    
    // Complex expression tests
    RUN_TEST(test_compile_nested_binary_expressions);
    
    UNITY_END();
}