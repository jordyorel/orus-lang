#include "../../include/parser.h"
#include "../../include/common.h"
#include "../../include/vm.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

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

static Token peekedToken2;
static bool hasPeekedToken2 = false;

static Token peekToken(void) {
    if (!hasPeekedToken) {
        peekedToken = scan_token();
        hasPeekedToken = true;
    }
    return peekedToken;
}

static Token peekSecondToken(void) {
    if (!hasPeekedToken) {
        peekedToken = scan_token();
        hasPeekedToken = true;
    }
    if (!hasPeekedToken2) {
        peekedToken2 = scan_token();
        hasPeekedToken2 = true;
    }
    return peekedToken2;
}

static Token nextToken(void) {
    if (hasPeekedToken) {
        Token result = peekedToken;
        if (hasPeekedToken2) {
            peekedToken = peekedToken2;
            hasPeekedToken2 = false;
        } else {
            hasPeekedToken = false;
        }
        return result;
    }
    return scan_token();
}

// Forward declarations
static ASTNode* parsePrintStatement(void);
static ASTNode* parseExpression(void);
static ASTNode* parseBinaryExpression(int minPrec);
static ASTNode* parsePrimaryExpression(void);
static ASTNode* parseVariableDeclaration(bool isMutable, Token nameToken);
static ASTNode* parseAssignOrVarList(bool isMutable, Token nameToken);
static ASTNode* parseStatement(void);
static ASTNode* parseIfStatement(void);
static ASTNode* parseWhileStatement(void);
static ASTNode* parseForStatement(void);
static ASTNode* parseBreakStatement(void);
static ASTNode* parseContinueStatement(void);
static ASTNode* parseBlock(void);
static ASTNode* parseFunctionDefinition(void);
static ASTNode* parseReturnStatement(void);
static ASTNode* parseCallExpression(ASTNode* callee);
static ASTNode* parseFunctionType(void);

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
        if (t.type == TOKEN_NEWLINE) {
            nextToken();
            continue;
        }
        if (t.type == TOKEN_SEMICOLON) {
            fprintf(stderr,
                    "Error at line %d:%d: Semicolons are not allowed in Orus."
                    " Use a newline to separate statements.\n",
                    t.line, t.column);
            return NULL;
        }

        ASTNode* stmt = parseStatement();
        if (!stmt) {
            // If in debug mode, provide some indication of where parsing failed
            extern VM vm;
            if (vm.devMode) {
                Token currentToken = peekToken();
                fprintf(stderr, "Debug: Failed to parse statement at line %d, column %d\n", 
                       currentToken.line, currentToken.column);
                fprintf(stderr, "Debug: Current token type: %d\n", currentToken.type);
                if (currentToken.type == TOKEN_IDENTIFIER || currentToken.type == TOKEN_FOR || 
                    currentToken.type == TOKEN_WHILE) {
                    fprintf(stderr, "Debug: Token text: '%.*s'\n", 
                           currentToken.length, currentToken.start);
                }
            }
            return NULL;
        }

        addStatement(&statements, &count, &capacity, stmt);

        t = peekToken();
        if (t.type == TOKEN_NEWLINE) {
            nextToken();
        } else if (t.type == TOKEN_SEMICOLON) {
            fprintf(stderr,
                    "Error at line %d:%d: Semicolons are not allowed in Orus."
                    " Use a newline to separate statements.\n",
                    t.line, t.column);
            return NULL;
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

    if (t.type == TOKEN_PRINT || t.type == TOKEN_PRINT_NO_NL) {
        return parsePrintStatement();
    }
    if (t.type == TOKEN_APOSTROPHE) {
        nextToken();
        Token labelTok = nextToken();
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        if (nextToken().type != TOKEN_COLON) return NULL;
        int len = labelTok.length;
        char* label = arena_alloc(&parserArena, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        Token after = peekToken();
        ASTNode* stmt = NULL;
        if (after.type == TOKEN_WHILE) {
            stmt = parseWhileStatement();
            if (stmt) stmt->whileStmt.label = label;
        } else if (after.type == TOKEN_FOR) {
            stmt = parseForStatement();
            if (stmt->type == NODE_FOR_RANGE) stmt->forRange.label = label;
            else if (stmt->type == NODE_FOR_ITER) stmt->forIter.label = label;
        } else {
            return NULL;
        }
        return stmt;
    }
    if (t.type == TOKEN_MUT) {
        nextToken(); // consume TOKEN_MUT
        Token nameTok = nextToken(); // get identifier
        if (nameTok.type != TOKEN_IDENTIFIER) return NULL;
        if (peekToken().type == TOKEN_COLON) {
            return parseVariableDeclaration(true, nameTok);
        }
        return parseAssignOrVarList(true, nameTok);
    }
    if (t.type == TOKEN_LET) {
        // ERROR: 'let' is not supported in Orus
        fprintf(stderr, "Error at line %d:%d: 'let' keyword is not supported in Orus.\n", 
                t.line, t.column);
        fprintf(stderr, "Use 'mut variable_name = value' for mutable variables\n");
        fprintf(stderr, "or 'variable_name = value' for immutable variables.\n");
        return NULL;
    }
    if (t.type == TOKEN_IDENTIFIER) {
        Token second = peekSecondToken();
        if (second.type == TOKEN_COLON) {
            nextToken();
            return parseVariableDeclaration(false, t);
        } else if (second.type == TOKEN_EQUAL) {
            nextToken();
            return parseAssignOrVarList(false, t);
        }
    }
    if (t.type == TOKEN_IF) {
        return parseIfStatement();
    } else if (t.type == TOKEN_WHILE) {
        return parseWhileStatement();
    } else if (t.type == TOKEN_FOR) {
        return parseForStatement();
    } else if (t.type == TOKEN_BREAK) {
        return parseBreakStatement();
    } else if (t.type == TOKEN_CONTINUE) {
        return parseContinueStatement();
    } else if (t.type == TOKEN_FN) {
        return parseFunctionDefinition();
    } else if (t.type == TOKEN_RETURN) {
        return parseReturnStatement();
    } else {
        return parseExpression();
    }
}

static ASTNode* parsePrintStatement(void) {
    // Consume PRINT or PRINT_NO_NL
    Token printTok = nextToken();
    bool newline = (printTok.type == TOKEN_PRINT);

    // Expect '('
    Token left = nextToken();
    if (left.type != TOKEN_LEFT_PAREN) return NULL;

    // Gather zero or more comma-separated expressions
    ASTNode** args = NULL;
    int count = 0, capacity = 0;
    if (peekToken().type != TOKEN_RIGHT_PAREN) {
        while (true) {
            ASTNode* expr = parseExpression();
            if (!expr) return NULL;
            addStatement(&args, &count, &capacity, expr);
            if (peekToken().type != TOKEN_COMMA) break;
            nextToken();  // consume comma
        }
    }

    // Expect ')'
    Token close = nextToken();
    if (close.type != TOKEN_RIGHT_PAREN) return NULL;

    // Build the NODE_PRINT AST node
    ASTNode* node = new_node();
    node->type = NODE_PRINT;
    node->print.values = args;
    node->print.count = count;
    node->print.newline = newline;
    node->location.line = printTok.line;
    node->location.column = printTok.column;
    node->dataType = NULL;

    return node;
}

static ASTNode* parseVariableDeclaration(bool isMutable, Token nameToken) {

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

    // Check for redundant type annotation with literal suffix
    if (typeNode && initializer->type == NODE_LITERAL) {
        const char* declaredType = typeNode->typeAnnotation.name;
        ValueType literalType = initializer->literal.value.type;
        
        bool isRedundant = false;
        if ((strcmp(declaredType, "u32") == 0 && literalType == VAL_U32) ||
            (strcmp(declaredType, "u64") == 0 && literalType == VAL_U64) ||
            (strcmp(declaredType, "i64") == 0 && literalType == VAL_I64) ||
            (strcmp(declaredType, "f64") == 0 && literalType == VAL_F64)) {
            isRedundant = true;
        }
        
        if (isRedundant) {
            printf("Warning: Redundant type annotation at line %d:%d. "
                   "Literal already has type suffix matching declared type '%s'. "
                   "Consider using just 'x = value%s' instead of 'x: %s = value%s'.\n",
                   nameToken.line, nameToken.column, declaredType, 
                   declaredType, declaredType, declaredType);
        }
    }

    ASTNode* varNode = new_node();
    varNode->type = NODE_VAR_DECL;
    varNode->location.line = nameToken.line;
    varNode->location.column = nameToken.column;
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
    varNode->varDecl.isMutable = isMutable;

    ASTNode** vars = NULL;
    int count = 0;
    int capacity = 0;
    addStatement(&vars, &count, &capacity, varNode);

    while (peekToken().type == TOKEN_COMMA) {
        nextToken();
        while (peekToken().type == TOKEN_NEWLINE) nextToken();
        Token nextName = nextToken();
        if (nextName.type != TOKEN_IDENTIFIER) return NULL;

        ASTNode* nextType = NULL;
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
            nextType = new_node();
            nextType->type = NODE_TYPE;
            nextType->typeAnnotation.name = typeName;
        }

        Token eqTok = nextToken();
        if (eqTok.type != TOKEN_EQUAL) return NULL;
        ASTNode* init = parseExpression();
        if (!init) return NULL;

        ASTNode* n = new_node();
        n->type = NODE_VAR_DECL;
        n->location.line = nextName.line;
        n->location.column = nextName.column;
        n->dataType = NULL;

        int nlen = nextName.length;
        char* nname = arena_alloc(&parserArena, nlen + 1);
        strncpy(nname, nextName.start, nlen);
        nname[nlen] = '\0';

        n->varDecl.name = nname;
        n->varDecl.isPublic = false;
        n->varDecl.initializer = init;
        n->varDecl.typeAnnotation = nextType;
        n->varDecl.isConst = false;
        n->varDecl.isMutable = isMutable;

        addStatement(&vars, &count, &capacity, n);
    }

    if (count == 1) {
        return varNode;
    }

    ASTNode* block = new_node();
    block->type = NODE_BLOCK;
    block->block.statements = vars;
    block->block.count = count;
    block->location = varNode->location;
    block->dataType = NULL;
    return block;
}

