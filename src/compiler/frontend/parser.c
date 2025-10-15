//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/compiler/frontend/parser.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2022 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Implements the recursive-descent parser that produces AST structures from tokens.
//  

// Orus Language Compiler - Parser Implementation


#include "compiler/parser.h"
#include "public/common.h"
#include "vm/vm.h"
#include "vm/vm_string_ops.h"
#include "internal/error_reporting.h"
#include "errors/features/variable_errors.h"
#include "errors/features/control_flow_errors.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <ctype.h>



// Parser recursion depth tracking
#define MAX_RECURSION_DEPTH 1000
#define PARSER_ARENA_SIZE (1 << 16)  // 64KB

// Temporary name counter for destructuring transformations
static int tuple_temp_counter = 0;

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

static void parser_enter_loop(ParserContext* ctx) {
    control_flow_enter_loop_context();
    if (ctx) {
        ctx->loop_depth++;
    }
}

static void parser_leave_loop(ParserContext* ctx) {
    control_flow_leave_loop_context();
    if (ctx && ctx->loop_depth > 0) {
        ctx->loop_depth--;
    }
}

static ASTNode* new_node(ParserContext* ctx) {
    ASTNode* node = parser_arena_alloc(ctx, sizeof(ASTNode));
    if (node) {
        memset(node, 0, sizeof(ASTNode));
    }
    return node;
}

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

static void add_enum_variant(ParserContext* ctx, EnumVariant** list, int* count, int* capacity, EnumVariant variant) {
    if (*count + 1 > *capacity) {
        int newCap = *capacity == 0 ? 4 : (*capacity * 2);
        EnumVariant* newArr = parser_arena_alloc(ctx, sizeof(EnumVariant) * newCap);
        if (*capacity > 0) {
            memcpy(newArr, *list, sizeof(EnumVariant) * (*count));
        }
        *list = newArr;
        *capacity = newCap;
    }
    (*list)[(*count)++] = variant;
}

static Token peekToken(ParserContext* ctx);
static Token nextToken(ParserContext* ctx);
static char* copy_token_text(ParserContext* ctx, Token token);
static bool report_reserved_keyword_identifier(ParserContext* ctx, Token token, const char* context);

static bool is_reserved_keyword_token(TokenType type) {
    switch (type) {
        case TOKEN_AND:
        case TOKEN_BREAK:
        case TOKEN_CONTINUE:
        case TOKEN_PASS:
        case TOKEN_ELSE:
        case TOKEN_ELIF:
        case TOKEN_FOR:
        case TOKEN_FN:
        case TOKEN_IF:
        case TOKEN_OR:
        case TOKEN_NOT:
        case TOKEN_PRINT:
        case TOKEN_RETURN:
        case TOKEN_MUT:
        case TOKEN_WHILE:
        case TOKEN_TRY:
        case TOKEN_CATCH:
        case TOKEN_IN:
        case TOKEN_STRUCT:
        case TOKEN_ENUM:
        case TOKEN_IMPL:
        case TOKEN_IMPORT:
        case TOKEN_AS:
        case TOKEN_MATCH:
        case TOKEN_MATCHES:
        case TOKEN_PUB:
            return true;
        default:
            return false;
    }
}

static bool report_reserved_keyword_identifier(ParserContext* ctx, Token token, const char* context) {
    if (token.type == TOKEN_IDENTIFIER) {
        return true;
    }

    const char* context_label = context ? context : "identifier";
    SrcLocation location = {NULL, token.line, token.column};

    if (token.type == TOKEN_ERROR) {
        const char* message = token.start ? token.start : "invalid token";
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "expected identifier for %s, but %s",
                             context_label,
                             message);
        return false;
    }

    char* token_text = NULL;
    if (token.start && token.length > 0) {
        token_text = copy_token_text(ctx, token);
    }

    if (is_reserved_keyword_token(token.type)) {
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "expected identifier for %s, but '%s' is a reserved keyword",
                             context_label,
                             token_text ? token_text : token_type_to_string(token.type));
    } else {
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "expected identifier for %s, but found '%s'",
                             context_label,
                             token_text ? token_text : token_type_to_string(token.type));
    }

    return false;
}

static bool append_token(Token** tokens, int* count, int* capacity, Token token) {
    if (*count >= *capacity) {
        int new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        Token* resized = realloc(*tokens, sizeof(Token) * (size_t)new_capacity);
        if (!resized) {
            return false;
        }
        *tokens = resized;
        *capacity = new_capacity;
    }
    (*tokens)[(*count)++] = token;
    return true;
}

static bool token_text_equals(const Token* token, const char* text) {
    if (!token || !text) {
        return false;
    }
    if (token->type != TOKEN_IDENTIFIER) {
        return false;
    }
    size_t expected_length = strlen(text);
    return token->length == (int)expected_length &&
           strncmp(token->start, text, expected_length) == 0;
}


static char* parse_qualified_name(ParserContext* ctx, Token firstToken, const char* missingMessage) {
    Token* parts = NULL;
    int partCount = 0;
    int partCapacity = 0;

    if (firstToken.type != TOKEN_IDENTIFIER) {
        SrcLocation location = {NULL, firstToken.line, firstToken.column};
        report_compile_error(E1006_INVALID_SYNTAX, location, missingMessage);
        return NULL;
    }

    if (!append_token(&parts, &partCount, &partCapacity, firstToken)) {
        free(parts);
        return NULL;
    }

    while (peekToken(ctx).type == TOKEN_DOT) {
        nextToken(ctx); // consume '.'
        Token segment = nextToken(ctx);
        if (segment.type != TOKEN_IDENTIFIER) {
            SrcLocation location = {NULL, segment.line, segment.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "expected identifier after '.' in module name");
            free(parts);
            return NULL;
        }
        if (!append_token(&parts, &partCount, &partCapacity, segment)) {
            free(parts);
            return NULL;
        }
    }

    size_t totalLength = 0;
    for (int i = 0; i < partCount; i++) {
        totalLength += (size_t)parts[i].length;
        if (i + 1 < partCount) {
            totalLength += 1; // for '.'
        }
    }

    char* result = parser_arena_alloc(ctx, totalLength + 1);
    if (!result) {
        free(parts);
        return NULL;
    }

    char* writePtr = result;
    for (int i = 0; i < partCount; i++) {
        memcpy(writePtr, parts[i].start, (size_t)parts[i].length);
        writePtr += parts[i].length;
        if (i + 1 < partCount) {
            *writePtr++ = '.';
        }
    }
    *writePtr = '\0';

    free(parts);
    return result;
}

// Parser context lifecycle functions
ParserContext* parser_context_create(void) {
    ParserContext* ctx = malloc(sizeof(ParserContext));
    if (!ctx) return NULL;

    // Initialize arena
    arena_init(&ctx->arena, PARSER_ARENA_SIZE);

    // Initialize state
    ctx->recursion_depth = 0;
    ctx->loop_depth = 0;
    ctx->block_depth = 0;
    ctx->has_peeked_token = false;
    ctx->has_peeked_token2 = false;
    ctx->max_recursion_depth = MAX_RECURSION_DEPTH;
    ctx->allow_array_fill = true;

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
    ctx->loop_depth = 0;
    ctx->block_depth = 0;
    ctx->has_peeked_token = false;
    ctx->has_peeked_token2 = false;
    ctx->allow_array_fill = true;
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

static Token consume_indent_token(ParserContext* ctx) {
    while (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }
    return nextToken(ctx);
}

// Forward declarations - all now take ParserContext*
static ASTNode* parsePrintStatement(ParserContext* ctx);
static ASTNode* parseExpression(ParserContext* ctx);
static ASTNode* parseBinaryExpression(ParserContext* ctx, int minPrec);
static ASTNode* parsePrimaryExpression(ParserContext* ctx);
static ASTNode* parseArrayLiteral(ParserContext* ctx, Token leftToken);

// Control flow parsing functions
static ASTNode* parseIfStatement(ParserContext* ctx);
static ASTNode* parseTryStatement(ParserContext* ctx);
static ASTNode* parseWhileStatement(ParserContext* ctx);
static ASTNode* parseForStatement(ParserContext* ctx);
static ASTNode* parseBreakStatement(ParserContext* ctx);
static ASTNode* parseContinueStatement(ParserContext* ctx);
static ASTNode* parseFunctionDefinition(ParserContext* ctx, bool isPublic);
static ASTNode* parseFunctionExpression(ParserContext* ctx, Token fnToken);
static ASTNode* parseReturnStatement(ParserContext* ctx);
static ASTNode* parseMatchStatement(ParserContext* ctx);
static ASTNode* parseMatchExpression(ParserContext* ctx, Token matchTok);

// Primary expression handlers
static ASTNode* parseNumberLiteral(ParserContext* ctx, Token token);

// Number parsing helper functions
static void preprocessNumberToken(const char* tokenStart, int tokenLength, char* numStr, int* processedLength);
// Suffix detection removed - type inference handles numeric types
static bool isFloatingPointNumber(const char* numStr, int length);
static Value parseNumberValue(const char* numStr, int length);
static bool token_is_numeric_suffix(const Token* token);
static bool tokens_are_adjacent(const Token* first, const Token* second);

static ASTNode* parseStringLiteral(ParserContext* ctx, Token token);
static ASTNode* parseBooleanLiteral(ParserContext* ctx, Token token);
static ASTNode* parseIdentifierExpression(ParserContext* ctx, Token token);
static ASTNode* parseTimeStampExpression(ParserContext* ctx, Token token);
static ASTNode* parseParenthesizedExpressionToken(ParserContext* ctx, Token token);
static ASTNode* parseVariableDeclaration(ParserContext* ctx, bool isMutable, bool isPublic, Token nameToken);
static ASTNode* parseAssignOrVarList(ParserContext* ctx, bool isMutable, bool isPublic, Token nameToken);
static ASTNode* parseImportStatement(ParserContext* ctx);
static ASTNode* parseStatement(ParserContext* ctx);
static ASTNode* parseDestructuringAssignment(ParserContext* ctx, Token firstToken);
static ASTNode* parseInlineBlock(ParserContext* ctx);
static ASTNode* parseIfStatement(ParserContext* ctx);
static ASTNode* parseTryStatement(ParserContext* ctx);
static ASTNode* parseWhileStatement(ParserContext* ctx);
static ASTNode* parseForStatement(ParserContext* ctx);
static ASTNode* parseBreakStatement(ParserContext* ctx);
static ASTNode* parseContinueStatement(ParserContext* ctx);
static ASTNode* parsePassStatement(ParserContext* ctx);
static ASTNode* parseBlock(ParserContext* ctx);
static ASTNode* parseFunctionDefinition(ParserContext* ctx, bool isPublic);
static ASTNode* parseEnumDefinition(ParserContext* ctx, bool isPublic);
static ASTNode* parseStructDefinition(ParserContext* ctx, bool isPublic);
static ASTNode* parseImplBlock(ParserContext* ctx, bool isPublic);
static ASTNode* parseReturnStatement(ParserContext* ctx);
static ASTNode* parseMatchStatement(ParserContext* ctx);
static ASTNode* parseCallExpression(ParserContext* ctx, ASTNode* callee);
static ASTNode* parseIndexExpression(ParserContext* ctx, ASTNode* arrayExpr, Token openToken);
static ASTNode* parsePostfixExpressions(ParserContext* ctx, ASTNode* expr);
static ASTNode* parseFunctionType(ParserContext* ctx);
static ASTNode* parseTypeAnnotation(ParserContext* ctx);
static ASTNode* parseStructLiteral(ParserContext* ctx, ASTNode* typeExpr, Token leftBrace);
static ASTNode* parseMemberAccess(ParserContext* ctx, ASTNode* objectExpr);

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
        case TOKEN_MATCHES:
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
        case TOKEN_MATCHES: return "==";
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
    return parseSourceWithModuleName(source, NULL);
}

ASTNode* parseSourceWithModuleName(const char* source, const char* module_name) {
    static ParserContext* global_ctx = NULL;
    if (!global_ctx) {
        global_ctx = parser_context_create();
    }
    return parseSourceWithContextAndModule(global_ctx, source, module_name);
}

static ASTNode* parseStatement(ParserContext* ctx) {
    Token t = peekToken(ctx);

    if (t.type == TOKEN_PRINT) {
        return parsePrintStatement(ctx);
    }
    if (t.type == TOKEN_PASS) {
        return parsePassStatement(ctx);
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
    if (t.type == TOKEN_PUB) {
        if (ctx->block_depth > 0) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "'pub' declarations are only allowed at module scope");
            return NULL;
        }

        nextToken(ctx); // consume 'pub'
        Token afterPub = peekToken(ctx);

        if (afterPub.type == TOKEN_STRUCT) {
            return parseStructDefinition(ctx, true);
        } else if (afterPub.type == TOKEN_ENUM) {
            return parseEnumDefinition(ctx, true);
        } else if (afterPub.type == TOKEN_IMPL) {
            return parseImplBlock(ctx, true);
        } else if (afterPub.type == TOKEN_FN) {
            return parseFunctionDefinition(ctx, true);
        }

        bool isMutable = false;
        Token nameTok;
        if (afterPub.type == TOKEN_MUT) {
            nextToken(ctx); // consume 'mut'
            isMutable = true;
            nameTok = nextToken(ctx);
        } else {
            nameTok = nextToken(ctx);
        }

        if (nameTok.type != TOKEN_IDENTIFIER) {
            return NULL;
        }

        Token nextAfterName = peekToken(ctx);
        if (nextAfterName.type == TOKEN_COLON || nextAfterName.type == TOKEN_DEFINE) {
            return parseVariableDeclaration(ctx, isMutable, true, nameTok);
        }
        return parseAssignOrVarList(ctx, isMutable, true, nameTok);
    }
    if (t.type == TOKEN_IMPORT) {
        if (ctx->block_depth > 0) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "'use' declarations are only allowed at module scope");
            return NULL;
        }
        return parseImportStatement(ctx);
    }
    if (t.type == TOKEN_MUT) {
        nextToken(ctx); // consume TOKEN_MUT
        Token nameTok = nextToken(ctx); // get identifier
        if (nameTok.type != TOKEN_IDENTIFIER) return NULL;
        if (peekToken(ctx).type == TOKEN_COLON) {
            return parseVariableDeclaration(ctx, true, false, nameTok);
        }
        return parseAssignOrVarList(ctx, true, false, nameTok);
    }
    if (t.type == TOKEN_STRUCT) {
        return parseStructDefinition(ctx, false);
    }
    if (t.type == TOKEN_ENUM) {
        return parseEnumDefinition(ctx, false);
    }
    if (t.type == TOKEN_IMPL) {
        return parseImplBlock(ctx, false);
    }
    if (t.type == TOKEN_MATCH) {
        return parseMatchStatement(ctx);
    }
    if (t.type == TOKEN_IDENTIFIER) {
        Token second = peekSecondToken(ctx);
        if (second.type == TOKEN_COLON || second.type == TOKEN_DEFINE) {
            nextToken(ctx);
            return parseVariableDeclaration(ctx, false, false, t);
        } else if (second.type == TOKEN_EQUAL) {
            nextToken(ctx);
            return parseAssignOrVarList(ctx, false, false, t);
        } else if (second.type == TOKEN_COMMA) {
            return parseDestructuringAssignment(ctx, t);
        }
    }
    if (t.type == TOKEN_TRY) {
        return parseTryStatement(ctx);
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
        return parseFunctionDefinition(ctx, false);
    } else if (t.type == TOKEN_RETURN) {
        return parseReturnStatement(ctx);
    } else {
        return parseExpression(ctx);
    }
}

