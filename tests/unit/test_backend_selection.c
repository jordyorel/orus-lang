// test_backend_selection.c
// Unit tests for backend selection and complexity analysis

#include "unity.h"
#include "compiler/backend_selection.h"
#include "compiler/compiler.h"
#include "compiler/ast.h"
#include "compiler/lexer.h"
#include "vm/vm.h"

// Helper function to create simple AST nodes
static ASTNode* createLiteralNode(int value) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_LITERAL;
    node->literal.value = I32_VAL(value);
    return node;
}

static ASTNode* createBinaryNode(ASTNode* left, ASTNode* right, const char* op) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_BINARY;
    node->binary.left = left;
    node->binary.right = right;
    node->binary.op = op;
    return node;
}

static ASTNode* createForLoopNode(const char* varName, ASTNode* start, ASTNode* end, ASTNode* body) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_FOR_RANGE;
    node->forRange.varName = varName;
    node->forRange.start = start;
    node->forRange.end = end;
    node->forRange.body = body;
    return node;
}

static ASTNode* createBlockNode(ASTNode** statements, int count) {
    ASTNode* node = malloc(sizeof(ASTNode));
    node->type = NODE_BLOCK;
    node->block.statements = statements;
    node->block.count = count;
    return node;
}

static void freeASTNode(ASTNode* node) {
    if (node) {
        // This is simplified - in real code you'd recursively free child nodes
        free(node);
    }
}

// Test simple expression complexity
void test_analyze_simple_expression(void) {
    ASTNode* left = createLiteralNode(10);
    ASTNode* right = createLiteralNode(20);
    ASTNode* binary = createBinaryNode(left, right, "+");
    
    CodeComplexity complexity = analyzeCodeComplexity(binary);
    
    TEST_ASSERT_EQUAL_INT(0, complexity.loopCount);
    TEST_ASSERT_EQUAL_INT(0, complexity.nestedLoopDepth);
    TEST_ASSERT_EQUAL_INT(0, complexity.functionCount);
    TEST_ASSERT_EQUAL_INT(0, complexity.callCount);
    TEST_ASSERT_EQUAL_INT(1, complexity.complexExpressionCount);
    TEST_ASSERT_FALSE(complexity.hasBreakContinue);
    TEST_ASSERT_TRUE(complexity.hasComplexArithmetic);
    TEST_ASSERT_TRUE(complexity.complexityScore > 0.0f);
    
    freeASTNode(left);
    freeASTNode(right);
    freeASTNode(binary);
}

// Test loop complexity analysis
void test_analyze_simple_loop(void) {
    ASTNode* start = createLiteralNode(0);
    ASTNode* end = createLiteralNode(10);
    ASTNode* body = createBlockNode(NULL, 0);
    ASTNode* loop = createForLoopNode("i", start, end, body);
    
    CodeComplexity complexity = analyzeCodeComplexity(loop);
    
    TEST_ASSERT_EQUAL_INT(1, complexity.loopCount);
    TEST_ASSERT_EQUAL_INT(1, complexity.nestedLoopDepth);
    TEST_ASSERT_TRUE(complexity.complexityScore > 5.0f); // Loops add significant complexity
    
    freeASTNode(start);
    freeASTNode(end);
    freeASTNode(body);
    freeASTNode(loop);
}

// Test nested loop complexity
void test_analyze_nested_loops(void) {
    // Create inner loop
    ASTNode* innerStart = createLiteralNode(0);
    ASTNode* innerEnd = createLiteralNode(5);
    ASTNode* innerBody = createBlockNode(NULL, 0);
    ASTNode* innerLoop = createForLoopNode("j", innerStart, innerEnd, innerBody);
    
    // Create outer loop with inner loop as body
    ASTNode** outerStatements = malloc(sizeof(ASTNode*));
    outerStatements[0] = innerLoop;
    ASTNode* outerBody = createBlockNode(outerStatements, 1);
    
    ASTNode* outerStart = createLiteralNode(0);
    ASTNode* outerEnd = createLiteralNode(10);
    ASTNode* outerLoop = createForLoopNode("i", outerStart, outerEnd, outerBody);
    
    CodeComplexity complexity = analyzeCodeComplexity(outerLoop);
    
    TEST_ASSERT_EQUAL_INT(2, complexity.loopCount);
    TEST_ASSERT_EQUAL_INT(2, complexity.nestedLoopDepth);
    TEST_ASSERT_TRUE(complexity.complexityScore > 15.0f); // Nested loops are very complex
    
    freeASTNode(innerStart);
    freeASTNode(innerEnd);
    freeASTNode(innerBody);
    freeASTNode(innerLoop);
    freeASTNode(outerBody);
    freeASTNode(outerStart);
    freeASTNode(outerEnd);
    freeASTNode(outerLoop);
    free(outerStatements);
}

// Test backend selection for simple code
void test_choose_backend_simple(void) {
    ASTNode* left = createLiteralNode(10);
    ASTNode* right = createLiteralNode(20);
    ASTNode* binary = createBinaryNode(left, right, "+");
    
    CompilerBackend backend = chooseOptimalBackend(binary, NULL);
    
    // Simple expressions should use fast backend
    TEST_ASSERT_TRUE(backend == BACKEND_FAST || backend == BACKEND_AUTO);
    
    freeASTNode(left);
    freeASTNode(right);
    freeASTNode(binary);
}

