#ifndef ORUS_PARSER_H
#define ORUS_PARSER_H

#include "lexer.h"
#include "vm.h"

// Simplified constraint system for parser
typedef enum {
    CONSTRAINT_NONE,
    CONSTRAINT_NUMERIC,
    CONSTRAINT_COMPARABLE
} GenericConstraint;


typedef enum {
    PREC_NONE,
    PREC_ASSIGNMENT,   // =
    PREC_CONDITIONAL,  // ?:
    PREC_OR,           // or
    PREC_AND,          // and
    PREC_BIT_OR,       // |
    PREC_BIT_XOR,      // ^
    PREC_BIT_AND,      // &
    PREC_EQUALITY,     // == !=
    PREC_COMPARISON,   // < > <= >=
    PREC_SHIFT,        // << >>
    PREC_TERM,         // + -
    PREC_FACTOR,       // * /
    PREC_UNARY,        // not -
    PREC_CALL,         // . ()
    PREC_PRIMARY
} Precedence;

typedef struct {
    Token current;
    Token previous;
    bool hadError;
    bool panicMode;
    Lexer* lexer;
    int functionDepth;      // Track nested function declarations
    Type* currentImplType;  // Track struct type for methods
    ObjString** genericParams;
    GenericConstraint* genericConstraints;
    int genericCount;
    int genericCapacity;
    const char* filePath;
    int parenDepth;
    bool inMatchCase;
    bool doubleColonWarned;
} Parser;

typedef ASTNode* (*ParseFn)(Parser*);

typedef struct {
    ParseFn prefix;
    ASTNode* (*infix)(Parser*, ASTNode* left);
    Precedence precedence;
} ParseRule;

void initParser(Parser* parser, Lexer* lexer, const char* filePath);
bool parse(const char* source, const char* filePath, ASTNode** ast);
ParseRule* get_rule(TokenType type);

#endif