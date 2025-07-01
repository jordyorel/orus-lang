#include "test_framework.h"
#include "../../include/lexer.h"

extern Lexer lexer;

void test_lexer_initialization() {
    const char* source = "test source";
    init_scanner(source);
    
    ASSERT(lexer.start == source, "Lexer start points to source");
    ASSERT(lexer.current == source, "Lexer current points to source start");
    ASSERT(lexer.source == source, "Lexer source points to source");
    ASSERT_EQ(1, lexer.line, "Lexer starts at line 1");
    ASSERT_EQ(1, lexer.column, "Lexer starts at column 1");
    ASSERT(!lexer.inBlockComment, "Lexer starts outside block comment");
    ASSERT_EQ(0, lexer.indentTop, "Lexer starts with empty indent stack");
    ASSERT_EQ(0, lexer.pendingDedents, "Lexer starts with no pending dedents");
    ASSERT(lexer.atLineStart, "Lexer starts at line beginning");
}

void test_single_character_tokens() {
    init_scanner("(){}[],.+-?;/*");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_LEFT_PAREN, token.type, "Recognizes left parenthesis");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_RIGHT_PAREN, token.type, "Recognizes right parenthesis");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_LEFT_BRACE, token.type, "Recognizes left brace");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_RIGHT_BRACE, token.type, "Recognizes right brace");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_LEFT_BRACKET, token.type, "Recognizes left bracket");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_RIGHT_BRACKET, token.type, "Recognizes right bracket");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_COMMA, token.type, "Recognizes comma");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_DOT, token.type, "Recognizes dot");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_PLUS, token.type, "Recognizes plus");

    token = scan_token();
    ASSERT_EQ(TOKEN_MINUS, token.type, "Recognizes minus");
}

void test_two_character_tokens() {
    init_scanner("== != <= >= .. ->");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_EQUAL_EQUAL, token.type, "Recognizes equal equal");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_BANG_EQUAL, token.type, "Recognizes bang equal");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_LESS_EQUAL, token.type, "Recognizes less equal");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_GREATER_EQUAL, token.type, "Recognizes greater equal");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_DOT_DOT, token.type, "Recognizes range operator");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_ARROW, token.type, "Recognizes arrow");
}

void test_keywords() {
    init_scanner("mut if else true false nil");

    Token token = scan_token();
    ASSERT_EQ(TOKEN_MUT, token.type, "Recognizes 'mut' keyword");

    token = scan_token();
    ASSERT_EQ(TOKEN_IF, token.type, "Recognizes 'if' keyword");

    token = scan_token();
    ASSERT_EQ(TOKEN_ELSE, token.type, "Recognizes 'else' keyword");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_TRUE, token.type, "Recognizes 'true' keyword");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_FALSE, token.type, "Recognizes 'false' keyword");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NIL, token.type, "Recognizes 'nil' keyword");
}

void test_identifiers() {
    init_scanner("hello world _private variable123");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Recognizes identifier");
    ASSERT_EQ(5, token.length, "Identifier has correct length");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Recognizes second identifier");
    ASSERT_EQ(5, token.length, "Second identifier has correct length");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Recognizes underscore identifier");
    ASSERT_EQ(8, token.length, "Underscore identifier has correct length");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_IDENTIFIER, token.type, "Recognizes alphanumeric identifier");
    ASSERT_EQ(11, token.length, "Alphanumeric identifier has correct length");
}

void test_numbers() {
    init_scanner("123 456.789 0.5");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Recognizes integer");
    ASSERT_EQ(3, token.length, "Integer has correct length");
    
    token = scan_token();  
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Recognizes float");
    ASSERT_EQ(7, token.length, "Float has correct length");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_NUMBER, token.type, "Recognizes decimal");
    ASSERT_EQ(3, token.length, "Decimal has correct length");
}

void test_strings() {
    init_scanner("\"hello\" \"world with spaces\" \"\"");
    
    Token token = scan_token();
    ASSERT_EQ(TOKEN_STRING, token.type, "Recognizes string");
    ASSERT_EQ(7, token.length, "String has correct length including quotes");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_STRING, token.type, "Recognizes string with spaces");
    ASSERT_EQ(19, token.length, "String with spaces has correct length");
    
    token = scan_token();
    ASSERT_EQ(TOKEN_STRING, token.type, "Recognizes empty string");
    ASSERT_EQ(2, token.length, "Empty string has correct length");
}

void test_line_and_column_tracking() {
    init_scanner("hello\nworld\n  test");
    
    Token token = scan_token();
    ASSERT_EQ(1, token.line, "First token on line 1");
    ASSERT_EQ(1, token.column, "First token at column 1");
    
    token = scan_token();  // newline
    token = scan_token();  // "world"
    ASSERT_EQ(2, token.line, "Second line token on line 2");
    ASSERT_EQ(1, token.column, "Second line token at column 1");
    
    token = scan_token();  // newline
    token = scan_token();  // "test"
    ASSERT_EQ(3, token.line, "Third line token on line 3");
    ASSERT_EQ(3, token.column, "Third line token at column 3 (after spaces)");
}

int main() {
    printf("Running Lexer Tests\n");
    printf("========================================\n");
    
    RUN_TEST(test_lexer_initialization);
    RUN_TEST(test_single_character_tokens);
    RUN_TEST(test_two_character_tokens);
    RUN_TEST(test_keywords);
    RUN_TEST(test_identifiers);
    RUN_TEST(test_numbers);
    RUN_TEST(test_strings);
    RUN_TEST(test_line_and_column_tracking);
    
    PRINT_TEST_RESULTS();
    
    return tests_failed > 0 ? 1 : 0;
}