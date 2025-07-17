#include "compiler/parser.h"
#include "public/common.h"
#include "vm/vm.h"
#include "internal/error_reporting.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

// ArenaBlock and Arena are now defined in parser.h
// This eliminates the redefinition error

// Parser recursion depth tracking
#define MAX_RECURSION_DEPTH 1000
#define PARSER_ARENA_SIZE (1 << 16)  // 64KB

#define PREC_MUL_DIV_MOD 4
#define PREC_ADD_SUB 3
#define PREC_CAST 2
#define PREC_COMPARISON 2
#define PREC_AND 1
#define PREC_OR 0
#define PREC_NONE -1

static void arena_init(Arena* a, size_t initial) {
    a->head = malloc(sizeof(ArenaBlock));
    a->head->buffer = malloc(initial);
    a->head->capacity = initial;
    a->head->used = 0;
    a->head->next = NULL;
}

static void* parser_arena_alloc(ParserContext* ctx, size_t size) {
    Arena* a = &ctx->arena;
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

static void parser_arena_reset(ParserContext* ctx) {
    Arena* a = &ctx->arena;
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

static ASTNode* new_node(ParserContext* ctx) { return parser_arena_alloc(ctx, sizeof(ASTNode)); }

static void addStatement(ParserContext* ctx, ASTNode*** list, int* count, int* capacity, ASTNode* stmt) {
    if (*count + 1 > *capacity) {
        int newCap = *capacity == 0 ? 4 : (*capacity * 2);
        ASTNode** newArr = parser_arena_alloc(ctx, sizeof(ASTNode*) * newCap);
        if (*capacity > 0) {
            memcpy(newArr, *list, sizeof(ASTNode*) * (*count));
        }
        *list = newArr;
        *capacity = newCap;
    }
    (*list)[(*count)++] = stmt;
}

// Parser context lifecycle functions
ParserContext* parser_context_create(void) {
    ParserContext* ctx = malloc(sizeof(ParserContext));
    if (!ctx) return NULL;
    
    // Initialize arena
    arena_init(&ctx->arena, PARSER_ARENA_SIZE);
    
    // Initialize state
    ctx->recursion_depth = 0;
    ctx->has_peeked_token = false;
    ctx->has_peeked_token2 = false;
    ctx->max_recursion_depth = MAX_RECURSION_DEPTH;
    
    return ctx;
}

void parser_context_destroy(ParserContext* ctx) {
    if (!ctx) return;
    
    // Free arena blocks
    ArenaBlock* block = ctx->arena.head;
    while (block) {
        ArenaBlock* next = block->next;
        free(block->buffer);
        free(block);
        block = next;
    }
    
    free(ctx);
}

void parser_context_reset(ParserContext* ctx) {
    if (!ctx) return;
    
    parser_arena_reset(ctx);
    ctx->recursion_depth = 0;
    ctx->has_peeked_token = false;
    ctx->has_peeked_token2 = false;
}

// Token lookahead functions now use context

static Token peekToken(ParserContext* ctx) {
    if (!ctx->has_peeked_token) {
        ctx->peeked_token = scan_token();
        ctx->has_peeked_token = true;
    }
    return ctx->peeked_token;
}

static Token peekSecondToken(ParserContext* ctx) {
    if (!ctx->has_peeked_token) {
        ctx->peeked_token = scan_token();
        ctx->has_peeked_token = true;
    }
    if (!ctx->has_peeked_token2) {
        ctx->peeked_token2 = scan_token();
        ctx->has_peeked_token2 = true;
    }
    return ctx->peeked_token2;
}

static Token nextToken(ParserContext* ctx) {
    if (ctx->has_peeked_token) {
        Token result = ctx->peeked_token;
        if (ctx->has_peeked_token2) {
            ctx->peeked_token = ctx->peeked_token2;
            ctx->has_peeked_token2 = false;
        } else {
            ctx->has_peeked_token = false;
        }
        return result;
    }
    return scan_token();
}

// Forward declarations - all now take ParserContext*
static ASTNode* parsePrintStatement(ParserContext* ctx);
static ASTNode* parseExpression(ParserContext* ctx);
static ASTNode* parseBinaryExpression(ParserContext* ctx, int minPrec);
static ASTNode* parsePrimaryExpression(ParserContext* ctx);

// Control flow parsing functions
static ASTNode* parseIfStatement(ParserContext* ctx);
static ASTNode* parseWhileStatement(ParserContext* ctx);
static ASTNode* parseForStatement(ParserContext* ctx);
static ASTNode* parseBreakStatement(ParserContext* ctx);
static ASTNode* parseContinueStatement(ParserContext* ctx);
static ASTNode* parseFunctionDefinition(ParserContext* ctx);
static ASTNode* parseReturnStatement(ParserContext* ctx);

// Primary expression handlers
static ASTNode* parseNumberLiteral(ParserContext* ctx, Token token);

// Number parsing helper functions
static void preprocessNumberToken(const char* tokenStart, int tokenLength, char* numStr, int* processedLength);
static bool detectNumberSuffix(char* numStr, int* length, bool* isF64, bool* isU32, bool* isU64, bool* isI32, bool* isI64, bool* hasExplicitSuffix);
static bool isFloatingPointNumber(const char* numStr, int length);
static Value parseNumberValue(const char* numStr, bool isF64, bool isU32, bool isU64, bool isI32, bool isI64);

static ASTNode* parseStringLiteral(ParserContext* ctx, Token token);
static ASTNode* parseBooleanLiteral(ParserContext* ctx, Token token);
static ASTNode* parseNilLiteral(ParserContext* ctx, Token token);
static ASTNode* parseIdentifierExpression(ParserContext* ctx, Token token);
static ASTNode* parseTimeStampExpression(ParserContext* ctx, Token token);
static ASTNode* parseParenthesizedExpressionToken(ParserContext* ctx, Token token);
static ASTNode* parseVariableDeclaration(ParserContext* ctx, bool isMutable, Token nameToken);
static ASTNode* parseAssignOrVarList(ParserContext* ctx, bool isMutable, Token nameToken);
static ASTNode* parseStatement(ParserContext* ctx);
static ASTNode* parseIfStatement(ParserContext* ctx);
static ASTNode* parseWhileStatement(ParserContext* ctx);
static ASTNode* parseForStatement(ParserContext* ctx);
static ASTNode* parseBreakStatement(ParserContext* ctx);
static ASTNode* parseContinueStatement(ParserContext* ctx);
static ASTNode* parseBlock(ParserContext* ctx);
static ASTNode* parseFunctionDefinition(ParserContext* ctx);
static ASTNode* parseReturnStatement(ParserContext* ctx);
static ASTNode* parseCallExpression(ParserContext* ctx, ASTNode* callee);
static ASTNode* parseFunctionType(ParserContext* ctx);
static ASTNode* parseTypeAnnotation(ParserContext* ctx);

static int getOperatorPrecedence(TokenType type) {
    switch (type) {
        case TOKEN_STAR:
        case TOKEN_SLASH:
        case TOKEN_MODULO:
            return PREC_MUL_DIV_MOD;
        case TOKEN_PLUS:
        case TOKEN_MINUS:
            return PREC_ADD_SUB;
        case TOKEN_AS:
            return PREC_CAST;  // Cast operator has higher precedence than comparison
        case TOKEN_EQUAL_EQUAL:
        case TOKEN_BANG_EQUAL:
        case TOKEN_LESS:
        case TOKEN_GREATER:
        case TOKEN_LESS_EQUAL:
        case TOKEN_GREATER_EQUAL:
            return PREC_COMPARISON;
        case TOKEN_AND:
            return PREC_AND;
        case TOKEN_OR:
            return PREC_OR;
        default:
            return PREC_NONE;
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

// Backward compatibility wrapper
ASTNode* parseSource(const char* source) {
    static ParserContext* global_ctx = NULL;
    if (!global_ctx) {
        global_ctx = parser_context_create();
    }
    return parseSourceWithContext(global_ctx, source);
}

static ASTNode* parseStatement(ParserContext* ctx) {
    Token t = peekToken(ctx);

    if (t.type == TOKEN_PRINT || t.type == TOKEN_PRINT_NO_NL) {
        return parsePrintStatement(ctx);
    }
    if (t.type == TOKEN_APOSTROPHE) {
        nextToken(ctx);
        Token labelTok = nextToken(ctx);
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        if (nextToken(ctx).type != TOKEN_COLON) return NULL;
        int len = labelTok.length;
        char* label = parser_arena_alloc(ctx, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        Token after = peekToken(ctx);
        ASTNode* stmt = NULL;
        if (after.type == TOKEN_WHILE) {
            stmt = parseWhileStatement(ctx);
            if (stmt) stmt->whileStmt.label = label;
        } else if (after.type == TOKEN_FOR) {
            stmt = parseForStatement(ctx);
            if (stmt->type == NODE_FOR_RANGE) stmt->forRange.label = label;
            else if (stmt->type == NODE_FOR_ITER) stmt->forIter.label = label;
        } else {
            return NULL;
        }
        return stmt;
    }
    if (t.type == TOKEN_MUT) {
        nextToken(ctx); // consume TOKEN_MUT
        Token nameTok = nextToken(ctx); // get identifier
        if (nameTok.type != TOKEN_IDENTIFIER) return NULL;
        if (peekToken(ctx).type == TOKEN_COLON) {
            return parseVariableDeclaration(ctx, true, nameTok);
        }
        return parseAssignOrVarList(ctx, true, nameTok);
    }
    if (t.type == TOKEN_LET) {
        // ERROR: 'let' is not supported in Orus
        SrcLocation location = {NULL, t.line, t.column};
        report_compile_error(E1006_INVALID_SYNTAX, location, 
                           "'let' keyword is not supported in Orus");
        return NULL;
    }
    if (t.type == TOKEN_IDENTIFIER) {
        Token second = peekSecondToken(ctx);
        if (second.type == TOKEN_COLON) {
            nextToken(ctx);
            return parseVariableDeclaration(ctx, false, t);
        } else if (second.type == TOKEN_EQUAL) {
            nextToken(ctx);
            return parseAssignOrVarList(ctx, false, t);
        }
    }
    if (t.type == TOKEN_IF) {
        return parseIfStatement(ctx);
    } else if (t.type == TOKEN_WHILE) {
        return parseWhileStatement(ctx);
    } else if (t.type == TOKEN_FOR) {
        return parseForStatement(ctx);
    } else if (t.type == TOKEN_BREAK) {
        return parseBreakStatement(ctx);
    } else if (t.type == TOKEN_CONTINUE) {
        return parseContinueStatement(ctx);
    } else if (t.type == TOKEN_FN) {
        return parseFunctionDefinition(ctx);
    } else if (t.type == TOKEN_RETURN) {
        return parseReturnStatement(ctx);
    } else {
        return parseExpression(ctx);
    }
}

static ASTNode* parsePrintStatement(ParserContext* ctx) {
    // Consume PRINT or PRINT_NO_NL
    Token printTok = nextToken(ctx);
    bool newline = (printTok.type == TOKEN_PRINT);

    // Expect '('
    Token left = nextToken(ctx);
    if (left.type != TOKEN_LEFT_PAREN) return NULL;

    // Gather zero or more comma-separated expressions
    ASTNode** args = NULL;
    int count = 0, capacity = 0;
    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        while (true) {
            ASTNode* expr = parseExpression(ctx);
            if (!expr) return NULL;
            addStatement(ctx, &args, &count, &capacity, expr);
            if (peekToken(ctx).type != TOKEN_COMMA) break;
            nextToken(ctx);  // consume comma
        }
    }

    // Expect ')'
    Token close = nextToken(ctx);
    if (close.type != TOKEN_RIGHT_PAREN) return NULL;

    // Build the NODE_PRINT AST node
    ASTNode* node = new_node(ctx);
    node->type = NODE_PRINT;
    node->print.values = args;
    node->print.count = count;
    node->print.newline = newline;
    node->location.line = printTok.line;
    node->location.column = printTok.column;
    node->dataType = NULL;

    return node;
}

static ASTNode* parseTypeAnnotation(ParserContext* ctx) {
    Token typeTok = nextToken(ctx);
    if (typeTok.type != TOKEN_IDENTIFIER && typeTok.type != TOKEN_INT &&
        typeTok.type != TOKEN_I64 && typeTok.type != TOKEN_U32 &&
        typeTok.type != TOKEN_U64 && typeTok.type != TOKEN_F64 &&
        typeTok.type != TOKEN_BOOL && typeTok.type != TOKEN_STRING) {
        return NULL;
    }

    int tl = typeTok.length;
    char* typeName = parser_arena_alloc(ctx, tl + 1);
    strncpy(typeName, typeTok.start, tl);
    typeName[tl] = '\0';

    ASTNode* typeNode = new_node(ctx);
    typeNode->type = NODE_TYPE;
    typeNode->typeAnnotation.name = typeName;
    typeNode->typeAnnotation.isNullable = false;

    if (peekToken(ctx).type == TOKEN_QUESTION) {
        nextToken(ctx);
        typeNode->typeAnnotation.isNullable = true;
    }

    return typeNode;
}

static ASTNode* parseVariableDeclaration(ParserContext* ctx, bool isMutable, Token nameToken) {

    ASTNode* typeNode = NULL;
    if (peekToken(ctx).type == TOKEN_COLON) {
        nextToken(ctx);
        typeNode = parseTypeAnnotation(ctx);
        if (!typeNode) return NULL;
    }

    Token equalToken = nextToken(ctx);
    if (equalToken.type != TOKEN_EQUAL) {
        return NULL;
    }

    ASTNode* initializer = parseExpression(ctx);
    if (!initializer) {
        return NULL;
    }

    // Check for redundant type annotation with literal suffix
    if (typeNode && initializer->type == NODE_LITERAL) {
        const char* declaredType = typeNode->typeAnnotation.name;
        ValueType literalType = initializer->literal.value.type;
        
        bool isRedundant = false;
        if (initializer->literal.hasExplicitSuffix &&
            ((strcmp(declaredType, "u32") == 0 && literalType == VAL_U32) ||
             (strcmp(declaredType, "u64") == 0 && literalType == VAL_U64) ||
             (strcmp(declaredType, "i64") == 0 && literalType == VAL_I64) ||
             (strcmp(declaredType, "f64") == 0 && literalType == VAL_F64))) {
            isRedundant = true;
        }

        if (isRedundant) {
            printf("Warning: Redundant type annotation at line %d:%d. "
                   "Literal already has type suffix matching declared type '%s'. "
                   "Consider using just 'x = value%s' instead of 'x: %s = value%s'.\n",
                   nameToken.line, nameToken.column, declaredType,
                   declaredType, declaredType, declaredType);
        } else {
            bool mismatch = true;
            if (strcmp(declaredType, "i32") == 0 && literalType == VAL_I32)
                mismatch = false;
            else if (strcmp(declaredType, "i64") == 0 && literalType == VAL_I64)
                mismatch = false;
            else if (strcmp(declaredType, "u32") == 0 && literalType == VAL_U32)
                mismatch = false;
            else if (strcmp(declaredType, "u64") == 0 && literalType == VAL_U64)
                mismatch = false;
            else if (strcmp(declaredType, "f64") == 0 && literalType == VAL_F64)
                mismatch = false;
            else if (strcmp(declaredType, "bool") == 0 && literalType == VAL_BOOL)
                mismatch = false;
            else if (strcmp(declaredType, "string") == 0 && literalType == VAL_STRING)
                mismatch = false;
            // Allow compatible type conversions when annotation overrides
            else if (strcmp(declaredType, "u32") == 0 && literalType == VAL_I32) {
                // Allow i32 -> u32 conversion if value is non-negative
                int32_t value = initializer->literal.value.as.i32;
                if (value >= 0) {
                    mismatch = false;
                    // Convert the literal to u32 type
                    initializer->literal.value.type = VAL_U32;
                    initializer->literal.value.as.u32 = (uint32_t)value;
                }
            }
            else if (strcmp(declaredType, "u64") == 0 && literalType == VAL_I32) {
                // Allow i32 -> u64 conversion if value is non-negative
                int32_t value = initializer->literal.value.as.i32;
                if (value >= 0) {
                    mismatch = false;
                    // Convert the literal to u64 type
                    initializer->literal.value.type = VAL_U64;
                    initializer->literal.value.as.u64 = (uint64_t)value;
                }
            }
            else if (strcmp(declaredType, "i64") == 0 && literalType == VAL_I32) {
                // Allow i32 -> i64 conversion always
                int32_t value = initializer->literal.value.as.i32;
                mismatch = false;
                // Convert the literal to i64 type
                initializer->literal.value.type = VAL_I64;
                initializer->literal.value.as.i64 = (int64_t)value;
            }
            else if (strcmp(declaredType, "f64") == 0 && literalType == VAL_I32) {
                // Allow i32 -> f64 conversion
                int32_t value = initializer->literal.value.as.i32;
                mismatch = false;
                // Convert the literal to f64 type
                initializer->literal.value.type = VAL_F64;
                initializer->literal.value.as.f64 = (double)value;
            }
            else if (literalType == VAL_NIL && typeNode->typeAnnotation.isNullable)
                mismatch = false;

            if (literalType == VAL_NIL && !typeNode->typeAnnotation.isNullable) {
                // Use structured error reporting for nil assignment error
                SrcLocation location = {
                    .file = NULL, // Will be set by error reporting system
                    .line = nameToken.line,
                    .column = nameToken.column
                };
                
                report_compile_error(E2001_TYPE_MISMATCH, location,
                    "nil not allowed for non-nullable type '%s'", declaredType);
                return NULL;
            }

            if (mismatch) {
                // Use structured error reporting instead of fprintf
                SrcLocation location = {
                    .file = NULL, // Will be set by error reporting system
                    .line = nameToken.line,
                    .column = nameToken.column
                };
                
                // Get the actual literal type name for the error
                const char* literalTypeName = "unknown";
                switch (literalType) {
                    case VAL_I32: literalTypeName = "i32"; break;
                    case VAL_I64: literalTypeName = "i64"; break;
                    case VAL_U32: literalTypeName = "u32"; break;
                    case VAL_U64: literalTypeName = "u64"; break;
                    case VAL_F64: literalTypeName = "f64"; break;
                    case VAL_BOOL: literalTypeName = "bool"; break;
                    case VAL_STRING: literalTypeName = "string"; break;
                    case VAL_NIL: literalTypeName = "nil"; break;
                    default: literalTypeName = "unknown"; break;
                }
                
                // Report structured type error
                report_type_error(E2001_TYPE_MISMATCH, location, declaredType, literalTypeName);
                return NULL;
            }
        }
    }

    ASTNode* varNode = new_node(ctx);
    varNode->type = NODE_VAR_DECL;
    varNode->location.line = nameToken.line;
    varNode->location.column = nameToken.column;
    varNode->dataType = NULL;

    int len = nameToken.length;
    char* name = parser_arena_alloc(ctx, len + 1);
    strncpy(name, nameToken.start, len);
    name[len] = '\0';

    varNode->varDecl.name = name;
    varNode->varDecl.isPublic = false;
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = typeNode;
    varNode->varDecl.isConst = false;
    varNode->varDecl.isMutable = isMutable;

    // For multiple variable declarations separated by commas,
    // only parse the first one and let the main parser handle the rest
    return varNode;
}

static ASTNode* parseAssignOrVarList(ParserContext* ctx, bool isMutable, Token nameToken) {
    Token opToken = nextToken(ctx);
    
    // Handle compound assignments (+=, -=, *=, /=, %=)
    if (opToken.type == TOKEN_PLUS_EQUAL || opToken.type == TOKEN_MINUS_EQUAL || 
        opToken.type == TOKEN_STAR_EQUAL || opToken.type == TOKEN_SLASH_EQUAL ||
        opToken.type == TOKEN_MODULO_EQUAL) {
        
        // Compound assignments are only valid for existing variables, not declarations
        if (isMutable) {
            // This is actually a variable declaration with mut, not a compound assignment
            return NULL;
        }
        
        ASTNode* right = parseExpression(ctx);
        if (!right) return NULL;
        
        // Create equivalent binary operation: x += y becomes x = x + y
        ASTNode* identifierLeft = new_node(ctx);
        identifierLeft->type = NODE_IDENTIFIER;
        int len = nameToken.length;
        char* name = parser_arena_alloc(ctx, len + 1);
        strncpy(name, nameToken.start, len);
        name[len] = '\0';
        identifierLeft->identifier.name = name;
        identifierLeft->location.line = nameToken.line;
        identifierLeft->location.column = nameToken.column;
        identifierLeft->dataType = NULL;
        
        ASTNode* binaryOp = new_node(ctx);
        binaryOp->type = NODE_BINARY;
        binaryOp->binary.left = identifierLeft;
        binaryOp->binary.right = right;
        binaryOp->location.line = opToken.line;
        binaryOp->location.column = opToken.column;
        binaryOp->dataType = NULL;
        
        // Map compound operator to binary operator
        switch (opToken.type) {
            case TOKEN_PLUS_EQUAL:
                binaryOp->binary.op = "+";
                break;
            case TOKEN_MINUS_EQUAL:
                binaryOp->binary.op = "-";
                break;
            case TOKEN_STAR_EQUAL:
                binaryOp->binary.op = "*";
                break;
            case TOKEN_SLASH_EQUAL:
                binaryOp->binary.op = "/";
                break;
            case TOKEN_MODULO_EQUAL:
                binaryOp->binary.op = "%";
                break;
            default:
                return NULL;
        }
        
        // Create assignment with the binary operation as the value
        ASTNode* assignNode = new_node(ctx);
        assignNode->type = NODE_ASSIGN;
        assignNode->assign.name = name; // Reuse the allocated name
        assignNode->assign.value = binaryOp;
        assignNode->location.line = nameToken.line;
        assignNode->location.column = nameToken.column;
        assignNode->dataType = NULL;
        
        return assignNode;
    }
    
    // Regular assignment
    if (opToken.type != TOKEN_EQUAL) return NULL;
    ASTNode* initializer = parseExpression(ctx);
    if (!initializer) return NULL;

    // For multiple variable declarations separated by commas,
    // only parse the first one and let the main parser handle the rest
    if (peekToken(ctx).type != TOKEN_COMMA && !isMutable) {
        // Regular assignment
        ASTNode* node = new_node(ctx);
        node->type = NODE_ASSIGN;
        int len = nameToken.length;
        char* name = parser_arena_alloc(ctx, len + 1);
        strncpy(name, nameToken.start, len);
        name[len] = '\0';
        node->assign.name = name;
        node->assign.value = initializer;
        node->location.line = nameToken.line;
        node->location.column = nameToken.column;
        node->dataType = NULL;
        return node;
    }

    // Create a single variable declaration for the first variable
    ASTNode* varNode = new_node(ctx);
    varNode->type = NODE_VAR_DECL;
    varNode->location.line = nameToken.line;
    varNode->location.column = nameToken.column;
    varNode->dataType = NULL;

    int len = nameToken.length;
    char* name = parser_arena_alloc(ctx, len + 1);
    strncpy(name, nameToken.start, len);
    name[len] = '\0';

    varNode->varDecl.name = name;
    varNode->varDecl.isPublic = false;
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = NULL;
    varNode->varDecl.isConst = false;
    varNode->varDecl.isMutable = isMutable;

    // Don't consume the comma - let the main parser handle subsequent declarations
    return varNode;
}

static ASTNode* parseBlock(ParserContext* ctx) {
    extern VM vm;
    if (vm.devMode) {
        fprintf(stderr, "Debug: Entering parseBlock\n");
    }
    
    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;

    while (true) {
        Token t = peekToken(ctx);
        if (vm.devMode) {
            fprintf(stderr, "Debug: parseBlock - Current token type: %d\n", t.type);
        }
        if (t.type == TOKEN_DEDENT || t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }
        if (t.type == TOKEN_SEMICOLON) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1007_SEMICOLON_NOT_ALLOWED, location, 
                               "found ';' here");
            return NULL;
        }
        ASTNode* stmt = parseStatement(ctx);
        if (!stmt) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: parseBlock failed to parse statement\n");
            }
            return NULL;
        }
        addStatement(ctx, &statements, &count, &capacity, stmt);
        t = peekToken(ctx);
        if (t.type == TOKEN_NEWLINE) {
            nextToken(ctx);
        } else if (t.type == TOKEN_SEMICOLON) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1007_SEMICOLON_NOT_ALLOWED, location, 
                               "found ';' here");
            return NULL;
        }
    }
    Token dedent = nextToken(ctx);
    if (dedent.type != TOKEN_DEDENT) return NULL;

    ASTNode* block = new_node(ctx);
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = count;
    block->location.line = dedent.line;
    block->location.column = dedent.column;
    block->dataType = NULL;
    return block;
}

