#ifndef ORUS_PARSER_H
#define ORUS_PARSER_H

#include "vm/vm.h"
#include "compiler/lexer.h"
#include "compiler/ast.h"

// Forward declarations
typedef struct ParserContext ParserContext;

// Arena memory management for parser
typedef struct ArenaBlock {
    void* buffer;
    size_t capacity;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct {
    ArenaBlock* head;
} Arena;

// Parser context structure - eliminates global state
typedef struct ParserContext {
    // Arena for AST node allocation
    Arena arena;
    
    // Recursion depth tracking
    int recursion_depth;
    
    // Token lookahead management
    Token peeked_token;
    bool has_peeked_token;
    Token peeked_token2;
    bool has_peeked_token2;
    
    // Parser configuration
    int max_recursion_depth;
} ParserContext;

// Parser context lifecycle
ParserContext* parser_context_create(void);
void parser_context_destroy(ParserContext* ctx);
void parser_context_reset(ParserContext* ctx);

// Main parsing interface
ASTNode* parseSource(const char* source);
ASTNode* parseSourceWithContext(ParserContext* ctx, const char* source);

// AST cleanup
void freeAST(ASTNode* node);

#endif // ORUS_PARSER_H
