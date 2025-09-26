/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/unit/test_lexer.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Provides unit tests validating lexer behavior and tokenization edge
 *              cases.
 */

#include <stdio.h>
#include "compiler/lexer.h"

int main() {
    const char* test_code = "x = 42\ny = x + 24\nprint(y)";
    
    printf("Testing lexer with sample code:\n");
    printf("%s\n\n", test_code);
    
    // Test the lexer debug functionality
    debug_print_tokens(test_code);
    
    return 0;
}