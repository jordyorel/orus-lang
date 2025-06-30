#include "include/lexer.h"
#include <stdio.h>

int main() {
    const char* source = ":\n    print";
    init_scanner(source);
    
    printf("Testing input: %s\n", source);
    printf("Tokens generated:\n");
    
    Token token;
    int count = 0;
    while ((token = scan_token()).type != TOKEN_EOF && count < 10) {
        printf("  %d. Type: %d", count + 1, token.type);
        
        // Print token type names for clarity
        switch (token.type) {
            case TOKEN_COLON: printf(" (COLON)"); break;
            case TOKEN_NEWLINE: printf(" (NEWLINE)"); break;
            case TOKEN_INDENT: printf(" (INDENT)"); break;
            case TOKEN_PRINT: printf(" (PRINT)"); break;
            case TOKEN_IDENTIFIER: printf(" (IDENTIFIER)"); break;
            case TOKEN_EOF: printf(" (EOF)"); break;
            default: printf(" (OTHER)"); break;
        }
        
        printf(", Line: %d, Column: %d", token.line, token.column);
        printf(", Length: %d", token.length);
        printf(", Text: '");
        for (int i = 0; i < token.length; i++) {
            char c = token.start[i];
            if (c == '\n') printf("\\n");
            else if (c == '\t') printf("\\t");
            else if (c == ' ') printf("·"); // visible space
            else printf("%c", c);
        }
        printf("'\n");
        
        count++;
    }
    
    return 0;
}