#include "../../include/parser.h"
#include "../../include/common.h"
#include <stdlib.h>
#include <string.h>

// Simple arena allocator used for AST nodes
typedef struct {
    char* buffer;
    size_t capacity, used;
} Arena;

static Arena parserArena;

static void arena_init(Arena* a, size_t initial) {
    a->buffer = malloc(initial);
    a->capacity = initial;
    a->used = 0;
}

static void* arena_alloc(Arena* a, size_t size) {
    if (a->used + size > a->capacity) {
        size_t newCap = (a->capacity * 2) + size;
        a->buffer = realloc(a->buffer, newCap);
        a->capacity = newCap;
    }
    void* ptr = a->buffer + a->used;
    a->used += size;
    return ptr;
}

static void arena_reset(Arena* a) { a->used = 0; }

static ASTNode* new_node(void) { return arena_alloc(&parserArena, sizeof(ASTNode)); }

static void addStatement(ASTNode*** list, int* count, int* capacity, ASTNode* stmt) {
    if (*count + 1 > *capacity) {
        int newCap = *capacity == 0 ? 4 : (*capacity * 2);
        ASTNode** newArr = arena_alloc(&parserArena, sizeof(ASTNode*) * newCap);
        if (*capacity > 0) {
            memcpy(newArr, *list, sizeof(ASTNode*) * (*count));
        }
        *list = newArr;
        *capacity = newCap;
    }
    (*list)[(*count)++] = stmt;
}

// Simple token lookahead
static Token peekedToken;
static bool hasPeekedToken = false;

static Token peekToken(void) {
    if (!hasPeekedToken) {
        peekedToken = scan_token();
        hasPeekedToken = true;
    }
    return peekedToken;
}

static Token nextToken(void) {
    if (hasPeekedToken) {
        hasPeekedToken = false;
        return peekedToken;
    }
    return scan_token();
}

// Forward declarations
static ASTNode* parseExpression(void);
static ASTNode* parseBinaryExpression(int minPrec);
static ASTNode* parsePrimaryExpression(void);
static ASTNode* parseVariableDeclaration(void);

static int getOperatorPrecedence(TokenType type) {
    switch (type) {
        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return 1;
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_MODULO:
            return 2;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
            return 0;
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
            return 0;
        default:
            return -1;
    }
}

static const char* getOperatorString(TokenType type) {
    switch (type) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_SLASH: return "/";
        case TOKEN_MODULO: return "%";
        case TOKEN_EQUAL_EQUAL: return "==";
        case TOKEN_BANG_EQUAL: return "!=";
        case TOKEN_LESS: return "<";
        case TOKEN_GREATER: return ">";
        case TOKEN_LESS_EQUAL: return "<=";
        case TOKEN_GREATER_EQUAL: return ">=";
        default: return "?";
    }
}

ASTNode* parseSource(const char* source) {
    arena_init(&parserArena, 1 << 16);
    init_scanner(source);
    hasPeekedToken = false;

    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (true) {
        Token t = peekToken();
        if (t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE || t.type == TOKEN_SEMICOLON) {
            nextToken();
            continue;
        }

        ASTNode* stmt = NULL;
        if (t.type == TOKEN_LET) {
            stmt = parseVariableDeclaration();
        } else {
            stmt = parseExpression();
        }
        if (!stmt) return NULL;

        addStatement(&statements, &count, &capacity, stmt);

        t = peekToken();
        if (t.type == TOKEN_SEMICOLON || t.type == TOKEN_NEWLINE) {
            nextToken();
        }
    }

    ASTNode* program = new_node();
    program->type = NODE_PROGRAM;
    program->program.declarations = statements;
    program->program.count = count;
    program->location.line = 1;
    program->location.column = 1;
    program->dataType = NULL;
    return program;
}