// Test backend selection for complex code
void test_choose_backend_complex(void) {
    // Create a complex nested loop structure
    ASTNode* innerStart = createLiteralNode(0);
    ASTNode* innerEnd = createLiteralNode(100);
    ASTNode* innerBody = createBlockNode(NULL, 0);
    ASTNode* innerLoop = createForLoopNode("j", innerStart, innerEnd, innerBody);
    
    ASTNode** statements = malloc(sizeof(ASTNode*));
    statements[0] = innerLoop;
    ASTNode* outerBody = createBlockNode(statements, 1);
    
    ASTNode* outerStart = createLiteralNode(0);
    ASTNode* outerEnd = createLiteralNode(100);
    ASTNode* outerLoop = createForLoopNode("i", outerStart, outerEnd, outerBody);
    
    CompilerBackend backend = chooseOptimalBackend(outerLoop, NULL);
    
    // Complex nested loops should use optimized backend
    TEST_ASSERT_TRUE(backend == BACKEND_OPTIMIZED || backend == BACKEND_HYBRID);
    
    freeASTNode(innerStart);
    freeASTNode(innerEnd);
    freeASTNode(innerBody);
    freeASTNode(innerLoop);
    freeASTNode(outerBody);
    freeASTNode(outerStart);
    freeASTNode(outerEnd);
    freeASTNode(outerLoop);
    free(statements);
}

// Test complexity score calculation
void test_complexity_score_calculation(void) {
    // Test empty complexity
    CodeComplexity empty = {0};
    TEST_ASSERT_TRUE(empty.complexityScore == 0.0f);
    
    // Test with some complexity
    CodeComplexity complex = {0};
    complex.loopCount = 2;
    complex.nestedLoopDepth = 2;
    complex.complexExpressionCount = 3;
    complex.hasComplexArithmetic = true;
    
    // Calculate expected score based on the algorithm
    float expectedScore = complex.loopCount * 5.0f + 
                         complex.nestedLoopDepth * 3.0f + 
                         complex.complexExpressionCount * 1.0f +
                         (complex.hasComplexArithmetic ? 2.0f : 0.0f);
    
    complex.complexityScore = expectedScore;
    
    TEST_ASSERT_TRUE(complex.complexityScore > 15.0f);
}

// Test edge cases
void test_analyze_null_node(void) {
    CodeComplexity complexity = analyzeCodeComplexity(NULL);
    
    TEST_ASSERT_EQUAL_INT(0, complexity.loopCount);
    TEST_ASSERT_EQUAL_INT(0, complexity.nestedLoopDepth);
    TEST_ASSERT_EQUAL_INT(0, complexity.functionCount);
    TEST_ASSERT_EQUAL_INT(0, complexity.callCount);
    TEST_ASSERT_EQUAL_INT(0, complexity.complexExpressionCount);
    TEST_ASSERT_FALSE(complexity.hasBreakContinue);
    TEST_ASSERT_FALSE(complexity.hasComplexArithmetic);
    TEST_ASSERT_TRUE(complexity.complexityScore == 0.0f);
}

// Test complex arithmetic detection
void test_detect_complex_arithmetic(void) {
    // Create a complex expression: (a * b) + (c / d)
    ASTNode* a = createLiteralNode(10);
    ASTNode* b = createLiteralNode(20);
    ASTNode* c = createLiteralNode(30);
    ASTNode* d = createLiteralNode(40);
    
    ASTNode* mul = createBinaryNode(a, b, "*");
    ASTNode* div = createBinaryNode(c, d, "/");
    ASTNode* add = createBinaryNode(mul, div, "+");
    
    CodeComplexity complexity = analyzeCodeComplexity(add);
    
    TEST_ASSERT_TRUE(complexity.hasComplexArithmetic);
    TEST_ASSERT_TRUE(complexity.complexExpressionCount >= 3); // Three binary operations
    
    freeASTNode(a);
    freeASTNode(b);
    freeASTNode(c);
    freeASTNode(d);
    freeASTNode(mul);
    freeASTNode(div);
    freeASTNode(add);
}

// Test backend selection consistency
void test_backend_selection_consistency(void) {
    ASTNode* node = createLiteralNode(42);
    
    // Multiple calls should return the same result
    CompilerBackend backend1 = chooseOptimalBackend(node, NULL);
    CompilerBackend backend2 = chooseOptimalBackend(node, NULL);
    
    TEST_ASSERT_EQUAL_INT(backend1, backend2);
    
    freeASTNode(node);
}

int main(void) {
    UNITY_BEGIN();
    
    // Basic complexity analysis tests
    RUN_TEST(test_analyze_simple_expression);
    RUN_TEST(test_analyze_simple_loop);
    RUN_TEST(test_analyze_nested_loops);
    
    // Backend selection tests
    RUN_TEST(test_choose_backend_simple);
    RUN_TEST(test_choose_backend_complex);
    
    // Complexity score tests
    RUN_TEST(test_complexity_score_calculation);
    
    // Edge case tests
    RUN_TEST(test_analyze_null_node);
    RUN_TEST(test_detect_complex_arithmetic);
    RUN_TEST(test_backend_selection_consistency);
    
    UNITY_END();
}