typedef struct {
    bool isWildcard;
    bool isEnumCase;
    char* enumTypeName;
    char* variantName;
    char** payloadNames;
    int payloadCount;
    ASTNode* valuePattern;
    ASTNode* body;
    SrcLocation location;
} MatchCaseInfo;

static void addMatchCase(ParserContext* ctx, MatchCaseInfo** list, int* count, int* capacity, MatchCaseInfo info) {
    if (*count + 1 > *capacity) {
        int newCap = *capacity == 0 ? 4 : (*capacity * 2);
        MatchCaseInfo* newArr = parser_arena_alloc(ctx, sizeof(MatchCaseInfo) * newCap);
        if (*capacity > 0) {
            memcpy(newArr, *list, sizeof(MatchCaseInfo) * (*count));
        }
        *list = newArr;
        *capacity = newCap;
    }
    (*list)[(*count)++] = info;
}

static bool parser_literal_is_numeric(Value value) {
    switch (value.type) {
        case VAL_I32:
        case VAL_I64:
        case VAL_U32:
        case VAL_U64:
        case VAL_F64:
        case VAL_NUMBER:
            return true;
        default:
            return false;
    }
}

static long double parser_literal_to_long_double(Value value) {
    switch (value.type) {
        case VAL_I32:
            return (long double)AS_I32(value);
        case VAL_I64:
            return (long double)AS_I64(value);
        case VAL_U32:
            return (long double)AS_U32(value);
        case VAL_U64:
            return (long double)AS_U64(value);
        case VAL_F64:
            return (long double)AS_F64(value);
        case VAL_NUMBER:
            return (long double)value.as.number;
        default:
            return 0.0L;
    }
}

static bool parser_match_literals_equal(Value a, Value b) {
    if (a.type == b.type) {
        switch (a.type) {
            case VAL_BOOL:
                return AS_BOOL(a) == AS_BOOL(b);
            case VAL_I32:
                return AS_I32(a) == AS_I32(b);
            case VAL_I64:
                return AS_I64(a) == AS_I64(b);
            case VAL_U32:
                return AS_U32(a) == AS_U32(b);
            case VAL_U64:
                return AS_U64(a) == AS_U64(b);
            case VAL_F64:
                return AS_F64(a) == AS_F64(b);
            case VAL_NUMBER:
                return a.as.number == b.as.number;
            case VAL_STRING: {
                ObjString* left = AS_STRING(a);
                ObjString* right = AS_STRING(b);
                const char* left_chars = left ? string_get_chars(left) : NULL;
                const char* right_chars = right ? string_get_chars(right) : NULL;
                if (!left_chars || !right_chars) {
                    return left == right;
                }
                return strcmp(left_chars, right_chars) == 0;
            }
            default:
                return false;
        }
    }

    if (parser_literal_is_numeric(a) && parser_literal_is_numeric(b)) {
        return parser_literal_to_long_double(a) == parser_literal_to_long_double(b);
    }

    return false;
}

static void parser_format_match_literal(Value value, char* buffer, size_t size) {
    if (!buffer || size == 0) {
        return;
    }

    switch (value.type) {
        case VAL_BOOL:
            snprintf(buffer, size, "%s", AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_I32:
            snprintf(buffer, size, "%d", AS_I32(value));
            break;
        case VAL_I64:
            snprintf(buffer, size, "%lld", (long long)AS_I64(value));
            break;
        case VAL_U32:
            snprintf(buffer, size, "%u", AS_U32(value));
            break;
        case VAL_U64:
            snprintf(buffer, size, "%llu", (unsigned long long)AS_U64(value));
            break;
        case VAL_F64:
        case VAL_NUMBER:
            snprintf(buffer, size, "%g",
                     value.type == VAL_NUMBER ? value.as.number : AS_F64(value));
            break;
        case VAL_STRING: {
            ObjString* str = AS_STRING(value);
            const char* chars = str ? string_get_chars(str) : NULL;
            snprintf(buffer, size, "\"%s\"", chars ? chars : "");
            break;
        }
        default:
            snprintf(buffer, size, "<literal>");
            break;
    }
}

static bool detect_duplicate_literal_cases(ParserContext* ctx, MatchCaseInfo* cases, int caseCount) {
    (void)ctx;
    if (!cases || caseCount <= 0) {
        return true;
    }

    Value* seen_literals = malloc(sizeof(Value) * (size_t)caseCount);
    if (!seen_literals) {
        return true;
    }

    int seen_count = 0;
    for (int i = 0; i < caseCount; i++) {
        MatchCaseInfo* info = &cases[i];
        if (!info->valuePattern || info->valuePattern->type != NODE_LITERAL) {
            continue;
        }

        Value literal = info->valuePattern->literal.value;
        bool duplicate = false;
        for (int j = 0; j < seen_count; j++) {
            if (parser_match_literals_equal(seen_literals[j], literal)) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            char repr[128];
            parser_format_match_literal(literal, repr, sizeof(repr));
            report_duplicate_literal_match_arm(info->location, repr);
            free(seen_literals);
            return false;
        }

        seen_literals[seen_count++] = literal;
    }

    free(seen_literals);
    return true;
}

static void addStringEntry(ParserContext* ctx, char*** list, int* count, int* capacity, char* value) {
    if (!value) return;
    if (*count + 1 > *capacity) {
        int newCap = *capacity == 0 ? 4 : (*capacity * 2);
        char** newArr = parser_arena_alloc(ctx, sizeof(char*) * newCap);
        if (*capacity > 0 && *list) {
            memcpy(newArr, *list, sizeof(char*) * (*count));
        }
        *list = newArr;
        *capacity = newCap;
    }
    (*list)[(*count)++] = value;
}

static char* copy_token_text(ParserContext* ctx, Token token) {
    char* text = parser_arena_alloc(ctx, (size_t)token.length + 1);
    memcpy(text, token.start, (size_t)token.length);
    text[token.length] = '\0';
    return text;
}

static ASTNode* wrap_statement_in_block(ParserContext* ctx, ASTNode* stmt) {
    if (!stmt) return NULL;
    ASTNode** statements = parser_arena_alloc(ctx, sizeof(ASTNode*));
    statements[0] = stmt;
    ASTNode* block = new_node(ctx);
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = 1;
    block->block.createsScope = true;
    block->location = stmt->location;
    block->dataType = NULL;
    return block;
}

static ASTNode* create_identifier_node(ParserContext* ctx, const char* name, SrcLocation location) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_IDENTIFIER;
    node->identifier.name = (char*)name;
    node->location = location;
    node->dataType = NULL;
    return node;
}

static ASTNode* create_binary_equals(ParserContext* ctx, ASTNode* left, ASTNode* right, SrcLocation location) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_BINARY;
    node->binary.left = left;
    node->binary.right = right;
    node->binary.op = "==";
    node->location = location;
    node->dataType = NULL;
    return node;
}

static ASTNode* create_enum_match_test(ParserContext* ctx, ASTNode* value, MatchCaseInfo* info) {
    if (!ctx || !value || !info) return NULL;
    ASTNode* node = new_node(ctx);
    node->type = NODE_ENUM_MATCH_TEST;
    node->enumMatchTest.value = value;
    node->enumMatchTest.enumTypeName = info->enumTypeName;
    node->enumMatchTest.variantName = info->variantName;
    node->enumMatchTest.variantIndex = -1;
    node->enumMatchTest.expectedPayloadCount = info->payloadCount;
    node->location = info->location;
    node->dataType = NULL;
    return node;
}

static ASTNode* create_enum_payload_access(ParserContext* ctx, ASTNode* value, MatchCaseInfo* info, int fieldIndex) {
    if (!ctx || !value || !info || fieldIndex < 0) return NULL;
    ASTNode* node = new_node(ctx);
    node->type = NODE_ENUM_PAYLOAD;
    node->enumPayload.value = value;
    node->enumPayload.enumTypeName = info->enumTypeName;
    node->enumPayload.variantName = info->variantName;
    node->enumPayload.variantIndex = -1;
    node->enumPayload.fieldIndex = fieldIndex;
    node->location = info->location;
    node->dataType = NULL;
    return node;
}

static ASTNode* create_var_decl_with_initializer(ParserContext* ctx, char* name, ASTNode* initializer, SrcLocation location) {
    ASTNode* decl = new_node(ctx);
    decl->type = NODE_VAR_DECL;
    decl->varDecl.name = name;
    decl->varDecl.isPublic = false;
    decl->varDecl.isGlobal = false;
    decl->varDecl.initializer = initializer;
    decl->varDecl.typeAnnotation = NULL;
    decl->varDecl.isMutable = false;
    decl->location = location;
    decl->dataType = NULL;
    return decl;
}

static ASTNode* create_enum_match_check(ParserContext* ctx,
                                        const char* tempName,
                                        char* enumTypeName,
                                        char** variantNames,
                                        int variantCount,
                                        bool hasWildcard,
                                        SrcLocation location) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_ENUM_MATCH_CHECK;
    node->enumMatchCheck.value = create_identifier_node(ctx, tempName, location);
    node->enumMatchCheck.enumTypeName = enumTypeName;
    node->enumMatchCheck.variantNames = variantNames;
    node->enumMatchCheck.variantCount = variantCount;
    node->enumMatchCheck.hasWildcard = hasWildcard;
    node->location = location;
    node->dataType = NULL;
    return node;
}

static int generate_match_temp_id(void) {
    static int counter = 0;
    return counter++;
}

static ASTNode* parseMatchStatement(ParserContext* ctx) {
    Token matchTok = nextToken(ctx);
    if (matchTok.type != TOKEN_MATCH) {
        return NULL;
    }

    ASTNode* subject = parseExpression(ctx);
    if (!subject) {
        return NULL;
    }

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        return NULL;
    }

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    Token indent = consume_indent_token(ctx);
    if (indent.type != TOKEN_INDENT) {
        return NULL;
    }

    MatchCaseInfo* cases = NULL;
    int caseCount = 0;
    int caseCapacity = 0;
    bool hasEnumCases = false;

    while (true) {
        Token next = peekToken(ctx);
        if (next.type == TOKEN_DEDENT) {
            break;
        }
        if (next.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }

        Token patternStart = peekToken(ctx);
        if (patternStart.type == TOKEN_EOF) {
            return NULL;
        }

        SrcLocation patternLocation = {NULL, patternStart.line, patternStart.column};
        bool isWildcard = false;
        bool isEnumCase = false;
        char* enumTypeName = NULL;
        char* variantName = NULL;
        char** payloadNames = NULL;
        int payloadCount = 0;
        int payloadCapacity = 0;
        ASTNode* valuePattern = NULL;

        if (patternStart.type == TOKEN_IDENTIFIER && patternStart.length == 1 &&
            patternStart.start[0] == '_') {
            nextToken(ctx);
            isWildcard = true;
        } else if (patternStart.type == TOKEN_IDENTIFIER &&
                   peekSecondToken(ctx).type == TOKEN_DOT) {
            isEnumCase = true;
            Token enumTok = nextToken(ctx);
            enumTypeName = copy_token_text(ctx, enumTok);
            nextToken(ctx);
            Token variantTok = nextToken(ctx);
            if (variantTok.type != TOKEN_IDENTIFIER) {
                return NULL;
            }
            variantName = copy_token_text(ctx, variantTok);

            if (peekToken(ctx).type == TOKEN_LEFT_PAREN) {
                nextToken(ctx);
                if (peekToken(ctx).type == TOKEN_RIGHT_PAREN) {
                    nextToken(ctx);
                } else {
                    while (true) {
                        Token payloadTok = nextToken(ctx);
                        if (payloadTok.type != TOKEN_IDENTIFIER) {
                            return NULL;
                        }
                        char* bindingName = NULL;
                        if (!(payloadTok.length == 1 && payloadTok.start[0] == '_')) {
                            bindingName = copy_token_text(ctx, payloadTok);
                        }
                        if (payloadCount + 1 > payloadCapacity) {
                            int newCap = payloadCapacity == 0 ? 4 : (payloadCapacity * 2);
                            char** newArr = parser_arena_alloc(ctx, sizeof(char*) * newCap);
                            if (payloadCapacity > 0 && payloadNames) {
                                memcpy(newArr, payloadNames, sizeof(char*) * payloadCount);
                            }
                            payloadNames = newArr;
                            payloadCapacity = newCap;
                        }
                        payloadNames[payloadCount++] = bindingName;

                        Token delim = peekToken(ctx);
                        if (delim.type == TOKEN_COMMA) {
                            nextToken(ctx);
                            continue;
                        }
                        if (delim.type == TOKEN_RIGHT_PAREN) {
                            nextToken(ctx);
                            break;
                        }
                        return NULL;
                    }
                }
            }
        } else {
            valuePattern = parseExpression(ctx);
            if (!valuePattern) {
                return NULL;
            }
        }

        Token delimiter = nextToken(ctx);
        if (delimiter.type != TOKEN_ARROW) {
            return NULL;
        }

        ASTNode* body = NULL;
        Token afterArrow = peekToken(ctx);
        if (afterArrow.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            Token bodyIndent = consume_indent_token(ctx);
            if (bodyIndent.type != TOKEN_INDENT) {
                return NULL;
            }
            body = parseBlock(ctx);
            if (!body) {
                return NULL;
            }
        } else {
            body = parseStatement(ctx);
            if (!body) {
                return NULL;
            }
            if (body->type != NODE_BLOCK) {
                body = wrap_statement_in_block(ctx, body);
            }
        }

        if (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }

        if (isEnumCase) {
            hasEnumCases = true;
        }

        MatchCaseInfo info = {isWildcard,       isEnumCase,       enumTypeName,
                              variantName,     payloadNames,     payloadCount,
                              valuePattern,    body,             patternLocation};
        addMatchCase(ctx, &cases, &caseCount, &caseCapacity, info);
    }
    Token dedentTok = nextToken(ctx);
    if (dedentTok.type != TOKEN_DEDENT) {
        return NULL;
    }

    if (caseCount == 0) {
        return NULL;
    }

    if (!detect_duplicate_literal_cases(ctx, cases, caseCount)) {
        return NULL;
    }

    char tempNameBuffer[32];
    int tempId = generate_match_temp_id();
    snprintf(tempNameBuffer, sizeof(tempNameBuffer), "__match_tmp_%d", tempId);
    size_t tempLen = strlen(tempNameBuffer);
    char* tempName = parser_arena_alloc(ctx, tempLen + 1);
    memcpy(tempName, tempNameBuffer, tempLen + 1);

    ASTNode* tempVarDecl = new_node(ctx);
    tempVarDecl->type = NODE_VAR_DECL;
    tempVarDecl->varDecl.name = tempName;
    tempVarDecl->varDecl.isPublic = false;
    tempVarDecl->varDecl.isGlobal = false;
    tempVarDecl->varDecl.initializer = subject;
    tempVarDecl->varDecl.typeAnnotation = NULL;
    tempVarDecl->varDecl.isMutable = false;
    tempVarDecl->location.line = matchTok.line;
    tempVarDecl->location.column = matchTok.column;
    tempVarDecl->dataType = NULL;

    ASTNode* rootIf = NULL;
    ASTNode* currentIf = NULL;
    ASTNode* wildcardBlock = NULL;
    bool hasWildcardCase = false;
    char** handledVariants = NULL;
    int handledVariantCount = 0;
    int handledVariantCapacity = 0;
    char* declaredEnumType = NULL;

    for (int i = 0; i < caseCount; i++) {
        MatchCaseInfo* info = &cases[i];
        if (info->isWildcard) {
            wildcardBlock = info->body;
            hasWildcardCase = true;
            continue;
        }

        if (!declaredEnumType && info->isEnumCase && info->enumTypeName) {
            declaredEnumType = info->enumTypeName;
        }

        ASTNode* tempIdentifier = create_identifier_node(ctx, tempName, info->location);
        ASTNode* condition = NULL;
        if (info->isEnumCase) {
            condition = create_enum_match_test(ctx, tempIdentifier, info);
        } else if (info->valuePattern) {
            condition = create_binary_equals(ctx, tempIdentifier, info->valuePattern, info->location);
        }
        if (!condition) {
            return NULL;
        }

        ASTNode* thenBlock = info->body;
        if (thenBlock && thenBlock->type != NODE_BLOCK) {
            thenBlock = wrap_statement_in_block(ctx, thenBlock);
        }

        int bindingCount = 0;
        if (info->payloadCount > 0 && info->payloadNames && thenBlock && thenBlock->type == NODE_BLOCK) {
            for (int j = 0; j < info->payloadCount; j++) {
                if (info->payloadNames[j]) {
                    bindingCount++;
                }
            }

            if (bindingCount > 0) {
                int totalStatements = bindingCount + thenBlock->block.count;
                ASTNode** combined = parser_arena_alloc(ctx, sizeof(ASTNode*) * totalStatements);
                int idx = 0;
                for (int j = 0; j < info->payloadCount; j++) {
                    if (!info->payloadNames[j]) {
                        continue;
                    }
                    ASTNode* payloadSource = create_identifier_node(ctx, tempName, info->location);
                    ASTNode* payloadExpr = create_enum_payload_access(ctx, payloadSource, info, j);
                    ASTNode* bindingDecl = create_var_decl_with_initializer(ctx, info->payloadNames[j], payloadExpr, info->location);
                    combined[idx++] = bindingDecl;
                }
                for (int j = 0; j < thenBlock->block.count; j++) {
                    combined[idx++] = thenBlock->block.statements[j];
                }

                ASTNode* expandedBlock = new_node(ctx);
                expandedBlock->type = NODE_BLOCK;
                expandedBlock->block.statements = combined;
                expandedBlock->block.count = bindingCount + thenBlock->block.count;
                expandedBlock->location = thenBlock->location;
                expandedBlock->dataType = NULL;
                thenBlock = expandedBlock;
            }
        }

        if (info->isEnumCase) {
            addStringEntry(ctx, &handledVariants, &handledVariantCount, &handledVariantCapacity,
                           info->variantName);
        }

        ASTNode* ifNode = new_node(ctx);
        ifNode->type = NODE_IF;
        ifNode->ifStmt.condition = condition;
        ifNode->ifStmt.thenBranch = thenBlock;
        ifNode->ifStmt.elseBranch = NULL;
        ifNode->location = info->location;
        ifNode->dataType = NULL;

        if (!rootIf) {
            rootIf = ifNode;
        } else {
            currentIf->ifStmt.elseBranch = ifNode;
        }
        currentIf = ifNode;
    }

    if (currentIf && wildcardBlock) {
        currentIf->ifStmt.elseBranch = wildcardBlock;
    }

    ASTNode** statements = NULL;
    int statementCount = 0;
    int statementCapacity = 0;
    addStatement(ctx, &statements, &statementCount, &statementCapacity, tempVarDecl);

    if (rootIf) {
        addStatement(ctx, &statements, &statementCount, &statementCapacity, rootIf);
    } else if (wildcardBlock && wildcardBlock->type == NODE_BLOCK) {
        for (int i = 0; i < wildcardBlock->block.count; i++) {
            addStatement(ctx, &statements, &statementCount, &statementCapacity,
                         wildcardBlock->block.statements[i]);
        }
    }

    SrcLocation checkLocation = {NULL, matchTok.line, matchTok.column};
    if (hasEnumCases) {
        ASTNode* matchCheck = create_enum_match_check(ctx, tempName, declaredEnumType,
                                                     handledVariants, handledVariantCount,
                                                     hasWildcardCase, checkLocation);
        if (matchCheck) {
            addStatement(ctx, &statements, &statementCount, &statementCapacity, matchCheck);
        }
    }

    ASTNode* matchBlock = new_node(ctx);
    matchBlock->type = NODE_BLOCK;
    matchBlock->block.statements = statements;
    matchBlock->block.count = statementCount;
    matchBlock->block.createsScope = true;
    matchBlock->location.line = matchTok.line;
    matchBlock->location.column = matchTok.column;
    matchBlock->dataType = NULL;

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    return matchBlock;
}