static ASTNode* parseVariableDeclaration(void) {
    Token letToken = nextToken();
    if (letToken.type != TOKEN_LET) {
        return NULL;
    }

    Token nameToken = nextToken();
    if (nameToken.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    ASTNode* typeNode = NULL;
    if (peekToken().type == TOKEN_COLON) {
        nextToken();
        Token typeTok = nextToken();
        if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
            typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
            typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
            typeTok.type != TOKEN_BOOL) {
            return NULL;
        }
        int tl = typeTok.length;
        char* typeName = arena_alloc(&parserArena, tl + 1);
        strncpy(typeName, typeTok.start, tl);
        typeName[tl] = '\0';
        typeNode = new_node();
        typeNode->type = NODE_TYPE;
        typeNode->typeAnnotation.name = typeName;
    }

    Token equalToken = nextToken();
    if (equalToken.type != TOKEN_EQUAL) {
        return NULL;
    }

    ASTNode* initializer = parseExpression();
    if (!initializer) {
        return NULL;
    }

    ASTNode* varNode = new_node();
    varNode->type = NODE_VAR_DECL;
    varNode->location.line = letToken.line;
    varNode->location.column = letToken.column;
    varNode->dataType = NULL;

    int len = nameToken.length;
    char* name = arena_alloc(&parserArena, len + 1);
    strncpy(name, nameToken.start, len);
    name[len] = '\0';

    varNode->varDecl.name = name;
    varNode->varDecl.isPublic = false;
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = typeNode;
    varNode->varDecl.isConst = false;

    return varNode;
}

static ASTNode* parseExpression(void) {
    return parseBinaryExpression(0);
}

static ASTNode* parseBinaryExpression(int minPrec) {
    ASTNode* left = parsePrimaryExpression();
    if (!left) return NULL;

    while (true) {
        Token operator = peekToken();
        int prec = getOperatorPrecedence(operator.type);

        if (prec < minPrec || operator.type == TOKEN_EOF) {
            break;
        }

        nextToken();

        ASTNode* right = parseBinaryExpression(prec + 1);
        if (!right) return NULL;

        ASTNode* binaryNode = new_node();
        binaryNode->type = NODE_BINARY;
        binaryNode->binary.left = left;
        binaryNode->binary.right = right;
        binaryNode->binary.op = (char*)getOperatorString(operator.type);
        binaryNode->location.line = operator.line;
        binaryNode->location.column = operator.column;
        binaryNode->dataType = NULL;

        left = binaryNode;
    }

    return left;
}

static ASTNode* parsePrimaryExpression(void) {
    Token token = nextToken();

    switch (token.type) {
        case TOKEN_NUMBER: {
            ASTNode* node = new_node();
            node->type = NODE_LITERAL;
            char numStr[32];
            int len = token.length < 31 ? token.length : 31;
            strncpy(numStr, token.start, len);
            numStr[len] = '\0';
            node->literal.value = I32_VAL(atoi(numStr));
            node->location.line = token.line;
            node->location.column = token.column;
            node->dataType = NULL;
            return node;
        }
        case TOKEN_STRING: {
            ASTNode* node = new_node();
            node->type = NODE_LITERAL;
            int contentLen = token.length - 2;
            char* content = malloc(contentLen + 1);
            strncpy(content, token.start + 1, contentLen);
            content[contentLen] = '\0';
            node->literal.value = I32_VAL(0);
            free(content);
            node->location.line = token.line;
            node->location.column = token.column;
            node->dataType = NULL;
            return node;
        }
        case TOKEN_TRUE: {
            ASTNode* node = new_node();
            node->type = NODE_LITERAL;
            node->literal.value = BOOL_VAL(true);
            node->location.line = token.line;
            node->location.column = token.column;
            node->dataType = NULL;
            return node;
        }
        case TOKEN_FALSE: {
            ASTNode* node = new_node();
            node->type = NODE_LITERAL;
            node->literal.value = BOOL_VAL(false);
            node->location.line = token.line;
            node->location.column = token.column;
            node->dataType = NULL;
            return node;
        }
        case TOKEN_IDENTIFIER: {
            ASTNode* node = new_node();
            node->type = NODE_IDENTIFIER;
            int len = token.length;
            char* name = arena_alloc(&parserArena, len + 1);
            strncpy(name, token.start, len);
            name[len] = '\0';
            node->identifier.name = name;
            node->location.line = token.line;
            node->location.column = token.column;
            node->dataType = NULL;
            return node;
        }
        case TOKEN_LEFT_PAREN: {
            ASTNode* expr = parseExpression();
            Token closeParen = nextToken();
            if (closeParen.type != TOKEN_RIGHT_PAREN) {
                return NULL;
            }
            return expr;
        }
        default:
            return NULL;
    }
}

void freeAST(ASTNode* node) {
    arena_reset(&parserArena);
    (void)node;
}

