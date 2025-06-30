#include "../../include/parser.h"
#include "../../include/common.h"
#include "../../include/vm.h"
#include <stdlib.h>
#include <string.h>

// Simple arena allocator used for AST nodes that never moves allocated blocks
typedef struct ArenaBlock {
    char* buffer;
    size_t capacity;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct {
    ArenaBlock* head;
} Arena;

static Arena parserArena;

static void arena_init(Arena* a, size_t initial) {
    a->head = malloc(sizeof(ArenaBlock));
    a->head->buffer = malloc(initial);
    a->head->capacity = initial;
    a->head->used = 0;
    a->head->next = NULL;
}

static void* arena_alloc(Arena* a, size_t size) {
    ArenaBlock* block = a->head;
    if (block->used + size > block->capacity) {
        size_t newCap = block->capacity * 2;
        if (newCap < size) newCap = size;
        ArenaBlock* newBlock = malloc(sizeof(ArenaBlock));
        newBlock->buffer = malloc(newCap);
        newBlock->capacity = newCap;
        newBlock->used = 0;
        newBlock->next = block;
        a->head = newBlock;
        block = newBlock;
    }
    void* ptr = block->buffer + block->used;
    block->used += size;
    return ptr;
}

static void arena_reset(Arena* a) {
    ArenaBlock* block = a->head->next;
    while (block) {
        ArenaBlock* next = block->next;
        free(block->buffer);
        free(block);
        block = next;
    }
    a->head->used = 0;
    a->head->next = NULL;
}

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
static ASTNode* parseStatement(void);
static ASTNode* parseIfStatement(void);
static ASTNode* parseBlock(void);

static int getOperatorPrecedence(TokenType type) {
    switch (type) {
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_MODULO:
            return 4;
        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return 3;
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
            return 2;
        case TOKEN_AND:
            return 1;
        case TOKEN_OR:
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
        case TOKEN_AND: return "and";
        case TOKEN_OR: return "or";
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

        ASTNode* stmt = parseStatement();
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

static ASTNode* parseStatement(void) {
    Token t = peekToken();
    if (t.type == TOKEN_LET) {
        return parseVariableDeclaration();
    } else if (t.type == TOKEN_IF) {
        return parseIfStatement();
    } else {
        return parseExpression();
    }
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

static ASTNode* parseBlock(void) {
    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (true) {
        Token t = peekToken();
        if (t.type == TOKEN_DEDENT || t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE || t.type == TOKEN_SEMICOLON) {
            nextToken();
            continue;
        }
        ASTNode* stmt = parseStatement();
        if (!stmt) return NULL;
        addStatement(&statements, &count, &capacity, stmt);
        t = peekToken();
        if (t.type == TOKEN_SEMICOLON || t.type == TOKEN_NEWLINE) nextToken();
    }
    Token dedent = nextToken();
    if (dedent.type != TOKEN_DEDENT) return NULL;

    ASTNode* block = new_node();
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = count;
    block->location.line = dedent.line;
    block->location.column = dedent.column;
    block->dataType = NULL;
    return block;
}

static ASTNode* parseIfStatement(void) {
    Token ifTok = nextToken();
    if (ifTok.type != TOKEN_IF && ifTok.type != TOKEN_ELIF) return NULL;

    ASTNode* condition = parseExpression();
    if (!condition) return NULL;

    Token colon = nextToken();
    if (colon.type != TOKEN_COLON) return NULL;
    if (nextToken().type != TOKEN_NEWLINE) return NULL;
    if (nextToken().type != TOKEN_INDENT) return NULL;

    ASTNode* thenBranch = parseBlock();
    if (!thenBranch) return NULL;

    if (peekToken().type == TOKEN_NEWLINE) {
        nextToken();
    }

    ASTNode* elseBranch = NULL;
    Token next = peekToken();
    if (next.type == TOKEN_ELIF) {
        elseBranch = parseIfStatement();
    } else if (next.type == TOKEN_ELSE) {
        nextToken();
        if (nextToken().type != TOKEN_COLON) return NULL;
        if (nextToken().type != TOKEN_NEWLINE) return NULL;
        if (nextToken().type != TOKEN_INDENT) return NULL;
        elseBranch = parseBlock();
        if (!elseBranch) return NULL;
        if (peekToken().type == TOKEN_NEWLINE) nextToken();
    }

    ASTNode* node = new_node();
    node->type = NODE_IF;
    node->ifStmt.condition = condition;
    node->ifStmt.thenBranch = thenBranch;
    node->ifStmt.elseBranch = elseBranch;
    node->location.line = ifTok.line;
    node->location.column = ifTok.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseAssignment(void);

static ASTNode* parseExpression(void) { return parseAssignment(); }

static ASTNode* parseTernary(ASTNode* condition) {
    if (peekToken().type != TOKEN_QUESTION) return condition;
    nextToken();
    ASTNode* trueExpr = parseExpression();
    if (!trueExpr) return NULL;
    if (nextToken().type != TOKEN_COLON) return NULL;
    ASTNode* falseExpr = parseExpression();
    if (!falseExpr) return NULL;
    ASTNode* node = new_node();
    node->type = NODE_TERNARY;
    node->ternary.condition = condition;
    node->ternary.trueExpr = trueExpr;
    node->ternary.falseExpr = falseExpr;
    node->location = condition->location;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseAssignment(void) {
    ASTNode* left = parseBinaryExpression(0);
    if (!left) return NULL;
    if (peekToken().type == TOKEN_EQUAL) {
        nextToken();
        ASTNode* value = parseBinaryExpression(0);
        if (!value || left->type != NODE_IDENTIFIER) return NULL;
        ASTNode* node = new_node();
        node->type = NODE_ASSIGN;
        node->assign.name = left->identifier.name;
        node->assign.value = value;
        node->location = left->location;
        node->dataType = NULL;
        return node;
    }
    return parseTernary(left);
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
            char* content = arena_alloc(&parserArena, contentLen + 1);
            strncpy(content, token.start + 1, contentLen);
            content[contentLen] = '\0';
            ObjString* s = allocateString(content, contentLen);
            node->literal.value.type = VAL_STRING;
            node->literal.value.as.obj = (Obj*)s;
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
        case TOKEN_PRINT:
        case TOKEN_PRINT_NO_NL: {
            bool newline = token.type == TOKEN_PRINT;
            Token next = nextToken();
            if (next.type != TOKEN_LEFT_PAREN) {
                return NULL;
            }
            ASTNode** args = NULL;
            int count = 0;
            int capacity = 0;
            if (peekToken().type != TOKEN_RIGHT_PAREN) {
                while (true) {
                    ASTNode* expr = parseExpression();
                    if (!expr) return NULL;
                    addStatement(&args, &count, &capacity, expr);
                    if (peekToken().type != TOKEN_COMMA) break;
                    nextToken();
                }
            }
            Token close = nextToken();
            if (close.type != TOKEN_RIGHT_PAREN) {
                return NULL;
            }
            ASTNode* node = new_node();
            node->type = NODE_PRINT;
            node->print.values = args;
            node->print.count = count;
            node->print.newline = newline;
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