static ASTNode* parseMatchExpression(ParserContext* ctx, Token matchTok) {
    ASTNode* subject = parseExpression(ctx);
    if (!subject) {
        return NULL;
    }

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        return NULL;
    }

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    Token indent = consume_indent_token(ctx);
    if (indent.type != TOKEN_INDENT) {
        return NULL;
    }

    MatchCaseInfo* cases = NULL;
    int caseCount = 0;
    int caseCapacity = 0;
    bool hasWildcard = false;

    while (true) {
        Token next = peekToken(ctx);
        if (next.type == TOKEN_DEDENT) {
            break;
        }
        if (next.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }

        Token patternStart = peekToken(ctx);
        if (patternStart.type == TOKEN_EOF) {
            return NULL;
        }

        SrcLocation patternLocation = {NULL, patternStart.line, patternStart.column};
        bool isWildcard = false;
        bool isEnumCase = false;
        char* enumTypeName = NULL;
        char* variantName = NULL;
        char** payloadNames = NULL;
        int payloadCount = 0;
        int payloadCapacity = 0;
        ASTNode* valuePattern = NULL;

        if (patternStart.type == TOKEN_IDENTIFIER && patternStart.length == 1 &&
            patternStart.start[0] == '_') {
            nextToken(ctx);
            isWildcard = true;
            hasWildcard = true;
        } else if (patternStart.type == TOKEN_IDENTIFIER &&
                   peekSecondToken(ctx).type == TOKEN_DOT) {
            isEnumCase = true;
            Token enumTok = nextToken(ctx);
            enumTypeName = copy_token_text(ctx, enumTok);
            nextToken(ctx);
            Token variantTok = nextToken(ctx);
            if (variantTok.type != TOKEN_IDENTIFIER) {
                return NULL;
            }
            variantName = copy_token_text(ctx, variantTok);

            if (peekToken(ctx).type == TOKEN_LEFT_PAREN) {
                nextToken(ctx);
                if (peekToken(ctx).type == TOKEN_RIGHT_PAREN) {
                    nextToken(ctx);
                } else {
                    while (true) {
                        Token payloadTok = nextToken(ctx);
                        if (payloadTok.type != TOKEN_IDENTIFIER) {
                            return NULL;
                        }
                        char* bindingName = NULL;
                        if (!(payloadTok.length == 1 && payloadTok.start[0] == '_')) {
                            bindingName = copy_token_text(ctx, payloadTok);
                        }
                        if (payloadCount + 1 > payloadCapacity) {
                            int newCap = payloadCapacity == 0 ? 4 : (payloadCapacity * 2);
                            char** newArr = parser_arena_alloc(ctx, sizeof(char*) * newCap);
                            if (payloadCapacity > 0 && payloadNames) {
                                memcpy(newArr, payloadNames, sizeof(char*) * payloadCount);
                            }
                            payloadNames = newArr;
                            payloadCapacity = newCap;
                        }
                        payloadNames[payloadCount++] = bindingName;

                        Token delim = peekToken(ctx);
                        if (delim.type == TOKEN_COMMA) {
                            nextToken(ctx);
                            continue;
                        }
                        if (delim.type == TOKEN_RIGHT_PAREN) {
                            nextToken(ctx);
                            break;
                        }
                        return NULL;
                    }
                }
            }
        } else {
            valuePattern = parseExpression(ctx);
            if (!valuePattern) {
                return NULL;
            }
        }

        Token arrow = nextToken(ctx);
        if (arrow.type != TOKEN_ARROW) {
            return NULL;
        }

        ASTNode* body = parseExpression(ctx);
        if (!body) {
            return NULL;
        }

        if (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }

        MatchCaseInfo info = {isWildcard,       isEnumCase,       enumTypeName,
                              variantName,     payloadNames,     payloadCount,
                              valuePattern,    body,             patternLocation};
        addMatchCase(ctx, &cases, &caseCount, &caseCapacity, info);
    }

    Token dedentTok = nextToken(ctx);
    if (dedentTok.type != TOKEN_DEDENT) {
        return NULL;
    }

    if (caseCount == 0) {
        return NULL;
    }

    if (!detect_duplicate_literal_cases(ctx, cases, caseCount)) {
        return NULL;
    }

    char tempNameBuffer[32];
    int tempId = generate_match_temp_id();
    snprintf(tempNameBuffer, sizeof(tempNameBuffer), "__match_tmp_%d", tempId);
    size_t tempLen = strlen(tempNameBuffer);
    char* tempName = parser_arena_alloc(ctx, tempLen + 1);
    memcpy(tempName, tempNameBuffer, tempLen + 1);

    MatchArm* arms = parser_arena_alloc(ctx, sizeof(MatchArm) * caseCount);
    for (int i = 0; i < caseCount; i++) {
        MatchCaseInfo* info = &cases[i];
        MatchArm arm = {0};
        arm.isWildcard = info->isWildcard;
        arm.isEnumCase = info->isEnumCase;
        arm.enumTypeName = info->enumTypeName;
        arm.variantName = info->variantName;
        arm.payloadNames = info->payloadNames;
        arm.payloadCount = info->payloadCount;
        arm.variantIndex = -1;
        arm.valuePattern = info->valuePattern;
        arm.body = info->body;
        arm.location = info->location;

        ASTNode* tempIdentifier = create_identifier_node(ctx, tempName, info->location);
        if (info->isWildcard) {
            arm.condition = NULL;
            arm.payloadAccesses = NULL;
        } else if (info->isEnumCase) {
            arm.condition = create_enum_match_test(ctx, tempIdentifier, info);
            if (!arm.condition) {
                return NULL;
            }
            if (info->payloadCount > 0) {
                arm.payloadAccesses = parser_arena_alloc(ctx, sizeof(ASTNode*) * info->payloadCount);
                for (int j = 0; j < info->payloadCount; j++) {
                    if (info->payloadNames && info->payloadNames[j]) {
                        ASTNode* payloadSource = create_identifier_node(ctx, tempName, info->location);
                        arm.payloadAccesses[j] = create_enum_payload_access(ctx, payloadSource, info, j);
                        if (!arm.payloadAccesses[j]) {
                            return NULL;
                        }
                    } else {
                        arm.payloadAccesses[j] = NULL;
                    }
                }
            } else {
                arm.payloadAccesses = NULL;
            }
        } else {
            arm.condition = create_binary_equals(ctx, tempIdentifier, info->valuePattern, info->location);
            if (!arm.condition) {
                return NULL;
            }
            arm.payloadAccesses = NULL;
        }

        arms[i] = arm;
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_MATCH_EXPRESSION;
    node->matchExpr.subject = subject;
    node->matchExpr.tempName = tempName;
    node->matchExpr.arms = arms;
    node->matchExpr.armCount = caseCount;
    node->matchExpr.hasWildcard = hasWildcard;
    node->location.line = matchTok.line;
    node->location.column = matchTok.column;
    node->dataType = NULL;

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    return node;
}

static ASTNode* parsePrintStatement(ParserContext* ctx) {
    // Consume PRINT keyword (always prints with newline)
    Token printTok = nextToken(ctx);

    // Expect '('
    Token left = nextToken(ctx);
    if (left.type != TOKEN_LEFT_PAREN) {
        SrcLocation location = {NULL, left.line, left.column};
        report_compile_error(E1003_MISSING_PARENTHESIS, location,
                             "expected '(' after print but found %s",
                             token_type_to_string(left.type));
        return NULL;
    }

    // Gather zero or more comma-separated expressions
    ASTNode** args = NULL;
    int count = 0, capacity = 0;

    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        while (true) {
            ASTNode* expr = parseExpression(ctx);
            if (!expr) return NULL;
            addStatement(ctx, &args, &count, &capacity, expr);

            Token separator = peekToken(ctx);
            if (separator.type == TOKEN_COMMA) {
                nextToken(ctx);  // consume comma and continue parsing arguments
                continue;
            }

            if (separator.type == TOKEN_RIGHT_PAREN) {
                break;  // End of argument list
            }

            SrcLocation location = {NULL, separator.line, separator.column};
            if (separator.length > 0 && separator.start != NULL) {
                report_compile_error(E1019_MISSING_PRINT_SEPARATOR, location,
                                     "I was expecting a comma before \"%.*s\" so the next value is clear.",
                                     separator.length, separator.start);
            } else {
                report_compile_error(E1019_MISSING_PRINT_SEPARATOR, location,
                                     "I was expecting a comma before %s so the next value is clear.",
                                     token_type_to_string(separator.type));
            }
            return NULL;
        }
    }

    // Expect ')'
    Token close = nextToken(ctx);
    if (close.type != TOKEN_RIGHT_PAREN) {
        SrcLocation location = {NULL, close.line, close.column};
        report_compile_error(E1003_MISSING_PARENTHESIS, location,
                             "expected ')' to close print arguments but found %s",
                             token_type_to_string(close.type));
        return NULL;
    }

    // Build the NODE_PRINT AST node
    ASTNode* node = new_node(ctx);
    node->type = NODE_PRINT;
    node->print.values = args;
    node->print.count = count;
    node->location.line = printTok.line;
    node->location.column = printTok.column;
    node->dataType = NULL;

    return node;
}

static bool token_can_start_type(Token token) {
    if (token.type == TOKEN_IDENTIFIER) {
        return true;
    }
    if (token.type == TOKEN_LEFT_BRACKET) {
        return true;
    }
    return false;
}

static ASTNode* build_type_annotation_node(ParserContext* ctx, Token typeTok);

