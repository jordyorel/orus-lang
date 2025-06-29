#include <stdio.h>
#include <stdlib.h>
#include "parser.h"
#include "lexer.h"

int main() {
    const char* source = "let x = 42;";
    ASTNode* ast = NULL;
    
    printf("Testing parser with: %s\n", source);
    
    bool success = parse(source, "test.orus", &ast);
    
    if (success) {
        printf("Parsing succeeded!\n");
        if (ast) {
            printf("AST root type: %d\n", ast->type);
        } else {
            printf("AST is NULL\n");
        }
    } else {
        printf("Parsing failed!\n");
    }
    
    return 0;
}