static ASTNode* parseAssignOrVarList(bool isMutable, Token nameToken) {
    if (nextToken().type != TOKEN_EQUAL) return NULL;
    ASTNode* initializer = parseExpression();
    if (!initializer) return NULL;

    if (peekToken().type != TOKEN_COMMA && !isMutable) {
        // Regular assignment
        ASTNode* node = new_node();
        node->type = NODE_ASSIGN;
        int len = nameToken.length;
        char* name = arena_alloc(&parserArena, len + 1);
        strncpy(name, nameToken.start, len);
        name[len] = '\0';
        node->assign.name = name;
        node->assign.value = initializer;
        node->location.line = nameToken.line;
        node->location.column = nameToken.column;
        node->dataType = NULL;
        return node;
    }

    // Treat as variable declaration list
    ASTNode** vars = NULL;
    int count = 0;
    int capacity = 0;

    ASTNode* first = new_node();
    first->type = NODE_VAR_DECL;
    first->location.line = nameToken.line;
    first->location.column = nameToken.column;
    first->dataType = NULL;

    int len = nameToken.length;
    char* name = arena_alloc(&parserArena, len + 1);
    strncpy(name, nameToken.start, len);
    name[len] = '\0';

    first->varDecl.name = name;
    first->varDecl.isPublic = false;
    first->varDecl.initializer = initializer;
    first->varDecl.typeAnnotation = NULL;
    first->varDecl.isConst = false;
    first->varDecl.isMutable = isMutable;

    addStatement(&vars, &count, &capacity, first);

    while (peekToken().type == TOKEN_COMMA) {
        nextToken();
        while (peekToken().type == TOKEN_NEWLINE) nextToken();
        Token nextName = nextToken();
        if (nextName.type != TOKEN_IDENTIFIER) return NULL;

        ASTNode* nextType = NULL;
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
            nextType = new_node();
            nextType->type = NODE_TYPE;
            nextType->typeAnnotation.name = typeName;
        }

        if (nextToken().type != TOKEN_EQUAL) return NULL;
        ASTNode* init = parseExpression();
        if (!init) return NULL;

        ASTNode* n = new_node();
        n->type = NODE_VAR_DECL;
        n->location.line = nextName.line;
        n->location.column = nextName.column;
        n->dataType = NULL;

        int nlen = nextName.length;
        char* nname = arena_alloc(&parserArena, nlen + 1);
        strncpy(nname, nextName.start, nlen);
        nname[nlen] = '\0';

        n->varDecl.name = nname;
        n->varDecl.isPublic = false;
        n->varDecl.initializer = init;
        n->varDecl.typeAnnotation = nextType;
        n->varDecl.isConst = false;
        n->varDecl.isMutable = isMutable;

        addStatement(&vars, &count, &capacity, n);
    }

    if (count == 1) return first;

    ASTNode* block = new_node();
    block->type = NODE_BLOCK;
    block->block.statements = vars;
    block->block.count = count;
    block->location = first->location;
    block->dataType = NULL;
    return block;
}