static ASTNode* parseIfStatement(ParserContext* ctx) {
    Token ifTok = nextToken(ctx);
    if (ifTok.type != TOKEN_IF && ifTok.type != TOKEN_ELIF) return NULL;

    ASTNode* condition = parseExpression(ctx);
    if (!condition) return NULL;

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) return NULL;
    if (nextToken(ctx).type != TOKEN_NEWLINE) return NULL;
    if (nextToken(ctx).type != TOKEN_INDENT) return NULL;

    ASTNode* thenBranch = parseBlock(ctx);
    if (!thenBranch) return NULL;

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    ASTNode* elseBranch = NULL;
    Token next = peekToken(ctx);
    if (next.type == TOKEN_ELIF) {
        elseBranch = parseIfStatement(ctx);
    } else if (next.type == TOKEN_ELSE) {
        nextToken(ctx);
        if (nextToken(ctx).type != TOKEN_COLON) return NULL;
        if (nextToken(ctx).type != TOKEN_NEWLINE) return NULL;
        if (nextToken(ctx).type != TOKEN_INDENT) return NULL;
        elseBranch = parseBlock(ctx);
        if (!elseBranch) return NULL;
        if (peekToken(ctx).type == TOKEN_NEWLINE) nextToken(ctx);
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_IF;
    node->ifStmt.condition = condition;
    node->ifStmt.thenBranch = thenBranch;
    node->ifStmt.elseBranch = elseBranch;
    node->location.line = ifTok.line;
    node->location.column = ifTok.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseWhileStatement(ParserContext* ctx) {
    Token whileTok = nextToken(ctx);
    if (whileTok.type != TOKEN_WHILE) return NULL;

    ASTNode* condition = parseExpression(ctx);
    if (!condition) return NULL;

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) return NULL;
    if (nextToken(ctx).type != TOKEN_NEWLINE) return NULL;
    if (nextToken(ctx).type != TOKEN_INDENT) return NULL;

    ASTNode* body = parseBlock(ctx);
    if (!body) return NULL;
    if (peekToken(ctx).type == TOKEN_NEWLINE) nextToken(ctx);

    ASTNode* node = new_node(ctx);
    node->type = NODE_WHILE;
    node->whileStmt.condition = condition;
    node->whileStmt.body = body;
    node->whileStmt.label = NULL;
    node->location.line = whileTok.line;
    node->location.column = whileTok.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseForStatement(ParserContext* ctx) {
    extern VM vm;
    if (vm.devMode) {
        fprintf(stderr, "Debug: Entering parseForStatement\n");
    }
    
    Token forTok = nextToken(ctx);
    if (forTok.type != TOKEN_FOR) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_FOR, got %d\n", forTok.type);
        }
        return NULL;
    }

    Token nameTok = nextToken(ctx);
    if (nameTok.type != TOKEN_IDENTIFIER) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IDENTIFIER after 'for', got %d\n", nameTok.type);
        }
        return NULL;
    }

    Token inTok = nextToken(ctx);
    if (inTok.type != TOKEN_IN) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IN after identifier, got %d\n", inTok.type);
        }
        return NULL;
    }

    ASTNode* first = parseExpression(ctx);
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
    if (peekToken(ctx).type == TOKEN_DOT_DOT) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Found TOKEN_DOT_DOT, parsing as range\n");
        }
        isRange = true;
        nextToken(ctx);
        if (peekToken(ctx).type == TOKEN_EQUAL) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Found TOKEN_EQUAL, marking as inclusive range\n");
            }
            nextToken(ctx);
            inclusive = true;
        }
        if (vm.devMode) {
            Token peekEnd = peekToken(ctx);
            fprintf(stderr, "Debug: About to parse end expression, next token type: %d\n", peekEnd.type);
            if (peekEnd.type == 44 || peekEnd.type == 38) {  // TOKEN_NUMBER-ish
                fprintf(stderr, "Debug: Token text: '%.*s'\n", peekEnd.length, peekEnd.start);
            }
        }
        end = parseExpression(ctx);
        if (!end) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Failed to parse end expression in range\n");
            }
            return NULL;
        }
        if (vm.devMode) {
            fprintf(stderr, "Debug: Successfully parsed end expression\n");
            Token afterEnd = peekToken(ctx);
            fprintf(stderr, "Debug: After parsing end expression, next token type: %d\n", afterEnd.type);
            if (afterEnd.type == 44 || afterEnd.type == 38) {  // TOKEN_NUMBER-ish
                fprintf(stderr, "Debug: Token text: '%.*s'\n", afterEnd.length, afterEnd.start);
            }
        }
        if (peekToken(ctx).type == TOKEN_DOT_DOT) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: Found second TOKEN_DOT_DOT, parsing step\n");
            }
            nextToken(ctx);
            step = parseExpression(ctx);
            if (!step) {
                if (vm.devMode) {
                    fprintf(stderr, "Debug: Failed to parse step expression in range\n");
                }
                return NULL;
            }
        }
    }

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_COLON after range, got %d\n", colon.type);
        }
        return NULL;
    }
    
    Token newline = nextToken(ctx);
    if (newline.type != TOKEN_NEWLINE) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_NEWLINE after colon, got %d\n", newline.type);
        }
        return NULL;
    }
    
    Token indent = nextToken(ctx);
    if (indent.type != TOKEN_INDENT) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_INDENT after newline, got %d\n", indent.type);
        }
        return NULL;
    }

    ASTNode* body = parseBlock(ctx);
    if (!body) {
        if (vm.devMode) {
            fprintf(stderr, "Debug: Failed to parse body block in for loop\n");
        }
        return NULL;
    }
    if (peekToken(ctx).type == TOKEN_NEWLINE) nextToken(ctx);

    int len = nameTok.length;
    char* name = parser_arena_alloc(ctx, len + 1);
    strncpy(name, nameTok.start, len);
    name[len] = '\0';

    ASTNode* node = new_node(ctx);
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

