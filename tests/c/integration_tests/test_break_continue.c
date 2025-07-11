#include "../../../include/vm.h"
#include "../../../include/parser.h"
#include "../../../include/compiler.h"
#include "../../../include/lexer.h"
#include "test_framework.h"
#include <string.h>

// Test that break and continue tokens are recognized by lexer
void test_break_continue_tokens() {
    // Test break token
    init_scanner("break");
    Token breakToken = scan_token();
    ASSERT(breakToken.type == TOKEN_BREAK, "Break token should be recognized");
    
    // Test continue token  
    init_scanner("continue");
    Token continueToken = scan_token();
    ASSERT(continueToken.type == TOKEN_CONTINUE, "Continue token should be recognized");
}

// Test error case: break outside of loop
void test_break_outside_loop_error() {
    const char* source = "break";
    
    initVM();
    InterpretResult result = interpret(source);
    ASSERT(result == INTERPRET_COMPILE_ERROR, "Break outside loop should cause compile error");
    freeVM();
}

// Test error case: continue outside of loop
void test_continue_outside_loop_error() {
    const char* source = "continue";
    
    initVM();
    InterpretResult result = interpret(source);
    ASSERT(result == INTERPRET_COMPILE_ERROR, "Continue outside loop should cause compile error");
    freeVM();
}

int main() {
    printf("Running break/continue statement tests...\n");
    
    RUN_TEST(test_break_continue_tokens);
    RUN_TEST(test_break_outside_loop_error);
    RUN_TEST(test_continue_outside_loop_error);
    
    PRINT_TEST_RESULTS();
    return tests_failed > 0 ? 1 : 0;
}