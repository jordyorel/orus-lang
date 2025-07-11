#include "test_framework.h"
#include "../../../include/lexer.h"

extern Lexer lexer;

void test_line_comments_basic() {
    init_scanner("x = 1\n// A comment\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (comment line skipped)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token after comment is identifier");
}

void test_indented_comments() {
    init_scanner("if true:\n    x = 1\n    // Indented comment\n    y = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IF, token.type, "First token is if");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_TRUE, token.type, "Second token is true");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_COLON, token.type, "Third token is colon");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_INDENT, token.type, "Fifth token is indent");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Sixth token is identifier x");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Seventh token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Eighth token is number 1");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Ninth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Tenth token is newline (comment line skipped)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token after indented comment is identifier y");
}

void test_block_comments_indented() {
    init_scanner("if true:\n    x = 1\n    /* Block comment\n       spanning multiple lines */\n    y = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IF, token.type, "First token is if");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_TRUE, token.type, "Second token is true");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_COLON, token.type, "Third token is colon");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_INDENT, token.type, "Fifth token is indent");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Sixth token is identifier x");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Seventh token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Eighth token is number 1");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Ninth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Tenth token is newline (block comment skipped)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token after block comment is identifier y");
}

void test_comment_only_lines_dont_affect_indentation() {
    init_scanner("if true:\n    // Comment at indent level 4\n    x = 1\n        // Comment at indent level 8\n    y = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IF, token.type, "First token is if");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_TRUE, token.type, "Second token is true");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_COLON, token.type, "Third token is colon");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (comment line)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_INDENT, token.type, "Sixth token is indent (based on x = 1 line)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Seventh token is identifier x");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Eighth token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Ninth token is number 1");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Tenth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Eleventh token is newline (comment line)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y (no extra indent)");
}

int main() {
    printf("Running Indented Comments Tests\n");
    printf("========================================\n");
    
    RUN_TEST(test_line_comments_basic);
    RUN_TEST(test_indented_comments);
    RUN_TEST(test_block_comments_indented);
    RUN_TEST(test_comment_only_lines_dont_affect_indentation);
    
    PRINT_TEST_RESULTS();
    
    return tests_failed > 0 ? 1 : 0;
}