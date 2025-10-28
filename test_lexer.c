#include <stdio.h>
#include <stdlib.h>
#include "include/compiler/lexer.h"

//  gcc -I include test_lexer.c src/compiler/frontend/lexer.c -o test_lexer
int main() {
    const char* source_code = "x: i32 = 5";

    // Initialize the scanner
    init_scanner(source_code);
    
    // Tokenize the source code
    printf("Tokens:\n");
    for (;;) {
        Token token = scan_token();
        const char* type_str = token_type_to_string(token.type);
        
        printf("  %-20s '%.*s' (line %d, col %d)\n",
               type_str,
               token.length,
               token.start,
               token.line,
               token.column);
        
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) {
            break;
        }
    }
    
    return 0;
}