static ASTNode* parse_array_type_annotation(ParserContext* ctx, Token openToken) {
    Token elementTok = nextToken(ctx);
    while (elementTok.type == TOKEN_NEWLINE || elementTok.type == TOKEN_INDENT ||
           elementTok.type == TOKEN_DEDENT) {
        elementTok = nextToken(ctx);
    }

    if (elementTok.type == TOKEN_RIGHT_BRACKET) {
        SrcLocation location = {NULL, elementTok.line, elementTok.column};
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "expected an element type before ']'");
        return NULL;
    }

    ASTNode* elementType = build_type_annotation_node(ctx, elementTok);
    if (!elementType) {
        SrcLocation location = {NULL, elementTok.line, elementTok.column};
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "invalid element type inside array type annotation");
        return NULL;
    }

    while (peekToken(ctx).type == TOKEN_NEWLINE || peekToken(ctx).type == TOKEN_INDENT ||
           peekToken(ctx).type == TOKEN_DEDENT) {
        nextToken(ctx);
    }

    if (peekToken(ctx).type == TOKEN_QUESTION) {
        nextToken(ctx);
        elementType->typeAnnotation.isNullable = true;
    }

    while (peekToken(ctx).type == TOKEN_NEWLINE || peekToken(ctx).type == TOKEN_INDENT ||
           peekToken(ctx).type == TOKEN_DEDENT) {
        nextToken(ctx);
    }

    bool hasLength = false;
    int lengthValue = 0;
    char* lengthIdentifier = NULL;

    if (peekToken(ctx).type == TOKEN_COMMA) {
        nextToken(ctx);
        while (peekToken(ctx).type == TOKEN_NEWLINE || peekToken(ctx).type == TOKEN_INDENT ||
               peekToken(ctx).type == TOKEN_DEDENT) {
            nextToken(ctx);
        }

        Token lengthTok = nextToken(ctx);
        if (lengthTok.type == TOKEN_NUMBER) {
            char* text = copy_token_text(ctx, lengthTok);
            char* endptr = NULL;
            long parsed = strtol(text, &endptr, 10);
            if (!text || !endptr || *endptr != '\0' || parsed < 0 || parsed > INT_MAX) {
                SrcLocation location = {NULL, lengthTok.line, lengthTok.column};
                report_compile_error(E1006_INVALID_SYNTAX, location,
                                     "array length must be a non-negative integer literal within range");
                return NULL;
            }
            lengthValue = (int)parsed;
        } else if (lengthTok.type == TOKEN_IDENTIFIER) {
            lengthIdentifier = copy_token_text(ctx, lengthTok);
        } else {
            SrcLocation location = {NULL, lengthTok.line, lengthTok.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "expected a constant name or integer literal for the array length");
            return NULL;
        }

        hasLength = true;

        while (peekToken(ctx).type == TOKEN_NEWLINE || peekToken(ctx).type == TOKEN_INDENT ||
               peekToken(ctx).type == TOKEN_DEDENT) {
            nextToken(ctx);
        }
    }

    while (peekToken(ctx).type == TOKEN_NEWLINE || peekToken(ctx).type == TOKEN_INDENT ||
           peekToken(ctx).type == TOKEN_DEDENT) {
        nextToken(ctx);
    }

    Token closeTok = nextToken(ctx);
    if (closeTok.type != TOKEN_RIGHT_BRACKET) {
        SrcLocation location = {NULL, closeTok.line, closeTok.column};
        report_compile_error(E1020_MISSING_BRACKET, location,
                             "expected ']' to close this array type, but found %s instead.",
                             token_type_to_string(closeTok.type));
        return NULL;
    }

    ASTNode* typeNode = new_node(ctx);
    typeNode->type = NODE_TYPE;
    typeNode->typeAnnotation.name = NULL;
    typeNode->typeAnnotation.isNullable = false;
    typeNode->typeAnnotation.isArrayType = true;
    typeNode->typeAnnotation.arrayElementType = elementType;
    typeNode->typeAnnotation.arrayHasLength = hasLength;
    typeNode->typeAnnotation.arrayLength = lengthIdentifier ? 0 : lengthValue;
    typeNode->typeAnnotation.arrayLengthIdentifier = lengthIdentifier;
    typeNode->typeAnnotation.genericArgs = NULL;
    typeNode->typeAnnotation.genericArgCount = 0;
    typeNode->location.line = openToken.line;
    typeNode->location.column = openToken.column;
    typeNode->dataType = NULL;

    return typeNode;
}

