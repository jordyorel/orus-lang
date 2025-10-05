/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/compiler/lexer.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Token definitions and lexer API used to scan Orus source files into
 *              lexical tokens.
 */

#ifndef ORUS_LEXER_H
#define ORUS_LEXER_H

#include <stdbool.h>

typedef enum {
    // Single-character tokens.
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET,
    TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA,
    TOKEN_DOT,
    TOKEN_MINUS,
    TOKEN_PLUS,
    TOKEN_QUESTION,
    TOKEN_SEMICOLON,
    TOKEN_SLASH,
    TOKEN_STAR,
    // One or two character tokens.
    TOKEN_BANG_EQUAL,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_MODULO,
    TOKEN_PLUS_EQUAL,
    TOKEN_MINUS_EQUAL,
    TOKEN_STAR_EQUAL,
    TOKEN_SLASH_EQUAL,
    TOKEN_MODULO_EQUAL,
    TOKEN_DOT_DOT,  // Range operator

    // function return
    TOKEN_ARROW,

    // Literals.
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    // Keywords.
    TOKEN_AND,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_PASS,
    TOKEN_ELSE,
    TOKEN_ELIF,
    TOKEN_FOR,
    TOKEN_FN,
    TOKEN_IF,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_PRINT,
    TOKEN_PRINT_NO_NL,
    TOKEN_RETURN,
    TOKEN_MUT,
    TOKEN_WHILE,
    TOKEN_TRY,
    TOKEN_CATCH,
    TOKEN_IN,
    TOKEN_STRUCT,
    TOKEN_ENUM,
    TOKEN_IMPL,
    TOKEN_IMPORT,
    TOKEN_AS,
    TOKEN_MATCH,
    TOKEN_MATCHES,
    TOKEN_PUB,
    TOKEN_DEFINE,

    // Bitwise operators
    TOKEN_BIT_AND,
    TOKEN_BIT_OR,
    TOKEN_BIT_XOR,
    TOKEN_BIT_NOT,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,

    TOKEN_ERROR,
    TOKEN_EOF,

    TOKEN_NEWLINE,

    TOKEN_COLON,  // Add this for type annotations
    TOKEN_APOSTROPHE,
    TOKEN_INDENT,
    TOKEN_DEDENT,
} TokenType;

typedef struct {
    TokenType type;
    const char* start;
    int length;
    int line;
    int column;  // Add precise column tracking
} Token;

typedef struct {
    const char* start;
    const char* current;
    const char* source;  // Pointer to the beginning of the entire source
    int line;
    int column;             // Add column tracking
    const char* lineStart;  // Track start of current line for precise column
                            // calculation
    bool inBlockComment;    // Track whether we are inside a block comment
    int indentStack[64];
    int indentTop;
    int pendingDedents;
    bool atLineStart;
} Lexer;

typedef struct {
    const char* keyword;
    TokenType type;
} KeywordEntry;

// Context-based lexer state (replaces global state)
typedef struct LexerContext {
    Lexer lexer;
} LexerContext;

// Context lifecycle management
LexerContext* lexer_context_create(const char* source);
void lexer_context_destroy(LexerContext* ctx);

// Context-based API
void init_scanner_ctx(LexerContext* ctx, const char* source);
Token scan_token_ctx(LexerContext* ctx);

// Backward compatibility API (uses internal context)
void init_scanner(const char* source);
Token scan_token();

// Debug functions
const char* token_type_to_string(TokenType type);
void print_token(Token token);
void debug_print_tokens(const char* source);

// Expose the global scanner instance so other modules (like the parser)
// can access the raw source when producing error messages.
extern Lexer lexer;

#endif