static ASTNode* parseBreakStatement(ParserContext* ctx) {
    Token breakToken = nextToken(ctx);
    if (breakToken.type != TOKEN_BREAK) {
        return NULL;
    }
    
    ASTNode* node = new_node(ctx);
    node->type = NODE_BREAK;
    node->location.line = breakToken.line;
    node->location.column = breakToken.column;
    node->dataType = NULL;
    node->breakStmt.label = NULL;
    if (peekToken(ctx).type == TOKEN_APOSTROPHE) {
        nextToken(ctx);
        Token labelTok = nextToken(ctx);
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        int len = labelTok.length;
        char* label = parser_arena_alloc(ctx, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        node->breakStmt.label = label;
    }
    return node;
}

static ASTNode* parseContinueStatement(ParserContext* ctx) {
    Token continueToken = nextToken(ctx);
    if (continueToken.type != TOKEN_CONTINUE) {
        return NULL;
    }
    
    ASTNode* node = new_node(ctx);
    node->type = NODE_CONTINUE;
    node->location.line = continueToken.line;
    node->location.column = continueToken.column;
    node->dataType = NULL;
    node->continueStmt.label = NULL;
    if (peekToken(ctx).type == TOKEN_APOSTROPHE) {
        nextToken(ctx);
        Token labelTok = nextToken(ctx);
        if (labelTok.type != TOKEN_IDENTIFIER) return NULL;
        int len = labelTok.length;
        char* label = parser_arena_alloc(ctx, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        node->continueStmt.label = label;
    }
    return node;
}

static ASTNode* parseAssignment(ParserContext* ctx);

static ASTNode* parseExpression(ParserContext* ctx) { return parseAssignment(ctx); }

// Parse inline conditional expressions using Python-like syntax:
// expr if cond [elif cond]* else expr
static ASTNode* parseInlineIf(ParserContext* ctx, ASTNode* expr) {
    if (peekToken(ctx).type != TOKEN_IF) return expr;
    nextToken(ctx);

    ASTNode* condition = parseExpression(ctx);
    if (!condition) return NULL;

    ASTNode* root = new_node(ctx);
    root->type = NODE_IF;
    root->ifStmt.condition = condition;
    root->ifStmt.thenBranch = expr;
    root->ifStmt.elseBranch = NULL;
    root->location = expr->location;
    root->dataType = NULL;

    ASTNode* current = root;
    while (peekToken(ctx).type == TOKEN_ELIF) {
        nextToken(ctx);
        ASTNode* elifCond = parseExpression(ctx);
        if (!elifCond) return NULL;
        ASTNode* newIf = new_node(ctx);
        newIf->type = NODE_IF;
        newIf->ifStmt.condition = elifCond;
        newIf->ifStmt.thenBranch = expr;
        newIf->ifStmt.elseBranch = NULL;
        newIf->location = expr->location;
        newIf->dataType = NULL;
        current->ifStmt.elseBranch = newIf;
        current = newIf;
    }

    if (peekToken(ctx).type == TOKEN_ELSE) {
        nextToken(ctx);
        ASTNode* elseExpr = parseExpression(ctx);
        if (!elseExpr) return NULL;
        current->ifStmt.elseBranch = elseExpr;
    }

    return root;
}

static ASTNode* parseUnaryExpression(ParserContext* ctx) {
    Token t = peekToken(ctx);
    if (t.type == TOKEN_MINUS || t.type == TOKEN_NOT || t.type == TOKEN_BIT_NOT) {
        // Check recursion depth for unary expressions too
        if (ctx->recursion_depth >= ctx->max_recursion_depth) {
            Token t = peekToken(ctx);
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, location, 
                               "expression nesting exceeds maximum depth of %d", ctx->max_recursion_depth);
            return NULL;
        }
        
        nextToken(ctx);
        ctx->recursion_depth++;
        ASTNode* operand = parseUnaryExpression(ctx);
        if (!operand) {
            ctx->recursion_depth--;
            return NULL;
        }
        ASTNode* node = new_node(ctx);
        node->type = NODE_UNARY;
        if (t.type == TOKEN_MINUS) node->unary.op = "-";
        else if (t.type == TOKEN_NOT) node->unary.op = "not";
        else node->unary.op = "~";
        node->unary.operand = operand;
        node->location.line = t.line;
        node->location.column = t.column;
        node->dataType = NULL;
        ctx->recursion_depth--;
        return node;
    }
    return parsePrimaryExpression(ctx);
}

static ASTNode* parseTernary(ParserContext* ctx, ASTNode* condition) {
    if (peekToken(ctx).type != TOKEN_QUESTION) return condition;
    nextToken(ctx);
    ASTNode* trueExpr = parseExpression(ctx);
    if (!trueExpr) return NULL;
    if (nextToken(ctx).type != TOKEN_COLON) return NULL;
    ASTNode* falseExpr = parseExpression(ctx);
    if (!falseExpr) return NULL;
    ASTNode* node = new_node(ctx);
    node->type = NODE_TERNARY;
    node->ternary.condition = condition;
    node->ternary.trueExpr = trueExpr;
    node->ternary.falseExpr = falseExpr;
    node->location = condition->location;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseAssignment(ParserContext* ctx) {
    ASTNode* left = parseBinaryExpression(ctx, 0);
    if (!left) return NULL;

    TokenType t = peekToken(ctx).type;
    if (t == TOKEN_EQUAL || t == TOKEN_PLUS_EQUAL || t == TOKEN_MINUS_EQUAL ||
        t == TOKEN_STAR_EQUAL || t == TOKEN_SLASH_EQUAL) {
        nextToken(ctx);
        ASTNode* value = NULL;

        if (t == TOKEN_EQUAL) {
            // Use full expression parsing so constructs like
            // x = cond ? a : b are handled correctly
            value = parseAssignment(ctx);
        } else {
            ASTNode* right = parseAssignment(ctx);
            if (!right) return NULL;
            if (left->type != NODE_IDENTIFIER) return NULL;
            ASTNode* binary = new_node(ctx);
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
        ASTNode* node = new_node(ctx);
        node->type = NODE_ASSIGN;
        node->assign.name = left->identifier.name;
        node->assign.value = value;
        node->location = left->location;
        node->dataType = NULL;
        return node;
    }
    ASTNode* expr = parseTernary(ctx, left);
    if (!expr) return NULL;
    return parseInlineIf(ctx, expr);
}

static ASTNode* parseBinaryExpression(ParserContext* ctx, int minPrec) {
    // Check recursion depth
    if (ctx->recursion_depth >= ctx->max_recursion_depth) {
        Token t = peekToken(ctx);
        SrcLocation location = {NULL, t.line, t.column};
        report_compile_error(E1009_EXPRESSION_TOO_COMPLEX, location, 
                           "expression nesting exceeds maximum depth of %d", ctx->max_recursion_depth);
        return NULL;
    }
    
    ctx->recursion_depth++;
    ASTNode* left = parseUnaryExpression(ctx);
    if (!left) {
        ctx->recursion_depth--;
        return NULL;
    }

    while (true) {
        Token operator = peekToken(ctx);
        int prec = getOperatorPrecedence(operator.type);

        if (prec < minPrec || operator.type == TOKEN_EOF) {
            break;
        }

        nextToken(ctx);

        // Handle 'as' operator specially for type casting
        if (operator.type == TOKEN_AS) {
            // Check for chained casts - reject direct chains but allow parenthesized chains
            if (left->type == NODE_CAST && !left->cast.parenthesized) {
                // This is a direct chained cast like "a as bool as string"
                // Reject this as ambiguous - user should use parentheses or intermediate variables
                fprintf(stderr, "Error: Chained type casts are not allowed at line %d:%d. "
                       "Use parentheses like '((a as type1) as type2)' or an intermediate variable for clarity.\n",
                       operator.line, operator.column);
                ctx->recursion_depth--;
                return NULL;
            }
            
            // Parse the target type
            Token typeToken = nextToken(ctx);
            if (typeToken.type != TOKEN_IDENTIFIER && typeToken.type != TOKEN_INT &&
                typeToken.type != TOKEN_I64 && typeToken.type != TOKEN_U32 &&
                typeToken.type != TOKEN_U64 && typeToken.type != TOKEN_F64 &&
                typeToken.type != TOKEN_BOOL && typeToken.type != TOKEN_STRING) {
                ctx->recursion_depth--;
                return NULL;
            }

            // Create a proper null-terminated string for the type name
            size_t typeNameLen = typeToken.length;
            char* typeName = parser_arena_alloc(ctx, typeNameLen + 1);
            memcpy(typeName, typeToken.start, typeNameLen);
            typeName[typeNameLen] = '\0';

            ASTNode* targetType = new_node(ctx);
            targetType->type = NODE_TYPE;
            targetType->typeAnnotation.name = typeName;
            targetType->location.line = typeToken.line;
            targetType->location.column = typeToken.column;

            ASTNode* castNode = new_node(ctx);
            castNode->type = NODE_CAST;
            castNode->cast.expression = left;
            castNode->cast.targetType = targetType;
            castNode->cast.parenthesized = false; // Default to false, will be set by parseParenthesizedExpression
            castNode->location.line = operator.line;
            castNode->location.column = operator.column;
            castNode->dataType = NULL;

            left = castNode;
            continue;
        }

        ASTNode* right = parseBinaryExpression(ctx, prec + 1);
        if (!right) {
            ctx->recursion_depth--;
            return NULL;
        }

        ASTNode* binaryNode = new_node(ctx);
        binaryNode->type = NODE_BINARY;
        binaryNode->binary.left = left;
        binaryNode->binary.right = right;
        binaryNode->binary.op = (char*)getOperatorString(operator.type);
        binaryNode->location.line = operator.line;
        binaryNode->location.column = operator.column;
        binaryNode->dataType = NULL;

        left = binaryNode;
    }

    ctx->recursion_depth--;
    return left;
}

// Primary expression handlers

// Number parsing helper functions implementation
static void preprocessNumberToken(const char* tokenStart, int tokenLength, char* numStr, int* processedLength) {
    char raw[64];
    int len = tokenLength < 63 ? tokenLength : 63;
    strncpy(raw, tokenStart, len);
    raw[len] = '\0';
    
    int j = 0;
    for (int i = 0; i < len && j < 63; i++) {
        if (raw[i] != '_') numStr[j++] = raw[i];
    }
    numStr[j] = '\0';
    *processedLength = j;
}

static bool detectNumberSuffix(char* numStr, int* length, bool* isF64, bool* isU32, bool* isU64, bool* isI32, bool* isI64, bool* hasExplicitSuffix) {
    int j = *length;
    
    *isF64 = *isU32 = *isU64 = *isI32 = *isI64 = *hasExplicitSuffix = false;
    
    if (j >= 3 && strcmp(numStr + j - 3, "f64") == 0) {
        *isF64 = true;
        *hasExplicitSuffix = true;
        j -= 3;
        numStr[j] = '\0';
    } else if (j >= 3 && strcmp(numStr + j - 3, "u32") == 0) {
        *isU32 = true;
        *hasExplicitSuffix = true;
        j -= 3;
        numStr[j] = '\0';
    } else if (j >= 3 && strcmp(numStr + j - 3, "u64") == 0) {
        *isU64 = true;
        *hasExplicitSuffix = true;
        j -= 3;
        numStr[j] = '\0';
    } else if (j >= 3 && strcmp(numStr + j - 3, "i32") == 0) {
        *isI32 = true;
        *hasExplicitSuffix = true;
        j -= 3;
        numStr[j] = '\0';
    } else if (j >= 3 && strcmp(numStr + j - 3, "i64") == 0) {
        *isI64 = true;
        *hasExplicitSuffix = true;
        j -= 3;
        numStr[j] = '\0';
    } else if (j >= 1 && numStr[j - 1] == 'u') {
        *isU64 = true;
        *hasExplicitSuffix = true;
        j -= 1;
        numStr[j] = '\0';
    }
    
    *length = j;
    return *hasExplicitSuffix;
}

static bool isFloatingPointNumber(const char* numStr, int length) {
    for (int i = 0; i < length; i++) {
        if (numStr[i] == '.' || numStr[i] == 'e' || numStr[i] == 'E') {
            return true;
        }
    }
    return false;
}

static Value parseNumberValue(const char* numStr, bool isF64, bool isU32, bool isU64, bool isI32, bool isI64) {
    if (isF64) {
        double val = strtod(numStr, NULL);
        return F64_VAL(val);
    } else if (isU32) {
        uint32_t val = (uint32_t)strtoul(numStr, NULL, 0);
        return U32_VAL(val);
    } else if (isU64) {
        uint64_t val = strtoull(numStr, NULL, 0);
        return U64_VAL(val);
    } else if (isI32) {
        int32_t val = (int32_t)strtol(numStr, NULL, 0);
        return I32_VAL(val);
    } else {
        long long value = strtoll(numStr, NULL, 0);
        if (isI64 || value > INT32_MAX || value < INT32_MIN) {
            return I64_VAL(value);
        } else {
            return I32_VAL((int32_t)value);
        }
    }
}

static ASTNode* parseNumberLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;

    // Preprocess token (remove underscores)
    char numStr[64];
    int length;
    preprocessNumberToken(token.start, token.length, numStr, &length);

    // Detect and handle type suffixes
    bool isF64, isU32, isU64, isI32, isI64, hasExplicitSuffix;
    detectNumberSuffix(numStr, &length, &isF64, &isU32, &isU64, &isI32, &isI64, &hasExplicitSuffix);

    // Check for floating point notation if no explicit type suffix
    if (!hasExplicitSuffix && isFloatingPointNumber(numStr, length)) {
        isF64 = true;
    }

    // Parse the numeric value
    node->literal.value = parseNumberValue(numStr, isF64, isU32, isU64, isI32, isI64);
    node->literal.hasExplicitSuffix = hasExplicitSuffix;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseStringLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;
    
    // Process escape sequences in string content
    int rawLen = token.length - 2; // Remove quotes
    const char* raw = token.start + 1; // Skip opening quote
    
    // Allocate buffer for processed content (may be smaller due to escape sequences)
    char* content = parser_arena_alloc(ctx, rawLen + 1);
    int processedLen = 0;
    
    for (int i = 0; i < rawLen; i++) {
        if (raw[i] == '\\' && i + 1 < rawLen) {
            // Process escape sequence
            switch (raw[i + 1]) {
                case 'n':
                    content[processedLen++] = '\n';
                    i++; // Skip the next character
                    break;
                case 't':
                    content[processedLen++] = '\t';
                    i++; // Skip the next character
                    break;
                case '\\':
                    content[processedLen++] = '\\';
                    i++; // Skip the next character
                    break;
                case '"':
                    content[processedLen++] = '"';
                    i++; // Skip the next character
                    break;
                case 'r':
                    content[processedLen++] = '\r';
                    i++; // Skip the next character
                    break;
                case '0':
                    content[processedLen++] = '\0';
                    i++; // Skip the next character
                    break;
                default:
                    // Invalid escape sequence - just copy the backslash and character
                    content[processedLen++] = raw[i];
                    break;
            }
        } else {
            // Regular character
            content[processedLen++] = raw[i];
        }
    }
    
    content[processedLen] = '\0';
    ObjString* s = allocateString(content, processedLen);
    node->literal.value.type = VAL_STRING;
    node->literal.value.as.obj = (Obj*)s;
    node->literal.hasExplicitSuffix = false;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseBooleanLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;
    node->literal.value = BOOL_VAL(token.type == TOKEN_TRUE);
    node->literal.hasExplicitSuffix = false;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseNilLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;
    node->literal.value = NIL_VAL;
    node->literal.hasExplicitSuffix = false;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseIdentifierExpression(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_IDENTIFIER;
    int len = token.length;
    char* name = parser_arena_alloc(ctx, len + 1);
    strncpy(name, token.start, len);
    name[len] = '\0';
    node->identifier.name = name;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    
    // Check if this is a function call
    if (peekToken(ctx).type == TOKEN_LEFT_PAREN) {
        return parseCallExpression(ctx, node);
    }
    
    return node;
}


static ASTNode* parseTimeStampExpression(ParserContext* ctx, Token token) {
    Token next = nextToken(ctx);
    if (next.type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    Token close = nextToken(ctx);
    if (close.type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    ASTNode* node = new_node(ctx);
    node->type = NODE_TIME_STAMP;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseParenthesizedExpressionToken(ParserContext* ctx, Token token) {
    (void)token; // Unused parameter
    ASTNode* expr = parseExpression(ctx);
    if (!expr) return NULL;
    
    Token rightParen = nextToken(ctx);
    if (rightParen.type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    // Mark cast nodes as parenthesized to allow chained casts with explicit parentheses
    if (expr->type == NODE_CAST) {
        expr->cast.parenthesized = true;
    }
    
    return expr;
}

static ASTNode* parsePrimaryExpression(ParserContext* ctx) {
    Token token = nextToken(ctx);

    switch (token.type) {
        case TOKEN_NUMBER:
            return parseNumberLiteral(ctx, token);
        case TOKEN_STRING:
            return parseStringLiteral(ctx, token);
        case TOKEN_TRUE:
        case TOKEN_FALSE:
            return parseBooleanLiteral(ctx, token);
        case TOKEN_NIL:
            return parseNilLiteral(ctx, token);
        case TOKEN_IDENTIFIER:
            return parseIdentifierExpression(ctx, token);
        case TOKEN_TIME_STAMP:
            return parseTimeStampExpression(ctx, token);
        case TOKEN_LEFT_PAREN:
            return parseParenthesizedExpressionToken(ctx, token);
        default:
            return NULL;
    }
}
static ASTNode* parseFunctionDefinition(ParserContext* ctx) {
    // Consume 'fn' token
    nextToken(ctx);
    
    // Get function name
    Token nameTok = nextToken(ctx);
    if (nameTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }
    
    // Copy function name
    int nameLen = nameTok.length;
    char* functionName = parser_arena_alloc(ctx, nameLen + 1);
    strncpy(functionName, nameTok.start, nameLen);
    functionName[nameLen] = '\0';
    
    // Parse parameter list
    if (nextToken(ctx).type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    
    FunctionParam* params = NULL;
    int paramCount = 0;
    int paramCapacity = 0;
    
    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        while (true) {
            Token paramTok = nextToken(ctx);
            if (paramTok.type != TOKEN_IDENTIFIER) {
                return NULL;
            }
            
            // Copy parameter name
            int paramLen = paramTok.length;
            char* paramName = parser_arena_alloc(ctx, paramLen + 1);
            strncpy(paramName, paramTok.start, paramLen);
            paramName[paramLen] = '\0';
            
            // Optional type annotation
            ASTNode* paramType = NULL;
            if (peekToken(ctx).type == TOKEN_COLON) {
                nextToken(ctx);
                paramType = parseTypeAnnotation(ctx);
                if (!paramType) return NULL;
            }
            
            // Add parameter to list
            if (paramCount + 1 > paramCapacity) {
                int newCap = paramCapacity == 0 ? 4 : (paramCapacity * 2);
                FunctionParam* newParams = parser_arena_alloc(ctx, sizeof(FunctionParam) * newCap);
                if (paramCapacity > 0) {
                    memcpy(newParams, params, sizeof(FunctionParam) * paramCount);
                }
                params = newParams;
                paramCapacity = newCap;
            }
            params[paramCount].name = paramName;
            params[paramCount].typeAnnotation = paramType;
            paramCount++;
            
            if (peekToken(ctx).type != TOKEN_COMMA) break;
            nextToken(ctx); // consume ','
        }
    }
    
    if (nextToken(ctx).type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    // Optional return type annotation
    ASTNode* returnType = NULL;
    if (peekToken(ctx).type == TOKEN_ARROW) {
        nextToken(ctx); // consume '->'
        Token typeTok = peekToken(ctx);
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType(ctx);
            if (!returnType) return NULL;
        } else {
            returnType = parseTypeAnnotation(ctx);
            if (!returnType) return NULL;
        }
    }
    
    // Expect ':'
    if (nextToken(ctx).type != TOKEN_COLON) {
        return NULL;
    }
    
    // Expect newline after colon
    if (nextToken(ctx).type != TOKEN_NEWLINE) {
        return NULL;
    }
    
    // Expect indent for function body
    if (nextToken(ctx).type != TOKEN_INDENT) {
        return NULL;
    }
    
    ASTNode* body = parseBlock(ctx);
    if (!body) {
        return NULL;
    }
    
    // Create function node
    ASTNode* function = new_node(ctx);
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
static ASTNode* parseReturnStatement(ParserContext* ctx) {
    Token returnTok = nextToken(ctx); // consume 'return'
    
    ASTNode* value = NULL;
    
    // Check if there's a return value
    Token next = peekToken(ctx);
    if (next.type != TOKEN_NEWLINE && next.type != TOKEN_EOF && next.type != TOKEN_DEDENT) {
        value = parseExpression(ctx);
        if (!value) {
            return NULL;
        }
    }
    
    ASTNode* returnStmt = new_node(ctx);
    returnStmt->type = NODE_RETURN;
    returnStmt->returnStmt.value = value;
    returnStmt->location.line = returnTok.line;
    returnStmt->location.column = returnTok.column;
    returnStmt->dataType = NULL;
    
    return returnStmt;
}

// Parse function call: identifier(arg1, arg2, ...)
static ASTNode* parseCallExpression(ParserContext* ctx, ASTNode* callee) {
    // Consume '('
    Token openParen = nextToken(ctx);
    
    ASTNode** args = NULL;
    int argCount = 0;
    int argCapacity = 0;
    
    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        while (true) {
            ASTNode* arg = parseExpression(ctx);
            if (!arg) return NULL;
            
            // Add argument to list
            if (argCount + 1 > argCapacity) {
                int newCap = argCapacity == 0 ? 4 : (argCapacity * 2);
                ASTNode** newArgs = parser_arena_alloc(ctx, sizeof(ASTNode*) * newCap);
                if (argCapacity > 0) {
                    memcpy(newArgs, args, sizeof(ASTNode*) * argCount);
                }
                args = newArgs;
                argCapacity = newCap;
            }
            args[argCount++] = arg;
            
            if (peekToken(ctx).type != TOKEN_COMMA) break;
            nextToken(ctx); // consume ','
        }
    }
    
    if (nextToken(ctx).type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    ASTNode* call = new_node(ctx);
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
static ASTNode* parseFunctionType(ParserContext* ctx) {
    // Expect 'fn'
    if (nextToken(ctx).type != TOKEN_FN) {
        return NULL;
    }
    
    // Expect '('
    if (nextToken(ctx).type != TOKEN_LEFT_PAREN) {
        return NULL;
    }
    
    // Parse parameter types
    FunctionParam* params = NULL;
    int paramCount = 0;
    int paramCapacity = 0;
    
    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        while (true) {
            // For function types, we only need the type, not the name
            Token typeTok = nextToken(ctx);
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
                char* typeName = parser_arena_alloc(ctx, typeLen + 1);
                strcpy(typeName, "fn");
                paramType = new_node(ctx);
                paramType->type = NODE_TYPE;
                paramType->typeAnnotation.name = typeName;
            } else {
                int typeLen = typeTok.length;
                char* typeName = parser_arena_alloc(ctx, typeLen + 1);
                strncpy(typeName, typeTok.start, typeLen);
                typeName[typeLen] = '\0';
                paramType = new_node(ctx);
                paramType->type = NODE_TYPE;
                paramType->typeAnnotation.name = typeName;
            }
            
            // Add parameter to list
            if (paramCount + 1 > paramCapacity) {
                int newCap = paramCapacity == 0 ? 4 : (paramCapacity * 2);
                FunctionParam* newParams = parser_arena_alloc(ctx, sizeof(FunctionParam) * newCap);
                if (paramCapacity > 0) {
                    memcpy(newParams, params, sizeof(FunctionParam) * paramCount);
                }
                params = newParams;
                paramCapacity = newCap;
            }
            params[paramCount].name = NULL; // No name for function type parameters
            params[paramCount].typeAnnotation = paramType;
            paramCount++;
            
            if (peekToken(ctx).type != TOKEN_COMMA) break;
            nextToken(ctx); // consume ','
        }
    }
    
    // Expect ')'
    if (nextToken(ctx).type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }
    
    // Parse return type
    ASTNode* returnType = NULL;
    if (peekToken(ctx).type == TOKEN_ARROW) {
        nextToken(ctx); // consume '->'
        Token typeTok = peekToken(ctx);
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType(ctx);
        } else {
            returnType = parseTypeAnnotation(ctx);
            if (!returnType) return NULL;
        }
    }
    
    // Create function type node  
    ASTNode* funcType = new_node(ctx);
    funcType->type = NODE_FUNCTION; // Reuse NODE_FUNCTION for function types
    funcType->function.name = NULL; // No name for function types
    funcType->function.params = params;
    funcType->function.paramCount = paramCount;
    funcType->function.returnType = returnType;
    funcType->function.body = NULL; // No body for function types
    
    return funcType;
}

void freeAST(ASTNode* node) {
    // Note: AST memory is managed by the parser context arena
    // This function is kept for backward compatibility but doesn't need to do anything
    (void)node;
}


// Context-based parsing interface - new implementation
ASTNode* parseSourceWithContext(ParserContext* ctx, const char* source) {
    if (!ctx) return NULL;
    
    parser_context_reset(ctx);
    init_scanner(source);
    
    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;
    
    while (true) {
        Token t = peekToken(ctx);
        if (t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }
        if (t.type == TOKEN_COMMA) {
            // Skip commas between statements (for comma-separated variable declarations)
            nextToken(ctx);
            continue;
        }
        if (t.type == TOKEN_SEMICOLON) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1007_SEMICOLON_NOT_ALLOWED, location, 
                               "found ';' here");
            return NULL;
        }
        
        ASTNode* stmt = parseStatement(ctx);
        if (!stmt) {
            return NULL;
        }
        
        addStatement(ctx, &statements, &count, &capacity, stmt);
    }
    
    // Create program node even if empty (valid empty program)
    ASTNode* root = new_node(ctx);
    root->type = NODE_PROGRAM;
    root->program.declarations = statements;
    root->program.count = count;
    root->location.line = 1;
    root->location.column = 1;
    root->dataType = NULL;
    
    return root;
}


