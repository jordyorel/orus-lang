#include "test_framework.h"
#include "../../../include/lexer.h"

extern Lexer lexer;

void test_comment_at_end_of_line() {
    init_scanner("x = 1 // comment after code\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_multiple_consecutive_comments() {
    init_scanner("x = 1\n// First comment\n// Second comment\n// Third comment\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    // Skip through comment lines
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (first comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Sixth token is newline (second comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Seventh token is newline (third comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_nested_block_comments() {
    init_scanner("x = 1\n/* Outer comment /* inner comment */ still outer */\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (nested block comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_comment_with_special_characters() {
    init_scanner("x = 1 // Comment with symbols !@#$%^&*(){}[]<>?/\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_comment_with_quotes() {
    init_scanner("x = 1 // Comment with \"quotes\" and 'apostrophes'\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_empty_comments() {
    init_scanner("x = 1 //\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_block_comment_spanning_multiple_lines() {
    init_scanner("x = 1\n/* This is a\n   multi-line\n   block comment */\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (block comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_comment_at_very_deep_indentation() {
    init_scanner("if true:\n    if true:\n        if true:\n            if true:\n                // Very deep comment\n                x = 1");
    
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
    
    // Skip through nested structure
    while (token.type != TOKEN_IDENTIFIER || memcmp(token.start, "x", 1) != 0) {
        token = scan_token();
        if (token.type == TOKEN_EOF) break;
    }
    
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Found identifier x after deep comment");
}

void test_comments_with_tabs_and_spaces() {
    init_scanner("x = 1\n\t// Tab-indented comment\n    // Space-indented comment\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fifth token is newline (tab comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Sixth token is newline (space comment)");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
}

void test_comment_false_starts() {
    init_scanner("x = 1 / 2 // This is a comment, not division\ny = 3 * 4 /* block comment */ z = 5");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_SLASH, token.type, "Fourth token is slash");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Fifth token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Sixth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier y");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Next token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Next token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_STAR, token.type, "Next token is star");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Next token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Next token is identifier z (after block comment)");
}

void test_unterminated_block_comment() {
    init_scanner("x = 1\n/* This block comment is not terminated\ny = 2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NEWLINE, token.type, "Fourth token is newline");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EOF, token.type, "EOF reached (unterminated block comment consumes rest)");
}

void test_comments_between_tokens() {
    init_scanner("x/*comment*/=/*comment*/1/*comment*/+/*comment*/2");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "First token is identifier");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL, token.type, "Second token is equals");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Third token is number");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_PLUS, token.type, "Fourth token is plus");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Fifth token is number");
}

int main() {
    printf("Running Comment Edge Cases Tests\n");
    printf("========================================\n");
    
    RUN_TEST(test_comment_at_end_of_line);
    RUN_TEST(test_multiple_consecutive_comments);
    RUN_TEST(test_nested_block_comments);
    RUN_TEST(test_comment_with_special_characters);
    RUN_TEST(test_comment_with_quotes);
    RUN_TEST(test_empty_comments);
    RUN_TEST(test_block_comment_spanning_multiple_lines);
    RUN_TEST(test_comment_at_very_deep_indentation);
    RUN_TEST(test_comments_with_tabs_and_spaces);
    RUN_TEST(test_comment_false_starts);
    RUN_TEST(test_unterminated_block_comment);
    RUN_TEST(test_comments_between_tokens);
    
    PRINT_TEST_RESULTS();
    
    return tests_failed > 0 ? 1 : 0;
}