static ASTNode* build_type_annotation_node(ParserContext* ctx, Token typeTok) {
    if (typeTok.type == TOKEN_LEFT_BRACKET) {
        return parse_array_type_annotation(ctx, typeTok);
    }

    if (!token_can_start_type(typeTok)) {
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
    typeNode->typeAnnotation.isArrayType = false;
    typeNode->typeAnnotation.arrayElementType = NULL;
    typeNode->typeAnnotation.arrayHasLength = false;
    typeNode->typeAnnotation.arrayLength = 0;
    typeNode->typeAnnotation.arrayLengthIdentifier = NULL;
    typeNode->typeAnnotation.genericArgs = NULL;
    typeNode->typeAnnotation.genericArgCount = 0;
    typeNode->location.line = typeTok.line;
    typeNode->location.column = typeTok.column;
    typeNode->dataType = NULL;

    return typeNode;
}

static ASTNode* parseTypeAnnotation(ParserContext* ctx) {
    Token typeTok = nextToken(ctx);
    ASTNode* typeNode = build_type_annotation_node(ctx, typeTok);
    if (!typeNode) {
        return NULL;
    }

    if (!typeNode->typeAnnotation.isArrayType && peekToken(ctx).type == TOKEN_LEFT_BRACKET) {
        nextToken(ctx);

        ASTNode** args = NULL;
        int argCount = 0;
        int argCapacity = 0;

        if (peekToken(ctx).type != TOKEN_RIGHT_BRACKET) {
            while (true) {
                ASTNode* arg = parseTypeAnnotation(ctx);
                if (!arg) {
                    return NULL;
                }

                if (argCount + 1 > argCapacity) {
                    int newCap = argCapacity == 0 ? 2 : argCapacity * 2;
                    ASTNode** newArgs = parser_arena_alloc(ctx, sizeof(ASTNode*) * newCap);
                    if (argCapacity > 0 && args) {
                        memcpy(newArgs, args, sizeof(ASTNode*) * argCount);
                    }
                    args = newArgs;
                    argCapacity = newCap;
                }

                args[argCount++] = arg;

                Token delim = peekToken(ctx);
                if (delim.type == TOKEN_COMMA) {
                    nextToken(ctx);
                    continue;
                }
                break;
            }
        }

        Token closeTok = nextToken(ctx);
        if (closeTok.type != TOKEN_RIGHT_BRACKET) {
            return NULL;
        }

        typeNode->typeAnnotation.genericArgs = args;
        typeNode->typeAnnotation.genericArgCount = argCount;
    }

    if (peekToken(ctx).type == TOKEN_QUESTION) {
        nextToken(ctx);
        typeNode->typeAnnotation.isNullable = true;
    }

    return typeNode;
}

static ASTNode* parseVariableDeclaration(ParserContext* ctx, bool isMutable, bool isPublic, Token nameToken) {

    ASTNode* typeNode = NULL;
    if (peekToken(ctx).type == TOKEN_COLON) {
        nextToken(ctx);
        typeNode = parseTypeAnnotation(ctx);
        if (!typeNode) return NULL;
    }

    Token assignToken = nextToken(ctx);
    bool usesDefine = (assignToken.type == TOKEN_DEFINE);
    if (usesDefine) {
        if (isMutable) {
            SrcLocation location = {NULL, assignToken.line, assignToken.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "mutable bindings must use '='; ':=' declares an immutable constant");
            return NULL;
        }
    } else if (assignToken.type != TOKEN_EQUAL) {
        return NULL;
    }

    ASTNode* initializer = parseExpression(ctx);
    if (!initializer) {
        return NULL;
    }

    // Suffix redundancy checks removed - type inference handles all type conflicts
    if (typeNode && initializer->type == NODE_LITERAL) {
        const char* declaredType = typeNode->typeAnnotation.name;
        ValueType literalType = initializer->literal.value.type;
        
        // Check for type mismatches (without suffix logic)
        {
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
            else if (strcmp(declaredType, "u32") == 0 && literalType == VAL_I64) {
                int64_t value = initializer->literal.value.as.i64;
                if (value >= 0 && value <= (int64_t)UINT32_MAX) {
                    mismatch = false;
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
            else if (strcmp(declaredType, "u64") == 0 && literalType == VAL_I64) {
                int64_t value = initializer->literal.value.as.i64;
                if (value >= 0) {
                    mismatch = false;
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

    // Validate variable name follows Orus conventions
    if (!is_valid_variable_name(name)) {
        const char* reason = get_variable_name_violation_reason(name);
        SrcLocation location = {NULL, nameToken.line, nameToken.column};
        report_invalid_variable_name(location, name, reason);
        return NULL;
    }

    if (ctx->block_depth == 0 && !isMutable && usesDefine) {
        if (!is_valid_constant_name(name)) {
            const char* reason = get_constant_name_violation_reason(name);
            if (!reason) {
                reason = "module constants must use SCREAMING_SNAKE_CASE (uppercase letters, digits, and underscores)";
            }
            SrcLocation location = {NULL, nameToken.line, nameToken.column};
            report_invalid_variable_name(location, name, reason);
            return NULL;
        }
    }

    varNode->varDecl.name = name;
    varNode->varDecl.isPublic = isPublic;
    varNode->varDecl.isGlobal = (ctx->block_depth == 0);
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = typeNode;
    varNode->varDecl.isMutable = isMutable;

    // For multiple variable declarations separated by commas,
    // only parse the first one and let the main parser handle the rest
    return varNode;
}

static ASTNode* parseDestructuringAssignment(ParserContext* ctx, Token firstToken) {
    char** names = NULL;
    SrcLocation* nameLocations = NULL;
    int count = 0;
    int capacity = 0;

    Token currentToken = firstToken;
    nextToken(ctx); // consume first identifier

    while (true) {
        if (count + 1 > capacity) {
            int newCap = (capacity == 0) ? 4 : (capacity * 2);
            char** newNames = parser_arena_alloc(ctx, sizeof(char*) * newCap);
            SrcLocation* newLocations = parser_arena_alloc(ctx, sizeof(SrcLocation) * newCap);
            if (capacity > 0 && names) {
                memcpy(newNames, names, sizeof(char*) * (size_t)count);
            }
            if (capacity > 0 && nameLocations) {
                memcpy(newLocations, nameLocations, sizeof(SrcLocation) * (size_t)count);
            }
            names = newNames;
            nameLocations = newLocations;
            capacity = newCap;
        }

        char* name = copy_token_text(ctx, currentToken);
        if (!is_valid_variable_name(name)) {
            const char* reason = get_variable_name_violation_reason(name);
            SrcLocation location = {NULL, currentToken.line, currentToken.column};
            report_invalid_variable_name(location, name, reason);
            return NULL;
        }

        names[count] = name;
        nameLocations[count].file = NULL;
        nameLocations[count].line = currentToken.line;
        nameLocations[count].column = currentToken.column;
        count++;

        Token separator = peekToken(ctx);
        if (separator.type != TOKEN_COMMA) {
            break;
        }

        nextToken(ctx); // consume comma
        Token nextNameTok = nextToken(ctx);
        if (nextNameTok.type != TOKEN_IDENTIFIER) {
            SrcLocation location = {NULL, nextNameTok.line, nextNameTok.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "expected identifier in destructuring assignment");
            return NULL;
        }
        currentToken = nextNameTok;
    }

    if (count == 0) {
        return NULL;
    }

    Token equalTok = nextToken(ctx);
    if (equalTok.type != TOKEN_EQUAL) {
        SrcLocation location = {NULL, equalTok.line, equalTok.column};
        report_compile_error(E1006_INVALID_SYNTAX, location,
                             "expected '=' after destructuring pattern");
        return NULL;
    }

    ASTNode* initializer = parseExpression(ctx);
    if (!initializer) {
        return NULL;
    }

    ASTNode** statements = NULL;
    int stmtCount = 0;
    int stmtCapacity = 0;

    char tempNameBuffer[64];
    snprintf(tempNameBuffer, sizeof(tempNameBuffer), "_tuple_tmp%d", tuple_temp_counter++);
    size_t tempLen = strlen(tempNameBuffer);
    char* tempName = parser_arena_alloc(ctx, tempLen + 1);
    memcpy(tempName, tempNameBuffer, tempLen + 1);

    ASTNode* tempAssign = new_node(ctx);
    tempAssign->type = NODE_ASSIGN;
    tempAssign->assign.name = tempName;
    tempAssign->assign.value = initializer;
    tempAssign->location = nameLocations[0];
    tempAssign->dataType = NULL;
    addStatement(ctx, &statements, &stmtCount, &stmtCapacity, tempAssign);

    for (int i = 0; i < count; i++) {
        ASTNode* tempIdentifier = create_identifier_node(ctx, tempName, nameLocations[i]);

        ASTNode* indexLiteral = new_node(ctx);
        indexLiteral->type = NODE_LITERAL;
        indexLiteral->literal.value.type = VAL_I32;
        indexLiteral->literal.value.as.i32 = i;
        indexLiteral->literal.hasExplicitSuffix = false;
        indexLiteral->location = nameLocations[i];
        indexLiteral->dataType = NULL;

        ASTNode* indexExpr = new_node(ctx);
        indexExpr->type = NODE_INDEX_ACCESS;
        indexExpr->indexAccess.array = tempIdentifier;
        indexExpr->indexAccess.index = indexLiteral;
        indexExpr->location = nameLocations[i];
        indexExpr->dataType = NULL;

        ASTNode* assignNode = new_node(ctx);
        assignNode->type = NODE_ASSIGN;
        assignNode->assign.name = names[i];
        assignNode->assign.value = indexExpr;
        assignNode->location = nameLocations[i];
        assignNode->dataType = NULL;
        addStatement(ctx, &statements, &stmtCount, &stmtCapacity, assignNode);
    }

    ASTNode* block = new_node(ctx);
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = stmtCount;
    block->block.createsScope = false;
    block->location = nameLocations[0];
    block->dataType = NULL;

    return block;
}

static ASTNode* parseImportStatement(ParserContext* ctx) {
    Token importTok = nextToken(ctx);
    if (importTok.type != TOKEN_IMPORT) {
        return NULL;
    }

    Token moduleTok = nextToken(ctx);
    char* moduleName = parse_qualified_name(ctx, moduleTok, "expected module name after 'use'");
    if (!moduleName) {
        return NULL;
    }

    char* moduleAlias = NULL;
    if (peekToken(ctx).type == TOKEN_AS) {
        nextToken(ctx); // consume 'as'
        Token aliasTok = nextToken(ctx);
        if (aliasTok.type != TOKEN_IDENTIFIER) {
            SrcLocation location = {NULL, aliasTok.line, aliasTok.column};
            report_compile_error(E1006_INVALID_SYNTAX, location, "expected identifier after 'as'");
            return NULL;
        }

        int aliasLen = aliasTok.length;
        moduleAlias = parser_arena_alloc(ctx, aliasLen + 1);
        strncpy(moduleAlias, aliasTok.start, aliasLen);
        moduleAlias[aliasLen] = '\0';
    }

    bool importAll = false;
    bool importModule = true;
    ImportSymbol* finalSymbols = NULL;
    int symbolCount = 0;

    if (peekToken(ctx).type == TOKEN_COLON) {
        nextToken(ctx); // consume ':'
        importModule = false;

        Token nextTok = peekToken(ctx);
        if (nextTok.type == TOKEN_STAR) {
            nextToken(ctx);
            importAll = true;
        } else {
            ImportSymbol* tempSymbols = NULL;
            int tempCount = 0;
            int tempCapacity = 0;

            while (true) {
                Token symTok = nextToken(ctx);
                if (symTok.type != TOKEN_IDENTIFIER) {
                    SrcLocation location = {NULL, symTok.line, symTok.column};
                    report_compile_error(E1006_INVALID_SYNTAX, location, "expected symbol name in use list");
                    free(tempSymbols);
                    return NULL;
                }

                int symLen = symTok.length;
                char* symbolName = parser_arena_alloc(ctx, symLen + 1);
                strncpy(symbolName, symTok.start, symLen);
                symbolName[symLen] = '\0';

                char* aliasName = NULL;
                if (peekToken(ctx).type == TOKEN_AS) {
                    nextToken(ctx); // consume 'as'
                    Token aliasTok = nextToken(ctx);
                    if (aliasTok.type != TOKEN_IDENTIFIER) {
                        SrcLocation location = {NULL, aliasTok.line, aliasTok.column};
                        report_compile_error(E1006_INVALID_SYNTAX, location, "expected alias name after 'as'");
                        free(tempSymbols);
                        return NULL;
                    }

                    int aliasLen = aliasTok.length;
                    aliasName = parser_arena_alloc(ctx, aliasLen + 1);
                    strncpy(aliasName, aliasTok.start, aliasLen);
                    aliasName[aliasLen] = '\0';
                }

                if (tempCount >= tempCapacity) {
                    tempCapacity = tempCapacity == 0 ? 4 : tempCapacity * 2;
                    ImportSymbol* resized = realloc(tempSymbols, sizeof(ImportSymbol) * (size_t)tempCapacity);
                    if (!resized) {
                        free(tempSymbols);
                        return NULL;
                    }
                    tempSymbols = resized;
                }

                tempSymbols[tempCount].name = symbolName;
                tempSymbols[tempCount].alias = aliasName;
                tempCount++;

                if (peekToken(ctx).type == TOKEN_COMMA) {
                    nextToken(ctx);
                    continue;
                }
                break;
            }

            if (tempCount == 0) {
                SrcLocation location = {NULL, moduleTok.line, moduleTok.column};
                report_compile_error(E1006_INVALID_SYNTAX, location, "expected at least one symbol after ':'");
                free(tempSymbols);
                return NULL;
            }

            symbolCount = tempCount;
            finalSymbols = parser_arena_alloc(ctx, sizeof(ImportSymbol) * (size_t)symbolCount);
            for (int i = 0; i < symbolCount; i++) {
                finalSymbols[i] = tempSymbols[i];
            }
            free(tempSymbols);
        }
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_IMPORT;
    node->location.line = importTok.line;
    node->location.column = importTok.column;
    node->dataType = NULL;
    node->import.moduleName = moduleName;
    node->import.moduleAlias = moduleAlias;
    node->import.symbols = finalSymbols;
    node->import.symbolCount = symbolCount;
    node->import.importAll = importAll;
    node->import.importModule = importModule;

    return node;
}

static ASTNode* parseAssignOrVarList(ParserContext* ctx, bool isMutable, bool isPublic, Token nameToken) {
    Token opToken = nextToken(ctx);
    bool usesDefine = (opToken.type == TOKEN_DEFINE);

    // Handle compound assignments (+=, -=, *=, /=, %=)
    if (opToken.type == TOKEN_PLUS_EQUAL || opToken.type == TOKEN_MINUS_EQUAL ||
        opToken.type == TOKEN_STAR_EQUAL || opToken.type == TOKEN_SLASH_EQUAL ||
        opToken.type == TOKEN_MODULO_EQUAL) {

        // Compound assignments are only valid for existing variables, not declarations
        if (isMutable) {
            // Provide a clear diagnostic instead of silently failing
            char* name = copy_token_text(ctx, nameToken);
            SrcLocation location = {NULL, nameToken.line, nameToken.column};
            report_invalid_multiple_declaration(location, name,
                                                "compound assignments cannot declare a new variable. Remove 'mut' to update an existing binding.");
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
        
        // Validate variable name even for compound assignment
        if (!is_valid_variable_name(name)) {
            const char* reason = get_variable_name_violation_reason(name);
            SrcLocation location = {NULL, nameToken.line, nameToken.column};
            report_invalid_variable_name(location, name, reason);
            return NULL;
        }
        
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
    if (usesDefine) {
        if (isMutable) {
            SrcLocation location = {NULL, opToken.line, opToken.column};
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "mutable bindings must use '='; ':=' declares an immutable constant");
            return NULL;
        }
    } else if (opToken.type != TOKEN_EQUAL) {
        return NULL;
    }
    ASTNode* initializer = parseExpression(ctx);
    if (!initializer) return NULL;

    // For multiple variable declarations separated by commas,
    // only parse the first one and let the main parser handle the rest
    if (!usesDefine && peekToken(ctx).type != TOKEN_COMMA && !isMutable) {
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
    varNode->varDecl.isPublic = isPublic;
    varNode->varDecl.isGlobal = (ctx->block_depth == 0);
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = NULL;
    varNode->varDecl.isMutable = isMutable;

    // Don't consume the comma - let the main parser handle subsequent declarations
    return varNode;
}

static ASTNode* parseInlineBlock(ParserContext* ctx) {
    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;
    bool firstStatement = true;

    while (true) {
        while (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }

        Token lookahead = peekToken(ctx);
        if (!firstStatement && (lookahead.type == TOKEN_EOF || lookahead.type == TOKEN_DEDENT ||
                                lookahead.type == TOKEN_ELSE || lookahead.type == TOKEN_ELIF ||
                                lookahead.type == TOKEN_CATCH)) {
            break;
        }

        ASTNode* stmt = parseStatement(ctx);
        if (!stmt) {
            return NULL;
        }

        addStatement(ctx, &statements, &count, &capacity, stmt);
        firstStatement = false;

        while (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }

        if (peekToken(ctx).type == TOKEN_SEMICOLON) {
            nextToken(ctx);
            continue;
        }

        break;
    }

    ASTNode* block = new_node(ctx);
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = count;
    block->block.createsScope = false;
    if (count > 0 && statements) {
        block->location = statements[0]->location;
    } else {
        block->location.line = 0;
        block->location.column = 0;
    }
    block->dataType = NULL;
    return block;
}

static ASTNode* parseBlock(ParserContext* ctx) {
    extern VM vm;
    if (vm.devMode) {
        fprintf(stderr, "Debug: Entering parseBlock\n");
    }

    ctx->block_depth++;

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
        if (t.type == TOKEN_INDENT) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1008_INVALID_INDENTATION, location,
                                 "It looks like this line is indented, but there's no open block above it.");
            ctx->block_depth--;
            return NULL;
        }
        if (t.type == TOKEN_SEMICOLON) {
            nextToken(ctx);
            continue;
        }
        ASTNode* stmt = parseStatement(ctx);
        if (!stmt) {
            if (vm.devMode) {
                fprintf(stderr, "Debug: parseBlock failed to parse statement\n");
            }
            ctx->block_depth--;
            return NULL;
        }
        addStatement(ctx, &statements, &count, &capacity, stmt);
        t = peekToken(ctx);
        if (t.type == TOKEN_NEWLINE) {
            nextToken(ctx);
        } else if (t.type == TOKEN_SEMICOLON) {
            nextToken(ctx);
        }
    }
    Token dedent = nextToken(ctx);
    if (dedent.type != TOKEN_DEDENT) {
        ctx->block_depth--;
        return NULL;
    }

    ASTNode* block = new_node(ctx);
    block->type = NODE_BLOCK;
    block->block.statements = statements;
    block->block.count = count;
    block->block.createsScope = true;
    block->location.line = dedent.line;
    block->location.column = dedent.column;
    block->dataType = NULL;
    ctx->block_depth--;
    return block;
}

static ASTNode* parseIfStatement(ParserContext* ctx) {
    Token ifTok = nextToken(ctx);
    if (ifTok.type != TOKEN_IF && ifTok.type != TOKEN_ELIF) return NULL;

    // Parse condition with error checking
    ASTNode* condition = parseExpression(ctx);
    if (!condition) {
        SrcLocation location = {NULL, ifTok.line, ifTok.column};
        report_empty_condition(location, ifTok.type == TOKEN_IF ? "if" : "elif");
        return NULL;
    }

    // Check for assignment in condition (= instead of ==)
    if (condition->type == NODE_ASSIGN) {
        SrcLocation location = {NULL, condition->location.line, condition->location.column};
        report_assignment_in_condition(location, ifTok.type == TOKEN_IF ? "if" : "elif");
        return NULL;
    }

    // Check for missing colon
    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        SrcLocation location = {NULL, colon.line, colon.column};
        report_missing_colon(location, ifTok.type == TOKEN_IF ? "if" : "elif");
        return NULL;
    }
    
    // Check if this is a single-line if or block if
    Token next = peekToken(ctx);
    ASTNode* thenBranch = NULL;
    
    if (next.type == TOKEN_NEWLINE) {
        // Block-style if statement: if condition:\n    statement
        nextToken(ctx); // consume newline
        Token indentToken = consume_indent_token(ctx);
        if (indentToken.type != TOKEN_INDENT) {
            SrcLocation location = {NULL, indentToken.line, indentToken.column};
            report_invalid_indentation(location, "if", 4, 0); // Assuming 4-space indentation
            return NULL;
        }
        thenBranch = parseBlock(ctx);
        if (!thenBranch) {
            SrcLocation location = {NULL, ifTok.line, ifTok.column};
            report_empty_block(location, "if");
            return NULL;
        }
    } else {
        // Single-line if statement: if condition: statement
        thenBranch = parseStatement(ctx);
        if (!thenBranch) {
            SrcLocation location = {NULL, ifTok.line, ifTok.column};
            report_empty_block(location, "if");
            return NULL;
        }
    }

    if (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    ASTNode* elseBranch = NULL;
    Token nextTok = peekToken(ctx);
    if (nextTok.type == TOKEN_ELIF) {
        elseBranch = parseIfStatement(ctx);
    } else if (nextTok.type == TOKEN_ELSE) {
        nextToken(ctx); // consume else
        Token elseColon = nextToken(ctx);
        if (elseColon.type != TOKEN_COLON) {
            SrcLocation location = {NULL, elseColon.line, elseColon.column};
            report_missing_colon(location, "else");
            return NULL;
        }
        
        // Check if this is a single-line else or block else
        Token afterColon = peekToken(ctx);
        if (afterColon.type == TOKEN_NEWLINE) {
            // Block-style else: else:\n    statement
            nextToken(ctx); // consume newline
            Token indentTok = consume_indent_token(ctx);
            if (indentTok.type != TOKEN_INDENT) return NULL;
            elseBranch = parseBlock(ctx);
            if (!elseBranch) return NULL;
        } else {
            // Single-line else: else: statement
            elseBranch = parseStatement(ctx);
            if (!elseBranch) return NULL;
        }
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

    // Parse condition with error checking
    ASTNode* condition = parseExpression(ctx);
    if (!condition) {
        SrcLocation location = {NULL, whileTok.line, whileTok.column};
        report_empty_condition(location, "while");
        return NULL;
    }

    // Check for assignment in condition (= instead of ==)
    if (condition->type == NODE_ASSIGN) {
        SrcLocation location = {NULL, condition->location.line, condition->location.column};
        report_assignment_in_condition(location, "while");
        return NULL;
    }

    // Check for missing colon
    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        SrcLocation location = {NULL, colon.line, colon.column};
        report_missing_colon(location, "while");
        return NULL;
    }
    
    // Check if this is a single-line while or block while
    Token next = peekToken(ctx);
    ASTNode* body = NULL;
    bool entered_loop = false;

    if (next.type == TOKEN_NEWLINE) {
        // Block-style while: while condition:\n    statement
        nextToken(ctx); // consume newline
        Token indentTok = consume_indent_token(ctx);
        if (indentTok.type != TOKEN_INDENT) return NULL;
        parser_enter_loop(ctx);
        entered_loop = true;
        body = parseBlock(ctx);
    } else {
        // Single-line while: while condition: statement
        parser_enter_loop(ctx);
        entered_loop = true;
        body = parseInlineBlock(ctx);
    }

    if (entered_loop) {
        parser_leave_loop(ctx);
    }

    if (!body) {
        return NULL;
    }
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

static ASTNode* parseTryStatement(ParserContext* ctx) {
    Token tryTok = nextToken(ctx);
    if (tryTok.type != TOKEN_TRY) return NULL;

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        SrcLocation location = {NULL, colon.line, colon.column};
        report_missing_colon(location, "try");
        return NULL;
    }

    ASTNode* tryBlock = NULL;
    Token next = peekToken(ctx);
    if (next.type == TOKEN_NEWLINE) {
        nextToken(ctx);  // consume newline
        Token indentToken = consume_indent_token(ctx);
        if (indentToken.type != TOKEN_INDENT) {
            SrcLocation location = {NULL, indentToken.line, indentToken.column};
            report_invalid_indentation(location, "try", 4, 0);
            return NULL;
        }
        tryBlock = parseBlock(ctx);
        if (!tryBlock) {
            SrcLocation location = {NULL, tryTok.line, tryTok.column};
            report_empty_block(location, "try");
            return NULL;
        }
    } else {
        tryBlock = parseStatement(ctx);
        if (!tryBlock) {
            SrcLocation location = {NULL, tryTok.line, tryTok.column};
            report_empty_block(location, "try");
            return NULL;
        }
    }

    Token lookahead = peekToken(ctx);
    while (lookahead.type == TOKEN_NEWLINE) {
        nextToken(ctx);
        lookahead = peekToken(ctx);
    }

    if (lookahead.type != TOKEN_CATCH) {
        SrcLocation location = {NULL, lookahead.line, lookahead.column};
        report_compile_error(E1006_INVALID_SYNTAX, location, "expected 'catch' after try block");
        return NULL;
    }

    Token catchTok = nextToken(ctx); // consume 'catch'

    Token nameTok = nextToken(ctx);
    char* catchName = NULL;
    Token catchColon = nameTok;
    if (nameTok.type == TOKEN_IDENTIFIER) {
        catchName = copy_token_text(ctx, nameTok);
        catchColon = nextToken(ctx);
    }

    if (catchColon.type != TOKEN_COLON) {
        SrcLocation location = {NULL, catchColon.line, catchColon.column};
        report_missing_colon(location, "catch");
        return NULL;
    }

    ASTNode* catchBlock = NULL;
    Token afterCatch = peekToken(ctx);
    if (afterCatch.type == TOKEN_NEWLINE) {
        nextToken(ctx);
        Token indentToken = consume_indent_token(ctx);
        if (indentToken.type != TOKEN_INDENT) {
            SrcLocation location = {NULL, indentToken.line, indentToken.column};
            report_invalid_indentation(location, "catch", 4, 0);
            return NULL;
        }
        catchBlock = parseBlock(ctx);
        if (!catchBlock) {
            SrcLocation location = {NULL, catchTok.line, catchTok.column};
            report_empty_block(location, "catch");
            return NULL;
        }
    } else {
        catchBlock = parseStatement(ctx);
        if (!catchBlock) {
            SrcLocation location = {NULL, catchTok.line, catchTok.column};
            report_empty_block(location, "catch");
            return NULL;
        }
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_TRY;
    node->tryStmt.tryBlock = tryBlock;
    node->tryStmt.catchBlock = catchBlock;
    node->tryStmt.catchVar = catchName;
    node->location.line = tryTok.line;
    node->location.column = tryTok.column;
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
        SrcLocation location = {NULL, forTok.line, forTok.column};
        report_invalid_loop_variable(location, "missing", "loop variable name is required after 'for'");
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IDENTIFIER after 'for', got %d\n", nameTok.type);
        }
        return NULL;
    }
    
    // Validate loop variable name
    int len = nameTok.length;
    char* name = parser_arena_alloc(ctx, len + 1);
    strncpy(name, nameTok.start, len);
    name[len] = '\0';
    
    // Check for valid variable name conventions
    if (name[0] >= '0' && name[0] <= '9') {
        SrcLocation location = {NULL, nameTok.line, nameTok.column};
        report_invalid_loop_variable(location, name, "variable names cannot start with a digit");
        return NULL;
    }

    Token inTok = nextToken(ctx);
    if (inTok.type != TOKEN_IN) {
        SrcLocation location = {NULL, nameTok.line, nameTok.column};
        report_invalid_range_syntax(location, "for loop", "expected 'in' after loop variable");
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_IN after identifier, got %d\n", inTok.type);
        }
        return NULL;
    }

    ASTNode* first = parseExpression(ctx);
    if (!first) {
        SrcLocation location = {NULL, inTok.line, inTok.column};
        report_invalid_range_syntax(location, "missing", "range or iterable expression is required after 'in'");
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
            SrcLocation location = {NULL, forTok.line, forTok.column};
            report_invalid_range_syntax(location, "incomplete", "range end value is required after '..'");
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
                SrcLocation location = {NULL, forTok.line, forTok.column};
                report_invalid_range_syntax(location, "incomplete", "step value is required after second '..'");
                if (vm.devMode) {
                    fprintf(stderr, "Debug: Failed to parse step expression in range\n");
                }
                return NULL;
            }
        }
    }

    Token colon = nextToken(ctx);
    if (colon.type != TOKEN_COLON) {
        SrcLocation location = {NULL, forTok.line, forTok.column};
        report_missing_colon(location, "for");
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_COLON after range, got %d\n", colon.type);
        }
        return NULL;
    }
    
    Token newline = nextToken(ctx);
    if (newline.type != TOKEN_NEWLINE) {
        SrcLocation location = {NULL, colon.line, colon.column};
        report_invalid_indentation(location, "for", 0, -1);
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_NEWLINE after colon, got %d\n", newline.type);
        }
        return NULL;
    }
    
    Token indent = consume_indent_token(ctx);
    if (indent.type != TOKEN_INDENT) {
        SrcLocation location = {NULL, newline.line, newline.column};
        report_invalid_indentation(location, "for", 4, 0);
        if (vm.devMode) {
            fprintf(stderr, "Debug: Expected TOKEN_INDENT after newline, got %d\n", indent.type);
        }
        return NULL;
    }

    parser_enter_loop(ctx);
    ASTNode* body = parseBlock(ctx);
    parser_leave_loop(ctx);
    if (!body) {
        SrcLocation location = {NULL, indent.line, indent.column};
        report_empty_block(location, "for loop");
        if (vm.devMode) {
            fprintf(stderr, "Debug: Failed to parse body block in for loop\n");
        }
        return NULL;
    }
    if (peekToken(ctx).type == TOKEN_NEWLINE) nextToken(ctx);

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

static ASTNode* parsePassStatement(ParserContext* ctx) {
    Token passToken = nextToken(ctx);
    if (passToken.type != TOKEN_PASS) {
        return NULL;
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_PASS;
    node->location.line = passToken.line;
    node->location.column = passToken.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseBreakStatement(ParserContext* ctx) {
    Token breakToken = nextToken(ctx);
    if (breakToken.type != TOKEN_BREAK) {
        return NULL;
    }
    
    // Check if break is used outside of loop context
    if (!is_valid_break_continue_context()) {
        SrcLocation location = {NULL, breakToken.line, breakToken.column};
        report_break_outside_loop(location);
        return NULL;
    }
    
    ASTNode* node = new_node(ctx);
    node->type = NODE_BREAK;
    node->location.line = breakToken.line;
    node->location.column = breakToken.column;
    node->dataType = NULL;
    node->breakStmt.label = NULL;
    
    // Handle labeled break
    if (peekToken(ctx).type == TOKEN_APOSTROPHE) {
        nextToken(ctx);
        Token labelTok = nextToken(ctx);
        if (labelTok.type != TOKEN_IDENTIFIER) {
            SrcLocation location = {NULL, labelTok.line, labelTok.column};
            report_invalid_loop_variable(location, "label", "expected identifier after apostrophe");
            return NULL;
        }
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
    
    // Check if continue is used outside of loop context
    if (!is_valid_break_continue_context()) {
        SrcLocation location = {NULL, continueToken.line, continueToken.column};
        report_continue_outside_loop(location);
        return NULL;
    }
    
    ASTNode* node = new_node(ctx);
    node->type = NODE_CONTINUE;
    node->location.line = continueToken.line;
    node->location.column = continueToken.column;
    node->dataType = NULL;
    node->continueStmt.label = NULL;
    
    // Handle labeled continue
    if (peekToken(ctx).type == TOKEN_APOSTROPHE) {
        nextToken(ctx);
        Token labelTok = nextToken(ctx);
        if (labelTok.type != TOKEN_IDENTIFIER) {
            SrcLocation location = {NULL, labelTok.line, labelTok.column};
            report_invalid_loop_variable(location, "label", "expected identifier after apostrophe");
            return NULL;
        }
        int len = labelTok.length;
        char* label = parser_arena_alloc(ctx, len + 1);
        strncpy(label, labelTok.start, len);
        label[len] = '\0';
        node->continueStmt.label = label;
    }
    return node;
}

static ASTNode* parseAssignment(ParserContext* ctx);

static ASTNode* parseExpression(ParserContext* ctx) { 
    return parseAssignment(ctx); 
}

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
        t == TOKEN_STAR_EQUAL || t == TOKEN_SLASH_EQUAL || t == TOKEN_MODULO_EQUAL) {
        nextToken(ctx);
        ASTNode* value = NULL;

        if (t == TOKEN_EQUAL) {
            // Use full expression parsing so constructs like
            // x = cond ? a : b are handled correctly
            value = parseAssignment(ctx);
        } else {
            ASTNode* right = parseAssignment(ctx);
            if (!right) return NULL;
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
                case TOKEN_MODULO_EQUAL:
                    binary->binary.op = "%";
                    break;
                default:
                    binary->binary.op = "+";
                    break;
            }
            value = binary;
        }
        if (!value) return NULL;

        if (left->type == NODE_IDENTIFIER) {
            ASTNode* node = new_node(ctx);
            node->type = NODE_ASSIGN;
            node->assign.name = left->identifier.name;
            node->assign.value = value;
            node->location = left->location;
            node->dataType = NULL;
            return node;
        } else if (left->type == NODE_MEMBER_ACCESS) {
            ASTNode* node = new_node(ctx);
            node->type = NODE_MEMBER_ASSIGN;
            node->memberAssign.target = left;
            node->memberAssign.value = value;
            node->location = left->location;
            node->dataType = NULL;
            return node;
        }

        if (t == TOKEN_EQUAL && left->type == NODE_INDEX_ACCESS) {
            ASTNode* node = new_node(ctx);
            node->type = NODE_ARRAY_ASSIGN;
            node->arrayAssign.target = left;
            node->arrayAssign.value = value;
            node->location = left->location;
            node->dataType = NULL;
            return node;
        }

        return NULL;
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
            if (typeToken.type != TOKEN_IDENTIFIER) {
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
            targetType->typeAnnotation.isNullable = false;
            targetType->typeAnnotation.isArrayType = false;
            targetType->typeAnnotation.arrayElementType = NULL;
            targetType->typeAnnotation.arrayHasLength = false;
            targetType->typeAnnotation.arrayLength = 0;
            targetType->typeAnnotation.arrayLengthIdentifier = NULL;
            targetType->location.line = typeToken.line;
            targetType->location.column = typeToken.column;
            targetType->dataType = NULL;

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
static bool token_is_numeric_suffix(const Token* token) {
    if (!token) {
        return false;
    }
    return token_text_equals(token, "i32") || token_text_equals(token, "i64") ||
           token_text_equals(token, "u32") || token_text_equals(token, "u64") ||
           token_text_equals(token, "f64");
}

static bool tokens_are_adjacent(const Token* first, const Token* second) {
    if (!first || !second) {
        return false;
    }
    if (first->line != second->line) {
        return false;
    }
    int first_end_column = first->column + first->length;
    return first_end_column == second->column;
}

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

// detectNumberSuffix function removed - type inference handles numeric types

static bool isFloatingPointNumber(const char* numStr, int length) {
    if (length >= 2 && numStr[0] == '0' && (numStr[1] == 'x' || numStr[1] == 'X')) {
        return false;
    }
    for (int i = 0; i < length; i++) {
        if (numStr[i] == '.' || numStr[i] == 'e' || numStr[i] == 'E') {
            return true;
        }
    }
    return false;
}

static Value parseNumberValue(const char* numStr, int length) {
    // Check if it's a floating point number
    if (isFloatingPointNumber(numStr, length)) {
        double val = strtod(numStr, NULL);
        return F64_VAL(val);
    } else {
        // Integer - use i32 as default, let type inference decide if promotion needed
        long long value = strtoll(numStr, NULL, 0);
        bool is_hex_literal = (length >= 2 && numStr[0] == '0' &&
                               (numStr[1] == 'x' || numStr[1] == 'X'));
        if (is_hex_literal) {
            unsigned long long uvalue = strtoull(numStr, NULL, 0);
            if (uvalue <= UINT32_MAX) {
                return I32_VAL((int32_t)(uint32_t)uvalue);
            }
        }
        if (value > INT32_MAX || value < INT32_MIN) {
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
    // Parse the numeric value (no suffix handling - type inference decides)
    node->literal.value = parseNumberValue(numStr, length);
    node->literal.hasExplicitSuffix = false; // No suffixes in language
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;

    Token suffix = peekToken(ctx);
    if (token_is_numeric_suffix(&suffix) && tokens_are_adjacent(&token, &suffix)) {
        nextToken(ctx);
        node->literal.hasExplicitSuffix = true;

        bool conversion_ok = true;
        Value converted = node->literal.value;

        if (token_text_equals(&suffix, "i32")) {
            int64_t value = 0;
            if (converted.type == VAL_I32) {
                value = AS_I32(converted);
            } else if (converted.type == VAL_I64) {
                value = AS_I64(converted);
            } else if (converted.type == VAL_F64) {
                double d = AS_F64(converted);
                if (d < (double)INT32_MIN || d > (double)INT32_MAX ||
                    (double)(int32_t)d != d) {
                    conversion_ok = false;
                } else {
                    value = (int32_t)d;
                }
            } else {
                conversion_ok = false;
            }

            if (conversion_ok) {
                if (value < INT32_MIN || value > INT32_MAX) {
                    conversion_ok = false;
                } else {
                    converted = I32_VAL((int32_t)value);
                }
            }
        } else if (token_text_equals(&suffix, "i64")) {
            int64_t value = 0;
            if (converted.type == VAL_I32) {
                value = AS_I32(converted);
            } else if (converted.type == VAL_I64) {
                value = AS_I64(converted);
            } else if (converted.type == VAL_F64) {
                double d = AS_F64(converted);
                double truncated = (double)(int64_t)d;
                if (truncated != d) {
                    conversion_ok = false;
                } else {
                    value = (int64_t)d;
                }
            } else {
                conversion_ok = false;
            }

            if (conversion_ok) {
                converted = I64_VAL(value);
            }
        } else if (token_text_equals(&suffix, "u32")) {
            uint64_t value = 0;
            if (converted.type == VAL_I32) {
                int32_t v = AS_I32(converted);
                if (v < 0) {
                    conversion_ok = false;
                } else {
                    value = (uint32_t)v;
                }
            } else if (converted.type == VAL_I64) {
                int64_t v = AS_I64(converted);
                if (v < 0 || v > (int64_t)UINT32_MAX) {
                    conversion_ok = false;
                } else {
                    value = (uint32_t)v;
                }
            } else if (converted.type == VAL_F64) {
                double d = AS_F64(converted);
                if (d < 0.0 || d > (double)UINT32_MAX ||
                    (double)(uint32_t)d != d) {
                    conversion_ok = false;
                } else {
                    value = (uint32_t)d;
                }
            } else {
                conversion_ok = false;
            }

            if (conversion_ok) {
                converted = U32_VAL((uint32_t)value);
            }
        } else if (token_text_equals(&suffix, "u64")) {
            uint64_t value = 0;
            if (converted.type == VAL_I32) {
                int32_t v = AS_I32(converted);
                if (v < 0) {
                    conversion_ok = false;
                } else {
                    value = (uint64_t)v;
                }
            } else if (converted.type == VAL_I64) {
                int64_t v = AS_I64(converted);
                if (v < 0) {
                    conversion_ok = false;
                } else {
                    value = (uint64_t)v;
                }
            } else if (converted.type == VAL_F64) {
                double d = AS_F64(converted);
                if (d < 0.0 || d > (double)UINT64_MAX) {
                    conversion_ok = false;
                } else {
                    double truncated = (double)(uint64_t)d;
                    if (truncated != d) {
                        conversion_ok = false;
                    } else {
                        value = (uint64_t)d;
                    }
                }
            } else {
                conversion_ok = false;
            }

            if (conversion_ok) {
                converted = U64_VAL(value);
            }
        } else if (token_text_equals(&suffix, "f64")) {
            if (converted.type == VAL_F64) {
                // Already floating point
            } else if (converted.type == VAL_I32) {
                converted = F64_VAL((double)AS_I32(converted));
            } else if (converted.type == VAL_I64) {
                converted = F64_VAL((double)AS_I64(converted));
            } else {
                conversion_ok = false;
            }
        } else {
            conversion_ok = false;
        }

        if (!conversion_ok) {
            SrcLocation location = {NULL, suffix.line, suffix.column};
            char* suffix_text = copy_token_text(ctx, suffix);
            report_compile_error(E1006_INVALID_SYNTAX, location,
                                 "invalid numeric literal suffix '%s'",
                                 suffix_text ? suffix_text : "<invalid>");
            node->literal.value = I32_VAL(0);
        } else {
            node->literal.value = converted;
        }
    }

    return node;
}

static ASTNode* parseStringLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;

    // Process escape sequences in string content
    int rawLen = token.length >= 2 ? token.length - 2 : 0; // Remove quotes
    const char* raw = token.start + 1;                      // Skip opening quote

    StringBuilder* sb = createStringBuilder((size_t)rawLen + 1);

    for (int i = 0; i < rawLen; i++) {
        char current = raw[i];
        if (current == '\\' && i + 1 < rawLen) {
            char escape = raw[i + 1];
            switch (escape) {
                case 'n':
                    current = '\n';
                    i++;
                    break;
                case 't':
                    current = '\t';
                    i++;
                    break;
                case '\\':
                    current = '\\';
                    i++;
                    break;
                case '"':
                    current = '"';
                    i++;
                    break;
                case 'r':
                    current = '\r';
                    i++;
                    break;
                case '0':
                    current = '\0';
                    i++;
                    break;
                default:
                    appendToStringBuilder(sb, &current, 1);
                    continue;
            }
        }
        appendToStringBuilder(sb, &current, 1);
    }

    ObjString* s = stringBuilderToOwnedString(sb);
    node->literal.value = STRING_VAL(s);
    node->literal.hasExplicitSuffix = false;
    node->location.line = token.line;
    node->location.column = token.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseBooleanLiteral(ParserContext* ctx, Token token) {
    ASTNode* node = new_node(ctx);
    node->type = NODE_LITERAL;
    bool boolValue = token_text_equals(&token, "true");
    node->literal.value = BOOL_VAL(boolValue);
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
    
    return parsePostfixExpressions(ctx, expr);
}

static ASTNode* parsePrimaryExpression(ParserContext* ctx) {
    Token token = nextToken(ctx);
    ASTNode* node = NULL;

    switch (token.type) {
        case TOKEN_NUMBER:
            node = parseNumberLiteral(ctx, token);
            break;
        case TOKEN_STRING:
            node = parseStringLiteral(ctx, token);
            break;
        case TOKEN_IDENTIFIER:
            if (token_text_equals(&token, "true") || token_text_equals(&token, "false")) {
                node = parseBooleanLiteral(ctx, token);
            } else if (token_text_equals(&token, "timestamp") &&
                       peekToken(ctx).type == TOKEN_LEFT_PAREN) {
                node = parseTimeStampExpression(ctx, token);
            } else {
                node = parseIdentifierExpression(ctx, token);
            }
            break;
        case TOKEN_LEFT_PAREN:
            node = parseParenthesizedExpressionToken(ctx, token);
            break;
        case TOKEN_LEFT_BRACKET:
            node = parseArrayLiteral(ctx, token);
            break;
        case TOKEN_FN:
            node = parseFunctionExpression(ctx, token);
            break;
        case TOKEN_MATCH:
            node = parseMatchExpression(ctx, token);
            break;
        default:
            return NULL;
    }

    if (!node) {
        return NULL;
    }

    return parsePostfixExpressions(ctx, node);
}

static bool is_valid_fill_length_node(const ASTNode* node) {
    if (!node) {
        return false;
    }

    if (node->type == NODE_LITERAL) {
        Value value = node->literal.value;
        switch (value.type) {
            case VAL_I32:
            case VAL_I64:
            case VAL_U32:
            case VAL_U64:
                return true;
            default:
                return false;
        }
    }

    if (node->type == NODE_IDENTIFIER) {
        return node->identifier.name != NULL;
    }

    return false;
}

static ASTNode* parseArrayLiteral(ParserContext* ctx, Token leftToken) {
    ASTNode** elements = NULL;
    int count = 0;
    int capacity = 0;
    bool trailingComma = false;

    while (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    if (peekToken(ctx).type != TOKEN_RIGHT_BRACKET) {
        while (true) {
            ASTNode* element = parseExpression(ctx);
            if (!element) {
                return NULL;
            }
            addStatement(ctx, &elements, &count, &capacity, element);

            Token next = peekToken(ctx);
            if (next.type != TOKEN_COMMA) {
                break;
            }

            nextToken(ctx); // consume comma

            while (peekToken(ctx).type == TOKEN_NEWLINE) {
                nextToken(ctx);
            }

            // Allow trailing comma before closing bracket
            if (peekToken(ctx).type == TOKEN_RIGHT_BRACKET) {
                trailingComma = true;
                break;
            }
        }
    }

    Token close = nextToken(ctx);
    if (close.type != TOKEN_RIGHT_BRACKET) {
        SrcLocation location = {NULL, leftToken.line, leftToken.column};
        report_compile_error(E1020_MISSING_BRACKET, location,
                             "Expected ']' to close this array literal, but found %s instead.",
                             token_type_to_string(close.type));
        return NULL;
    }

    if (ctx->allow_array_fill && count == 2 && !trailingComma &&
        is_valid_fill_length_node(elements[1])) {
        ASTNode* node = new_node(ctx);
        node->type = NODE_ARRAY_FILL;
        node->arrayFill.value = elements[0];
        node->arrayFill.lengthExpr = elements[1];
        node->arrayFill.lengthIdentifier = (elements[1]->type == NODE_IDENTIFIER)
                                               ? elements[1]->identifier.name
                                               : NULL;
        node->arrayFill.hasResolvedLength = false;
        node->arrayFill.resolvedLength = 0;
        node->location.line = leftToken.line;
        node->location.column = leftToken.column;
        node->dataType = NULL;
        return node;
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_ARRAY_LITERAL;
    node->arrayLiteral.elements = elements;
    node->arrayLiteral.count = count;
    node->location.line = leftToken.line;
    node->location.column = leftToken.column;
    node->dataType = NULL;
    return node;
}

static ASTNode* parseFunctionExpression(ParserContext* ctx, Token fnToken) {
    // Parameters
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

            int paramLen = paramTok.length;
            char* paramName = parser_arena_alloc(ctx, paramLen + 1);
            strncpy(paramName, paramTok.start, paramLen);
            paramName[paramLen] = '\0';

            ASTNode* paramType = NULL;
            if (peekToken(ctx).type == TOKEN_COLON) {
                nextToken(ctx);
                paramType = parseTypeAnnotation(ctx);
                if (!paramType) return NULL;
            }

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
            nextToken(ctx);
        }
    }

    if (nextToken(ctx).type != TOKEN_RIGHT_PAREN) {
        return NULL;
    }

    ASTNode* returnType = NULL;
    if (peekToken(ctx).type == TOKEN_ARROW) {
        Token arrowTok = nextToken(ctx);
        Token typeTok = peekToken(ctx);
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType(ctx);
            if (!returnType) return NULL;
        } else {
            if (!token_can_start_type(typeTok)) {
                SrcLocation location = {NULL, arrowTok.line, arrowTok.column};
                report_compile_error(E1006_INVALID_SYNTAX, location,
                                     "Expected return type after '->' in function expression, but found %s",
                                     token_type_to_string(typeTok.type));
                return NULL;
            }
            returnType = parseTypeAnnotation(ctx);
            if (!returnType) return NULL;
        }
    }

    if (nextToken(ctx).type != TOKEN_COLON) {
        return NULL;
    }

    if (nextToken(ctx).type != TOKEN_NEWLINE) {
        return NULL;
    }

    if (consume_indent_token(ctx).type != TOKEN_INDENT) {
        return NULL;
    }

    ASTNode* body = parseBlock(ctx);
    if (!body) {
        return NULL;
    }

    ASTNode* function = new_node(ctx);
    function->type = NODE_FUNCTION;
    function->function.name = NULL;
    function->function.params = params;
    function->function.paramCount = paramCount;
    function->function.returnType = returnType;
    function->function.body = body;
    function->function.isPublic = false;
    function->function.isMethod = false;
    function->function.isInstanceMethod = false;
    function->function.methodStructName = NULL;
    function->location.line = fnToken.line;
    function->location.column = fnToken.column;
    function->dataType = NULL;

    return function;
}

static ASTNode* parseFunctionDefinition(ParserContext* ctx, bool isPublic) {
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
        Token arrowTok = nextToken(ctx); // consume '->'
        Token typeTok = peekToken(ctx);
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType(ctx);
            if (!returnType) return NULL;
        } else {
            if (!token_can_start_type(typeTok)) {
                SrcLocation location = {NULL, arrowTok.line, arrowTok.column};
                report_compile_error(E1006_INVALID_SYNTAX, location,
                                     "Expected return type after '->' in function '%s', but found %s",
                                     functionName,
                                     token_type_to_string(typeTok.type));
                return NULL;
            }
            returnType = parseTypeAnnotation(ctx);
            if (!returnType) return NULL;
        }
    }

    ASTNode* body = NULL;
    if (nextToken(ctx).type != TOKEN_COLON) {
        return NULL;
    }

    Token afterColon = peekToken(ctx);
    if (afterColon.type == TOKEN_NEWLINE) {
        nextToken(ctx);
        if (consume_indent_token(ctx).type != TOKEN_INDENT) {
            return NULL;
        }
        body = parseBlock(ctx);
        if (!body) {
            return NULL;
        }
    } else {
        body = parseInlineBlock(ctx);
        if (!body) {
            return NULL;
        }
    }

    // Create function node
    ASTNode* function = new_node(ctx);
    function->type = NODE_FUNCTION;
    function->function.name = functionName;
    function->function.params = params;
    function->function.paramCount = paramCount;
    function->function.returnType = returnType;
    function->function.body = body;
    function->function.isPublic = isPublic;
    function->function.isMethod = false;
    function->function.isInstanceMethod = false;
    function->function.methodStructName = NULL;
    function->location.line = nameTok.line;
    function->location.column = nameTok.column;
    function->dataType = NULL;

    return function;
}

static ASTNode* parseEnumDefinition(ParserContext* ctx, bool isPublic) {
    Token enumTok = nextToken(ctx);
    if (enumTok.type != TOKEN_ENUM) {
        return NULL;
    }

    Token nameTok = nextToken(ctx);
    if (nameTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    int nameLen = nameTok.length;
    char* enumName = parser_arena_alloc(ctx, nameLen + 1);
    strncpy(enumName, nameTok.start, nameLen);
    enumName[nameLen] = '\0';

    char** genericParams = NULL;
    int genericParamCount = 0;
    int genericParamCapacity = 0;

    if (peekToken(ctx).type == TOKEN_LEFT_BRACKET) {
        nextToken(ctx);

        if (peekToken(ctx).type != TOKEN_RIGHT_BRACKET) {
            while (true) {
                Token paramTok = nextToken(ctx);
                if (paramTok.type != TOKEN_IDENTIFIER) {
                    report_reserved_keyword_identifier(ctx, paramTok, "generic parameter");
                    return NULL;
                }

                if (genericParamCount + 1 > genericParamCapacity) {
                    int newCap = genericParamCapacity == 0 ? 2 : genericParamCapacity * 2;
                    char** newParams = parser_arena_alloc(ctx, sizeof(char*) * newCap);
                    if (genericParamCapacity > 0 && genericParams) {
                        memcpy(newParams, genericParams, sizeof(char*) * genericParamCount);
                    }
                    genericParams = newParams;
                    genericParamCapacity = newCap;
                }

                int paramLen = paramTok.length;
                char* paramName = parser_arena_alloc(ctx, paramLen + 1);
                strncpy(paramName, paramTok.start, paramLen);
                paramName[paramLen] = '\0';
                genericParams[genericParamCount++] = paramName;

                Token delim = peekToken(ctx);
                if (delim.type == TOKEN_COMMA) {
                    nextToken(ctx);
                    continue;
                }
                break;
            }
        }

        Token closeGenerics = nextToken(ctx);
        if (closeGenerics.type != TOKEN_RIGHT_BRACKET) {
            return NULL;
        }
    }

    Token colonTok = nextToken(ctx);
    if (colonTok.type != TOKEN_COLON) {
        return NULL;
    }

    if (peekToken(ctx).type != TOKEN_NEWLINE) {
        return NULL;
    }
    nextToken(ctx);

    Token indentTok = consume_indent_token(ctx);
    if (indentTok.type != TOKEN_INDENT) {
        return NULL;
    }

    EnumVariant* variants = NULL;
    int variantCount = 0;
    int variantCapacity = 0;

    while (true) {
        Token lookahead = peekToken(ctx);
        if (lookahead.type == TOKEN_DEDENT) {
            nextToken(ctx);
            break;
        }
        if (lookahead.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }

        Token variantNameTok = nextToken(ctx);
        if (variantNameTok.type != TOKEN_IDENTIFIER) {
            return NULL;
        }

        int variantLen = variantNameTok.length;
        char* variantName = parser_arena_alloc(ctx, variantLen + 1);
        strncpy(variantName, variantNameTok.start, variantLen);
        variantName[variantLen] = '\0';

        EnumVariantField* fields = NULL;
        int fieldCount = 0;
        int fieldCapacity = 0;

        if (peekToken(ctx).type == TOKEN_LEFT_PAREN) {
            nextToken(ctx);

            if (peekToken(ctx).type == TOKEN_RIGHT_PAREN) {
                nextToken(ctx);
            } else {
                while (true) {
                    Token firstTok = nextToken(ctx);
                    if (!token_can_start_type(firstTok) &&
                        firstTok.type != TOKEN_IDENTIFIER) {
                        return NULL;
                    }

                    ASTNode* fieldType = NULL;
                    char* fieldName = NULL;

                    if (peekToken(ctx).type == TOKEN_COLON) {
                        if (firstTok.type != TOKEN_IDENTIFIER) {
                            return NULL;
                        }
                        int fieldNameLen = firstTok.length;
                        fieldName = parser_arena_alloc(ctx, fieldNameLen + 1);
                        strncpy(fieldName, firstTok.start, fieldNameLen);
                        fieldName[fieldNameLen] = '\0';
                        nextToken(ctx);
                        fieldType = parseTypeAnnotation(ctx);
                        if (!fieldType) {
                            return NULL;
                        }
                    } else {
                        fieldType = build_type_annotation_node(ctx, firstTok);
                        if (!fieldType) {
                            return NULL;
                        }
                        if (peekToken(ctx).type == TOKEN_QUESTION) {
                            nextToken(ctx);
                            fieldType->typeAnnotation.isNullable = true;
                        }
                    }

                    if (fieldCount + 1 > fieldCapacity) {
                        int newCap = fieldCapacity == 0 ? 2 : fieldCapacity * 2;
                        EnumVariantField* newFields = parser_arena_alloc(ctx, sizeof(EnumVariantField) * newCap);
                        if (fieldCapacity > 0) {
                            memcpy(newFields, fields, sizeof(EnumVariantField) * fieldCount);
                        }
                        fields = newFields;
                        fieldCapacity = newCap;
                    }

                    fields[fieldCount].name = fieldName;
                    fields[fieldCount].typeAnnotation = fieldType;
                    fieldCount++;

                    if (peekToken(ctx).type != TOKEN_COMMA) {
                        break;
                    }
                    nextToken(ctx);
                }

                Token closeTok = nextToken(ctx);
                if (closeTok.type != TOKEN_RIGHT_PAREN) {
                    return NULL;
                }
            }
        }

        EnumVariant variant = {
            .name = variantName,
            .fields = fields,
            .fieldCount = fieldCount,
        };
        add_enum_variant(ctx, &variants, &variantCount, &variantCapacity, variant);

        if (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_ENUM_DECL;
    node->enumDecl.name = enumName;
    node->enumDecl.isPublic = isPublic;
    node->enumDecl.variants = variants;
    node->enumDecl.variantCount = variantCount;
    node->enumDecl.genericParams = genericParams;
    node->enumDecl.genericParamCount = genericParamCount;
    node->location.line = enumTok.line;
    node->location.column = enumTok.column;
    node->dataType = NULL;

    return node;
}

static ASTNode* parseStructDefinition(ParserContext* ctx, bool isPublic) {
    Token structTok = nextToken(ctx);
    if (structTok.type != TOKEN_STRUCT) {
        return NULL;
    }

    Token nameTok = nextToken(ctx);
    if (nameTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    int nameLen = nameTok.length;
    char* structName = parser_arena_alloc(ctx, nameLen + 1);
    strncpy(structName, nameTok.start, nameLen);
    structName[nameLen] = '\0';

    Token colonTok = nextToken(ctx);
    if (colonTok.type != TOKEN_COLON) {
        return NULL;
    }

    if (peekToken(ctx).type != TOKEN_NEWLINE) {
        return NULL;
    }
    nextToken(ctx);

    Token indentTok = consume_indent_token(ctx);
    if (indentTok.type != TOKEN_INDENT) {
        return NULL;
    }

    StructField* fields = NULL;
    int fieldCount = 0;
    int fieldCapacity = 0;

    while (true) {
        Token lookahead = peekToken(ctx);
        if (lookahead.type == TOKEN_DEDENT) {
            nextToken(ctx);
            break;
        }
        if (lookahead.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }

        Token fieldNameTok = nextToken(ctx);
        if (fieldNameTok.type != TOKEN_IDENTIFIER) {
            return NULL;
        }

        int fieldNameLen = fieldNameTok.length;
        char* fieldName = parser_arena_alloc(ctx, fieldNameLen + 1);
        strncpy(fieldName, fieldNameTok.start, fieldNameLen);
        fieldName[fieldNameLen] = '\0';

        Token separatorTok = nextToken(ctx);
        if (separatorTok.type != TOKEN_COLON) {
            return NULL;
        }

        ASTNode* typeAnnotation = parseTypeAnnotation(ctx);
        if (!typeAnnotation) {
            return NULL;
        }

        ASTNode* defaultValue = NULL;
        if (peekToken(ctx).type == TOKEN_EQUAL) {
            nextToken(ctx);
            defaultValue = parseExpression(ctx);
            if (!defaultValue) {
                return NULL;
            }
        }

        if (fieldCount + 1 > fieldCapacity) {
            int newCap = fieldCapacity == 0 ? 4 : fieldCapacity * 2;
            StructField* newFields = parser_arena_alloc(ctx, sizeof(StructField) * newCap);
            if (fieldCapacity > 0) {
                memcpy(newFields, fields, sizeof(StructField) * fieldCount);
            }
            fields = newFields;
            fieldCapacity = newCap;
        }

        fields[fieldCount].name = fieldName;
        fields[fieldCount].typeAnnotation = typeAnnotation;
        fields[fieldCount].defaultValue = defaultValue;
        fieldCount++;

        if (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_STRUCT_DECL;
    node->structDecl.name = structName;
    node->structDecl.isPublic = isPublic;
    node->structDecl.fields = fields;
    node->structDecl.fieldCount = fieldCount;
    node->location.line = structTok.line;
    node->location.column = structTok.column;
    node->dataType = NULL;

    return node;
}

static ASTNode* parseImplBlock(ParserContext* ctx, bool isPublic) {
    Token implTok = nextToken(ctx);
    if (implTok.type != TOKEN_IMPL) {
        return NULL;
    }

    Token nameTok = nextToken(ctx);
    if (nameTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    int nameLen = nameTok.length;
    char* structName = parser_arena_alloc(ctx, nameLen + 1);
    strncpy(structName, nameTok.start, nameLen);
    structName[nameLen] = '\0';

    Token colonTok = nextToken(ctx);
    if (colonTok.type != TOKEN_COLON) {
        return NULL;
    }

    if (peekToken(ctx).type != TOKEN_NEWLINE) {
        return NULL;
    }
    nextToken(ctx);

    Token indentTok = consume_indent_token(ctx);
    if (indentTok.type != TOKEN_INDENT) {
        return NULL;
    }

    ASTNode** methods = NULL;
    int methodCount = 0;
    int methodCapacity = 0;

    while (true) {
        Token lookahead = peekToken(ctx);
        if (lookahead.type == TOKEN_DEDENT) {
            nextToken(ctx);
            break;
        }
        if (lookahead.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }

        bool methodIsPublic = false;
        if (lookahead.type == TOKEN_PUB) {
            nextToken(ctx);
            methodIsPublic = true;
            lookahead = peekToken(ctx);
        }

        if (lookahead.type != TOKEN_FN) {
            return NULL;
        }

        ASTNode* method = parseFunctionDefinition(ctx, methodIsPublic);
        if (!method) {
            return NULL;
        }

        method->function.isMethod = true;
        method->function.isPublic = methodIsPublic;
        method->function.methodStructName = structName;
        bool instance = false;
        if (method->function.paramCount > 0 && method->function.params &&
            method->function.params[0].name) {
            if (strcmp(method->function.params[0].name, "self") == 0) {
                instance = true;
            }
        }
        method->function.isInstanceMethod = instance;

        addStatement(ctx, &methods, &methodCount, &methodCapacity, method);

        if (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }
    }

    ASTNode* node = new_node(ctx);
    node->type = NODE_IMPL_BLOCK;
    node->implBlock.structName = structName;
    node->implBlock.isPublic = isPublic;
    node->implBlock.methods = methods;
    node->implBlock.methodCount = methodCount;
    node->location.line = implTok.line;
    node->location.column = implTok.column;
    node->dataType = NULL;

    return node;
}

// Parse return statement: return [expression]
static ASTNode* parseReturnStatement(ParserContext* ctx) {
    Token returnTok = nextToken(ctx); // consume 'return'
    
    ASTNode* value = NULL;

    // Check if there's a return value
    Token next = peekToken(ctx);
    if (next.type != TOKEN_NEWLINE && next.type != TOKEN_EOF && next.type != TOKEN_DEDENT) {
        ASTNode* firstValue = parseExpression(ctx);
        if (!firstValue) {
            return NULL;
        }

        ASTNode** elements = NULL;
        int elementCount = 0;
        int elementCapacity = 0;

        if (peekToken(ctx).type == TOKEN_COMMA) {
            addStatement(ctx, &elements, &elementCount, &elementCapacity, firstValue);
            while (peekToken(ctx).type == TOKEN_COMMA) {
                nextToken(ctx);
                ASTNode* expr = parseExpression(ctx);
                if (!expr) {
                    return NULL;
                }
                addStatement(ctx, &elements, &elementCount, &elementCapacity, expr);
            }

            ASTNode* arrayNode = new_node(ctx);
            arrayNode->type = NODE_ARRAY_LITERAL;
            arrayNode->arrayLiteral.elements = elements;
            arrayNode->arrayLiteral.count = elementCount;
            arrayNode->location = firstValue->location;
            arrayNode->dataType = NULL;
            value = arrayNode;
        } else {
            value = firstValue;
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
    
    bool previous_allow_fill = ctx->allow_array_fill;
    if (peekToken(ctx).type != TOKEN_RIGHT_PAREN) {
        ctx->allow_array_fill = false;
        while (true) {
            ASTNode* arg = parseExpression(ctx);
            if (!arg) {
                ctx->allow_array_fill = previous_allow_fill;
                return NULL;
            }
            
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
        ctx->allow_array_fill = previous_allow_fill;
        return NULL;
    }

    ctx->allow_array_fill = previous_allow_fill;
    
    ASTNode* call = new_node(ctx);
    call->type = NODE_CALL;
    call->call.callee = callee;
    call->call.args = args;
    call->call.argCount = argCount;
    call->call.arity_error_reported = false;
    call->location.line = openParen.line;
    call->location.column = openParen.column;
    call->dataType = NULL;

    return call;
}

static ASTNode* parseIndexExpression(ParserContext* ctx, ASTNode* arrayExpr, Token openToken) {
    while (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    ASTNode* firstExpr = NULL;
    TokenType nextType = peekToken(ctx).type;
    if (nextType != TOKEN_DOT_DOT && nextType != TOKEN_RIGHT_BRACKET) {
        firstExpr = parseExpression(ctx);
        if (!firstExpr) {
            return NULL;
        }
    }

    while (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    bool isSlice = false;
    ASTNode* endExpr = NULL;

    if (peekToken(ctx).type == TOKEN_DOT_DOT) {
        isSlice = true;
        nextToken(ctx); // consume '..'

        while (peekToken(ctx).type == TOKEN_NEWLINE) {
            nextToken(ctx);
        }

        if (peekToken(ctx).type != TOKEN_RIGHT_BRACKET) {
            endExpr = parseExpression(ctx);
            if (!endExpr) {
                return NULL;
            }

            while (peekToken(ctx).type == TOKEN_NEWLINE) {
                nextToken(ctx);
            }
        }
    }

    Token close = nextToken(ctx);
    if (close.type != TOKEN_RIGHT_BRACKET) {
        SrcLocation location = {NULL, openToken.line, openToken.column};
        report_compile_error(E1020_MISSING_BRACKET, location,
                             "Expected ']' to close this bracket expression, but found %s instead.",
                             token_type_to_string(close.type));
        return NULL;
    }

    ASTNode* indexNode = new_node(ctx);
    indexNode->location.line = openToken.line;
    indexNode->location.column = openToken.column;
    indexNode->dataType = NULL;

    if (isSlice) {
        indexNode->type = NODE_ARRAY_SLICE;
        indexNode->arraySlice.array = arrayExpr;
        indexNode->arraySlice.start = firstExpr;
        indexNode->arraySlice.end = endExpr;
    } else {
        if (!firstExpr) {
            return NULL;
        }
        indexNode->type = NODE_INDEX_ACCESS;
        indexNode->indexAccess.array = arrayExpr;
        indexNode->indexAccess.index = firstExpr;
        indexNode->indexAccess.isStringIndex = false;
    }

    return indexNode;
}

static ASTNode* parseMemberAccess(ParserContext* ctx, ASTNode* objectExpr) {
    if (!objectExpr) {
        return NULL;
    }

    Token dot = nextToken(ctx);
    (void)dot; // Dot token consumed

    Token memberTok = nextToken(ctx);
    if (memberTok.type != TOKEN_IDENTIFIER) {
        return NULL;
    }

    int nameLen = memberTok.length;
    char* memberName = parser_arena_alloc(ctx, nameLen + 1);
    strncpy(memberName, memberTok.start, nameLen);
    memberName[nameLen] = '\0';

    ASTNode* node = new_node(ctx);
    node->type = NODE_MEMBER_ACCESS;
    node->member.object = objectExpr;
    node->member.member = memberName;
    node->member.isMethod = false;
    node->member.isInstanceMethod = false;
    node->member.resolvesToEnum = false;
    node->member.resolvesToEnumVariant = false;
    node->member.enumVariantIndex = -1;
    node->member.enumVariantArity = 0;
    node->member.enumTypeName = NULL;
    node->location.line = memberTok.line;
    node->location.column = memberTok.column;
    node->dataType = NULL;

    return node;
}

static ASTNode* parseStructLiteral(ParserContext* ctx, ASTNode* typeExpr, Token leftBrace) {
    if (!typeExpr) {
        return NULL;
    }

    char* structName = NULL;
    char* moduleAlias = NULL;

    if (typeExpr->type == NODE_IDENTIFIER) {
        structName = typeExpr->identifier.name;
    } else if (typeExpr->type == NODE_MEMBER_ACCESS && typeExpr->member.member &&
               typeExpr->member.object && typeExpr->member.object->type == NODE_IDENTIFIER) {
        structName = typeExpr->member.member;
        moduleAlias = typeExpr->member.object->identifier.name;
    } else {
        return NULL;
    }

    StructLiteralField* fields = NULL;
    int fieldCount = 0;
    int fieldCapacity = 0;

    while (peekToken(ctx).type == TOKEN_NEWLINE) {
        nextToken(ctx);
    }

    if (peekToken(ctx).type != TOKEN_RIGHT_BRACE) {
        while (true) {
            Token fieldTok = nextToken(ctx);
            if (fieldTok.type != TOKEN_IDENTIFIER) {
                return NULL;
            }

            int fieldLen = fieldTok.length;
            char* fieldName = parser_arena_alloc(ctx, fieldLen + 1);
            strncpy(fieldName, fieldTok.start, fieldLen);
            fieldName[fieldLen] = '\0';

            if (nextToken(ctx).type != TOKEN_COLON) {
                return NULL;
            }

            ASTNode* valueExpr = parseExpression(ctx);
            if (!valueExpr) {
                return NULL;
            }

            if (fieldCount + 1 > fieldCapacity) {
                int newCap = fieldCapacity == 0 ? 4 : fieldCapacity * 2;
                StructLiteralField* newFields = parser_arena_alloc(ctx, sizeof(StructLiteralField) * newCap);
                if (fieldCapacity > 0 && fields) {
                    memcpy(newFields, fields, sizeof(StructLiteralField) * fieldCount);
                }
                fields = newFields;
                fieldCapacity = newCap;
            }

            fields[fieldCount].name = fieldName;
            fields[fieldCount].value = valueExpr;
            fieldCount++;

            Token nextTok = peekToken(ctx);
            if (nextTok.type == TOKEN_COMMA) {
                nextToken(ctx);
                while (peekToken(ctx).type == TOKEN_NEWLINE) {
                    nextToken(ctx);
                }
                if (peekToken(ctx).type == TOKEN_RIGHT_BRACE) {
                    break;
                }
            } else if (nextTok.type == TOKEN_NEWLINE) {
                nextToken(ctx);
                while (peekToken(ctx).type == TOKEN_NEWLINE) {
                    nextToken(ctx);
                }
                if (peekToken(ctx).type == TOKEN_RIGHT_BRACE) {
                    break;
                }
            } else {
                break;
            }
        }
    }

    if (nextToken(ctx).type != TOKEN_RIGHT_BRACE) {
        return NULL;
    }

    typeExpr->type = NODE_STRUCT_LITERAL;
    typeExpr->structLiteral.structName = structName;
    typeExpr->structLiteral.moduleAlias = moduleAlias;
    typeExpr->structLiteral.resolvedModuleName = NULL;
    typeExpr->structLiteral.fields = fields;
    typeExpr->structLiteral.fieldCount = fieldCount;
    typeExpr->location.line = leftBrace.line;
    typeExpr->location.column = leftBrace.column;
    typeExpr->dataType = NULL;

    return typeExpr;
}

static ASTNode* parsePostfixExpressions(ParserContext* ctx, ASTNode* expr) {
    if (!expr) {
        return NULL;
    }

    while (true) {
        Token next = peekToken(ctx);
        if (next.type == TOKEN_LEFT_PAREN) {
            expr = parseCallExpression(ctx, expr);
            if (!expr) {
                return NULL;
            }
        } else if (next.type == TOKEN_LEFT_BRACKET) {
            Token openToken = nextToken(ctx);
            expr = parseIndexExpression(ctx, expr, openToken);
            if (!expr) {
                return NULL;
            }
        } else if (next.type == TOKEN_DOT) {
            expr = parseMemberAccess(ctx, expr);
            if (!expr) {
                return NULL;
            }
        } else if (next.type == TOKEN_LEFT_BRACE) {
            Token leftBrace = nextToken(ctx);
            expr = parseStructLiteral(ctx, expr, leftBrace);
            if (!expr) {
                return NULL;
            }
        } else {
            break;
        }
    }

    return expr;
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
            ASTNode* paramType = NULL;
            Token nextTok = peekToken(ctx);
            if (nextTok.type == TOKEN_FN) {
                paramType = parseFunctionType(ctx);
            } else {
                if (!token_can_start_type(nextTok)) {
                    SrcLocation location = {NULL, nextTok.line, nextTok.column};
                    report_compile_error(E1006_INVALID_SYNTAX, location,
                                         "expected a type annotation in function type, but found %s",
                                         token_type_to_string(nextTok.type));
                    return NULL;
                }
                paramType = parseTypeAnnotation(ctx);
            }
            if (!paramType) {
                return NULL;
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
        Token arrowTok = nextToken(ctx); // consume '->'
        Token typeTok = peekToken(ctx);
        if (typeTok.type == TOKEN_FN) {
            returnType = parseFunctionType(ctx);
        } else {
            if (!token_can_start_type(typeTok)) {
                SrcLocation location = {NULL, arrowTok.line, arrowTok.column};
                report_compile_error(E1006_INVALID_SYNTAX, location,
                                     "Expected return type after '->' in function type, but found %s",
                                     token_type_to_string(typeTok.type));
                return NULL;
            }
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
    funcType->function.isPublic = false;
    funcType->function.isMethod = false;
    funcType->function.isInstanceMethod = false;
    funcType->function.methodStructName = NULL;

    return funcType;
}

void freeAST(ASTNode* node) {
    // Note: AST memory is managed by the parser context arena
    // This function is kept for backward compatibility but doesn't need to do anything
    (void)node;
}


// Global flag to control parser debug output - can be set externally
static bool parser_debug_enabled = false;

// Function to control parser debug output
void set_parser_debug(bool enabled) {
    parser_debug_enabled = enabled;
}

// Debug macro that checks the flag
#define PARSER_DEBUG_PRINTF(...) do { \
    if (parser_debug_enabled) { \
        printf(__VA_ARGS__); \
        fflush(stdout); \
    } \
} while(0)

// Context-based parsing interface - new implementation
ASTNode* parseSourceWithContextAndModule(ParserContext* ctx, const char* source, const char* module_name) {
    if (!ctx) return NULL;

    parser_context_reset(ctx);
    control_flow_reset_validation_state();
    tuple_temp_counter = 0;

    init_scanner(source);

    ASTNode** statements = NULL;
    int count = 0;
    int capacity = 0;

    char* moduleName = NULL;
    if (module_name && module_name[0] != '\0') {
        size_t nameLen = strlen(module_name);
        moduleName = parser_arena_alloc(ctx, nameLen + 1);
        if (!moduleName) {
            return NULL;
        }
        memcpy(moduleName, module_name, nameLen);
        moduleName[nameLen] = '\0';
    }

    while (true) {
        Token t = peekToken(ctx);
        if (t.type == TOKEN_EOF) break;
        if (t.type == TOKEN_NEWLINE) {
            nextToken(ctx);
            continue;
        }
        if (t.type == TOKEN_INDENT) {
            SrcLocation location = {NULL, t.line, t.column};
            report_compile_error(E1008_INVALID_INDENTATION, location,
                                 "It looks like this line is indented, but there's no open block above it.");
            return NULL;
        }
        if (t.type == TOKEN_COMMA) {
            nextToken(ctx);
            continue;
        }
        if (t.type == TOKEN_SEMICOLON) {
            nextToken(ctx);
            continue;
        }

        ASTNode* stmt = parseStatement(ctx);
        if (!stmt) {
            return NULL;
        }

        addStatement(ctx, &statements, &count, &capacity, stmt);
    }

    ASTNode* root = new_node(ctx);

    root->type = NODE_PROGRAM;
    root->program.declarations = statements;
    root->program.count = count;
    root->program.moduleName = moduleName;
    root->location.line = 1;
    root->location.column = 1;
    root->dataType = NULL;

    return root;
}

ASTNode* parseSourceWithContext(ParserContext* ctx, const char* source) {
    return parseSourceWithContextAndModule(ctx, source, NULL);
}