static ASTNode* parseBlock(void) {
    extern VM vm;
    if (vm.devMode) {
        fprintf(stderr, "Debug: Entering parseBlock\n");
    }
    
    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (true) {
        Token t = peekToken();
        if (vm.devMode) {
            fprintf(stderr, "Debug: parseBlock - Current token type: %d\n", t.type);
        }
        if (t.type == TOKEN_DEDENT || t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE) {
            nextToken();
            continue;
        }
        if (t.type == TOKEN_SEMICOLON) {
            fprintf(stderr,
                    "Error at line %d:%d: Semicolons are not allowed in Orus."
                    " Use a newline to separate statements.\n",
                    t.line, t.column);
            return NULL;
        }
        ASTNode* stmt = parseStatement();
        if (!stmt) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: parseBlock failed to parse statement\n");
            }
            return NULL;
        }
        addStatement(&statements, &count, &capacity, stmt);
        t = peekToken();
        if (t.type == TOKEN_NEWLINE) {
            nextToken();
        } else if (t.type == TOKEN_SEMICOLON) {
            fprintf(stderr,
                    "Error at line %d:%d: Semicolons are not allowed in Orus."
                    " Use a newline to separate statements.\n",
                    t.line, t.column);
            return NULL;
        }
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

static ASTNode* parseWhileStatement(void) {
    Token whileTok = nextToken();
    if (whileTok.type != TOKEN_WHILE) return NULL;

    ASTNode* condition = parseExpression();
    if (!condition) return NULL;

    Token colon = nextToken();
    if (colon.type != TOKEN_COLON) return NULL;
    if (nextToken().type != TOKEN_NEWLINE) return NULL;
    if (nextToken().type != TOKEN_INDENT) return NULL;

    ASTNode* body = parseBlock();
    if (!body) return NULL;
    if (peekToken().type == TOKEN_NEWLINE) nextToken();

    ASTNode* node = new_node();
    node->type = NODE_WHILE;
    node->whileStmt.condition = condition;
    node->whileStmt.body = body;
    node->whileStmt.label = NULL;
    node->location.line = whileTok.line;
    node->location.column = whileTok.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseForStatement(void) {
    extern VM vm;
    if (vm.devMode) {
        fprintf(stderr, "Debug: Entering parseForStatement\n");
    }
    
    Token forTok = nextToken();
    if (forTok.type != TOKEN_FOR) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_FOR, got %d\n", forTok.type);
        }
        return NULL;
    }

    Token nameTok = nextToken();
    if (nameTok.type != TOKEN_IDENTIFIER) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IDENTIFIER after 'for', got %d\n", nameTok.type);
        }
        return NULL;
    }

    Token inTok = nextToken();
    if (inTok.type != TOKEN_IN) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IN after identifier, got %d\n", inTok.type);
        }
        return NULL;
    }

    ASTNode* first = parseExpression();
    if (!first) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Failed to parse first expression in for loop\n");
        }
        return NULL;
    }

    bool isRange = false;
    bool inclusive = false;
    ASTNode* end = NULL;
    ASTNode* step = NULL;
    if (peekToken().type == TOKEN_DOT_DOT) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Found TOKEN_DOT_DOT, parsing as range\n");
        }
        isRange = true;
        nextToken();
        if (peekToken().type == TOKEN_EQUAL) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Found TOKEN_EQUAL, marking as inclusive range\n");
            }
            nextToken();
            inclusive = true;
        }
        if (vm.devMode) {
            Token peekEnd = peekToken();
            fprintf(stderr, "Debug: About to parse end expression, next token type: %d\n", peekEnd.type);
            if (peekEnd.type == 44 || peekEnd.type == 38) {  // TOKEN_NUMBER-ish
                fprintf(stderr, "Debug: Token text: '%.*s'\n", peekEnd.length, peekEnd.start);
            }
        }
        end = parseExpression();
        if (!end) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Failed to parse end expression in range\n");
            }
            return NULL;
        }
        if (vm.devMode) {
            fprintf(stderr, "Debug: Successfully parsed end expression\n");
            Token afterEnd = peekToken();
            fprintf(stderr, "Debug: After parsing end expression, next token type: %d\n", afterEnd.type);
            if (afterEnd.type == 44 || afterEnd.type == 38) {  // TOKEN_NUMBER-ish
                fprintf(stderr, "Debug: Token text: '%.*s'\n", afterEnd.length, afterEnd.start);
            }
        }
        if (peekToken().type == TOKEN_DOT_DOT) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Found second TOKEN_DOT_DOT, parsing step\n");
            }
            nextToken();
            step = parseExpression();
            if (!step) {
                if (vm.devMode) {
                    fprintf(stderr, "Debug: Failed to parse step expression in range\n");
                }
                return NULL;
            }
        }
    }

    Token colon = nextToken();
    if (colon.type != TOKEN_COLON) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_COLON after range, got %d\n", colon.type);
        }
        return NULL;
    }
    
    Token newline = nextToken();
    if (newline.type != TOKEN_NEWLINE) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_NEWLINE after colon, got %d\n", newline.type);
        }
        return NULL;
    }
    
    Token indent = nextToken();
    if (indent.type != TOKEN_INDENT) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_INDENT after newline, got %d\n", indent.type);
        }
        return NULL;
    }

    ASTNode* body = parseBlock();
    if (!body) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Failed to parse body block in for loop\n");
        }
        return NULL;
    }
    if (peekToken().type == TOKEN_NEWLINE) nextToken();

    int len = nameTok.length;
    char* name = arena_alloc(&parserArena, len + 1);
    strncpy(name, nameTok.start, len);
    name[len] = '\0';

    ASTNode* node = new_node();
    if (isRange) {
        node->type = NODE_FOR_RANGE;
        node->forRange.varName = name;
        node->forRange.start = first;
        node->forRange.end = end;
        node->forRange.step = step;
        node->forRange.inclusive = inclusive;
        node->forRange.body = body;
        node->forRange.label = NULL;
    } else {
        node->type = NODE_FOR_ITER;
        node->forIter.varName = name;
        node->forIter.iterable = first;
        node->forIter.body = body;
        node->forIter.label = NULL;
    }
    node->location.line = forTok.line;
    node->location.column = forTok.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseBreakStatement(void) {
    Token breakToken = nextToken();
    if (breakToken.type != TOKEN_BREAK) {
        return NULL;
    }
    
    ASTNode* node = new_node();
    node->type = NODE_BREAK;
    node->location.line = breakToken.line;
    node->location.column = breakToken.column;
    node->dataType = NULL;
    node->breakStmt.label = NULL;
    if (peekToken().type == TOKEN_APOSTROPHE) {
        nextToken();
        Token labelTok = nextToken();
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        int len = labelTok.length;
        char* label = arena_alloc(&parserArena, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        node->breakStmt.label = label;
    }
    return node;
}

static ASTNode* parseContinueStatement(void) {
    Token continueToken = nextToken();
    if (continueToken.type != TOKEN_CONTINUE) {
        return NULL;
    }
    
    ASTNode* node = new_node();
    node->type = NODE_CONTINUE;
    node->location.line = continueToken.line;
    node->location.column = continueToken.column;
    node->dataType = NULL;
    node->continueStmt.label = NULL;
    if (peekToken().type == TOKEN_APOSTROPHE) {
        nextToken();
        Token labelTok = nextToken();
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        int len = labelTok.length;
        char* label = arena_alloc(&parserArena, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        node->continueStmt.label = label;
    }
    return node;
}

static ASTNode* parseAssignment(void);

static ASTNode* parseExpression(void) { return parseAssignment(); }

// Parse inline conditional expressions using Python-like syntax:
// expr if cond [elif cond]* else expr
static ASTNode* parseInlineIf(ASTNode* expr) {
    if (peekToken().type != TOKEN_IF) return expr;
    nextToken();

    ASTNode* condition = parseExpression();
    if (!condition) return NULL;

    ASTNode* root = new_node();
    root->type = NODE_IF;
    root->ifStmt.condition = condition;
    root->ifStmt.thenBranch = expr;
    root->ifStmt.elseBranch = NULL;
    root->location = expr->location;
    root->dataType = NULL;

    ASTNode* current = root;
    while (peekToken().type == TOKEN_ELIF) {
        nextToken();
        ASTNode* elifCond = parseExpression();
        if (!elifCond) return NULL;
        ASTNode* newIf = new_node();
        newIf->type = NODE_IF;
        newIf->ifStmt.condition = elifCond;
        newIf->ifStmt.thenBranch = expr;
        newIf->ifStmt.elseBranch = NULL;
        newIf->location = expr->location;
        newIf->dataType = NULL;
        current->ifStmt.elseBranch = newIf;
        current = newIf;
    }

    if (peekToken().type == TOKEN_ELSE) {
        nextToken();
        ASTNode* elseExpr = parseExpression();
        if (!elseExpr) return NULL;
        current->ifStmt.elseBranch = elseExpr;
    }

    return root;
}

static ASTNode* parseUnaryExpression(void) {
    Token t = peekToken();
    if (t.type == TOKEN_MINUS || t.type == TOKEN_NOT || t.type == TOKEN_BIT_NOT) {
        nextToken();
        ASTNode* operand = parseUnaryExpression();
        if (!operand) return NULL;
        ASTNode* node = new_node();
        node->type = NODE_UNARY;
        if (t.type == TOKEN_MINUS) node->unary.op = "-";
        else if (t.type == TOKEN_NOT) node->unary.op = "not";
        else node->unary.op = "~";
        node->unary.operand = operand;
        node->location.line = t.line;
        node->location.column = t.column;
        node->dataType = NULL;
        return node;
    }
    return parsePrimaryExpression();
}

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

    TokenType t = peekToken().type;
    if (t == TOKEN_EQUAL || t == TOKEN_PLUS_EQUAL || t == TOKEN_MINUS_EQUAL ||
        t == TOKEN_STAR_EQUAL || t == TOKEN_SLASH_EQUAL) {
        nextToken();
        ASTNode* value = NULL;

        if (t == TOKEN_EQUAL) {
            // Use full expression parsing so constructs like
            // x = cond ? a : b are handled correctly
            value = parseAssignment();
        } else {
            ASTNode* right = parseAssignment();
            if (!right) return NULL;
            if (left->type != NODE_IDENTIFIER) return NULL;
            ASTNode* binary = new_node();
            binary->type = NODE_BINARY;
            binary->binary.left = left;
            binary->binary.right = right;
            switch (t) {
                case TOKEN_PLUS_EQUAL:
                    binary->binary.op = "+";
                    break;
                case TOKEN_MINUS_EQUAL:
                    binary->binary.op = "-";
                    break;
                case TOKEN_STAR_EQUAL:
                    binary->binary.op = "*";
                    break;
                case TOKEN_SLASH_EQUAL:
                    binary->binary.op = "/";
                    break;
                default:
                    binary->binary.op = "+";
                    break;
            }
            value = binary;
        }
        if (!value || left->type != NODE_IDENTIFIER) return NULL;
        ASTNode* node = new_node();
        node->type = NODE_ASSIGN;
        node->assign.name = left->identifier.name;
        node->assign.value = value;
        node->location = left->location;
        node->dataType = NULL;
        return node;
    }
    ASTNode* expr = parseTernary(left);
    if (!expr) return NULL;
    return parseInlineIf(expr);
}

static ASTNode* parseBinaryExpression(int minPrec) {
    ASTNode* left = parseUnaryExpression();
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

            /* Copy token text and remove underscores */
            char raw[64];
            int len = token.length < 63 ? token.length : 63;
            strncpy(raw, token.start, len);
            raw[len] = '\0';
            char numStr[64];
            int j = 0;
            for (int i = 0; i < len && j < 63; i++) {
                if (raw[i] != '_') numStr[j++] = raw[i];
            }
            numStr[j] = '\0';

            bool isF64 = false;
            bool isU32 = false;
            bool isU64 = false;
            bool isI64 = false;

            /* Handle explicit suffixes */
            if (j >= 3 && strcmp(numStr + j - 3, "f64") == 0) {
                isF64 = true;
                j -= 3;
                numStr[j] = '\0';
            } else if (j >= 3 && strcmp(numStr + j - 3, "u32") == 0) {
                isU32 = true;
                j -= 3;
                numStr[j] = '\0';
            } else if (j >= 3 && strcmp(numStr + j - 3, "u64") == 0) {
                isU64 = true;
                j -= 3;
                numStr[j] = '\0';
            } else if (j >= 1 && numStr[j - 1] == 'u') {
                isU64 = true;
                j -= 1;
                numStr[j] = '\0';
            } else if (j >= 3 && strcmp(numStr + j - 3, "i64") == 0) {
                isI64 = true;
                j -= 3;
                numStr[j] = '\0';
            }

            /* Detect float by decimal/exponent */
            for (int i = 0; i < j; i++) {
                if (numStr[i] == '.' || numStr[i] == 'e' || numStr[i] == 'E') {
                    isF64 = true;
                    break;
                }
            }

            if (isF64) {
                double val = strtod(numStr, NULL);
                node->literal.value = F64_VAL(val);
            } else if (isU32) {
                uint32_t val = (uint32_t)strtoul(numStr, NULL, 0);
                node->literal.value = U32_VAL(val);
            } else if (isU64) {
                uint64_t val = strtoull(numStr, NULL, 0);
                node->literal.value = U64_VAL(val);
            } else {
                long long value = strtoll(numStr, NULL, 0);
                if (isI64 || value > INT32_MAX || value < INT32_MIN) {
                    node->literal.value = I64_VAL(value);
                } else {
                    node->literal.value = I32_VAL((int32_t)value);
                }
            }

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
            
            // Check if this is a function call
            if (peekToken().type == TOKEN_LEFT_PAREN) {
                return parseCallExpression(node);
            }
            
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
        case TOKEN_TIME_STAMP: {
            Token next = nextToken();
            if (next.type != TOKEN_LEFT_PAREN) {
                return NULL;
            }
            Token close = nextToken();
            if (close.type != TOKEN_RIGHT_PAREN) {
                return NULL;
            }
            ASTNode* node = new_node();
            node->type = NODE_TIME_STAMP;
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

// Parse function definition: fn name(param1, param2) -> return_type:
static ASTNode* parseFunctionDefinition(void) {
    // Consume 'fn' token
    nextToken();
    
    // Get function name
    Token nameTok = nextToken();
    if (nameTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }
    
    // Copy function name
    int nameLen = nameTok.length;
    char* functionName = arena_alloc(&parserArena, nameLen + 1);
    strncpy(functionName, nameTok.start, nameLen);
    functionName[nameLen] = '\0';
    
    // Parse parameter list
    if (nextToken().type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    
    FunctionParam* params = NULL;
    int paramCount = 0;
    int paramCapacity = 0;
    
    if (peekToken().type != TOKEN_RIGHT_PAREN) {
        while (true) {
            Token paramTok = nextToken();
            if (paramTok.type != TOKEN_IDENTIFIER) {
                return NULL;
            }
            
            // Copy parameter name
            int paramLen = paramTok.length;
            char* paramName = arena_alloc(&parserArena, paramLen + 1);
            strncpy(paramName, paramTok.start, paramLen);
            paramName[paramLen] = '\0';
            
            // Optional type annotation
            ASTNode* paramType = NULL;
            if (peekToken().type == TOKEN_COLON) {
                nextToken(); // consume ':'
                Token typeTok = nextToken();
                if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
                    typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
                    typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
                    typeTok.type != TOKEN_BOOL && typeTok.type != TOKEN_STRING) {
                    return NULL;
                }
                int typeLen = typeTok.length;
                char* typeName = arena_alloc(&parserArena, typeLen + 1);
                strncpy(typeName, typeTok.start, typeLen);
                typeName[typeLen] = '\0';
                paramType = new_node();
                paramType->type = NODE_TYPE;
                paramType->typeAnnotation.name = typeName;
            }
            
            // Add parameter to list
            if (paramCount + 1 > paramCapacity) {
                int newCap = paramCapacity == 0 ? 4 : (paramCapacity * 2);
                FunctionParam* newParams = arena_alloc(&parserArena, sizeof(FunctionParam) * newCap);
                if (paramCapacity > 0) {
                    memcpy(newParams, params, sizeof(FunctionParam) * paramCount);
                }
                params = newParams;
                paramCapacity = newCap;
            }
            params[paramCount].name = paramName;
            params[paramCount].typeAnnotation = paramType;
            paramCount++;
            
            if (peekToken().type != TOKEN_COMMA) break;
            nextToken(); // consume ','
        }
    }
    
    if (nextToken().type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    // Optional return type annotation
    ASTNode* returnType = NULL;
    if (peekToken().type == TOKEN_ARROW) {
        nextToken(); // consume '->'
        Token typeTok = peekToken();
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType();
            if (!returnType) return NULL;
        } else {
            typeTok = nextToken();
            if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
                typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
                typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
                typeTok.type != TOKEN_BOOL && typeTok.type != TOKEN_STRING) {
                return NULL;
            }
            int typeLen = typeTok.length;
            char* typeName = arena_alloc(&parserArena, typeLen + 1);
            strncpy(typeName, typeTok.start, typeLen);
            typeName[typeLen] = '\0';
            returnType = new_node();
            returnType->type = NODE_TYPE;
            returnType->typeAnnotation.name = typeName;
        }
    }
    
    // Expect ':'
    if (nextToken().type != TOKEN_COLON) {
        return NULL;
    }
    
    // Expect newline after colon
    if (nextToken().type != TOKEN_NEWLINE) {
        return NULL;
    }
    
    // Expect indent for function body
    if (nextToken().type != TOKEN_INDENT) {
        return NULL;
    }
    
    ASTNode* body = parseBlock();
    if (!body) {
        return NULL;
    }
    
    // Create function node
    ASTNode* function = new_node();
    function->type = NODE_FUNCTION;
    function->function.name = functionName;
    function->function.params = params;
    function->function.paramCount = paramCount;
    function->function.returnType = returnType;
    function->function.body = body;
    function->location.line = nameTok.line;
    function->location.column = nameTok.column;
    function->dataType = NULL;
    
    return function;
}

// Parse return statement: return [expression]
static ASTNode* parseReturnStatement(void) {
    Token returnTok = nextToken(); // consume 'return'
    
    ASTNode* value = NULL;
    
    // Check if there's a return value
    Token next = peekToken();
    if (next.type != TOKEN_NEWLINE && next.type != TOKEN_EOF && next.type != TOKEN_DEDENT) {
        value = parseExpression();
        if (!value) {
            return NULL;
        }
    }
    
    ASTNode* returnStmt = new_node();
    returnStmt->type = NODE_RETURN;
    returnStmt->returnStmt.value = value;
    returnStmt->location.line = returnTok.line;
    returnStmt->location.column = returnTok.column;
    returnStmt->dataType = NULL;
    
    return returnStmt;
}

// Parse function call: identifier(arg1, arg2, ...)
static ASTNode* parseCallExpression(ASTNode* callee) {
    // Consume '('
    Token openParen = nextToken();
    
    ASTNode** args = NULL;
    int argCount = 0;
    int argCapacity = 0;
    
    if (peekToken().type != TOKEN_RIGHT_PAREN) {
        while (true) {
            ASTNode* arg = parseExpression();
            if (!arg) return NULL;
            
            // Add argument to list
            if (argCount + 1 > argCapacity) {
                int newCap = argCapacity == 0 ? 4 : (argCapacity * 2);
                ASTNode** newArgs = arena_alloc(&parserArena, sizeof(ASTNode*) * newCap);
                if (argCapacity > 0) {
                    memcpy(newArgs, args, sizeof(ASTNode*) * argCount);
                }
                args = newArgs;
                argCapacity = newCap;
            }
            args[argCount++] = arg;
            
            if (peekToken().type != TOKEN_COMMA) break;
            nextToken(); // consume ','
        }
    }
    
    if (nextToken().type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    ASTNode* call = new_node();
    call->type = NODE_CALL;
    call->call.callee = callee;
    call->call.args = args;
    call->call.argCount = argCount;
    call->location.line = openParen.line;
    call->location.column = openParen.column;
    call->dataType = NULL;
    
    return call;
}

// Parse function type: fn(param_types...) -> return_type
static ASTNode* parseFunctionType(void) {
    // Expect 'fn'
    if (nextToken().type != TOKEN_FN) {
        return NULL;
    }
    
    // Expect '('
    if (nextToken().type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    
    // Parse parameter types
    FunctionParam* params = NULL;
    int paramCount = 0;
    int paramCapacity = 0;
    
    if (peekToken().type != TOKEN_RIGHT_PAREN) {
        while (true) {
            // For function types, we only need the type, not the name
            Token typeTok = nextToken();
            if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
                typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
                typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
                typeTok.type != TOKEN_BOOL && typeTok.type != TOKEN_STRING &&
                typeTok.type != TOKEN_FN) {
                return NULL;
            }
            
            // Handle nested function types
            ASTNode* paramType = NULL;
            if (typeTok.type == TOKEN_FN) {
                // TODO: Handle nested function types - for now just treat as identifier
                int typeLen = 2; // "fn"
                char* typeName = arena_alloc(&parserArena, typeLen + 1);
                strcpy(typeName, "fn");
                paramType = new_node();
                paramType->type = NODE_TYPE;
                paramType->typeAnnotation.name = typeName;
            } else {
                int typeLen = typeTok.length;
                char* typeName = arena_alloc(&parserArena, typeLen + 1);
                strncpy(typeName, typeTok.start, typeLen);
                typeName[typeLen] = '\0';
                paramType = new_node();
                paramType->type = NODE_TYPE;
                paramType->typeAnnotation.name = typeName;
            }
            
            // Add parameter to list
            if (paramCount + 1 > paramCapacity) {
                int newCap = paramCapacity == 0 ? 4 : (paramCapacity * 2);
                FunctionParam* newParams = arena_alloc(&parserArena, sizeof(FunctionParam) * newCap);
                if (paramCapacity > 0) {
                    memcpy(newParams, params, sizeof(FunctionParam) * paramCount);
                }
                params = newParams;
                paramCapacity = newCap;
            }
            params[paramCount].name = NULL; // No name for function type parameters
            params[paramCount].typeAnnotation = paramType;
            paramCount++;
            
            if (peekToken().type != TOKEN_COMMA) break;
            nextToken(); // consume ','
        }
    }
    
    // Expect ')'
    if (nextToken().type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    // Parse return type
    ASTNode* returnType = NULL;
    if (peekToken().type == TOKEN_ARROW) {
        nextToken(); // consume '->'
        Token typeTok = peekToken();
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType();
        } else {
            typeTok = nextToken();
            if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
                typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
                typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
                typeTok.type != TOKEN_BOOL && typeTok.type != TOKEN_STRING) {
                return NULL;
            }
            int typeLen = typeTok.length;
            char* typeName = arena_alloc(&parserArena, typeLen + 1);
            strncpy(typeName, typeTok.start, typeLen);
            typeName[typeLen] = '\0';
            returnType = new_node();
            returnType->type = NODE_TYPE;
            returnType->typeAnnotation.name = typeName;
        }
    }
    
    // Create function type node  
    ASTNode* funcType = new_node();
    funcType->type = NODE_FUNCTION; // Reuse NODE_FUNCTION for function types
    funcType->function.name = NULL; // No name for function types
    funcType->function.params = params;
    funcType->function.paramCount = paramCount;
    funcType->function.returnType = returnType;
    funcType->function.body = NULL; // No body for function types
    
    return funcType;
}

void freeAST(ASTNode* node) {
    arena_reset(&parserArena);
    (void)node;
}

