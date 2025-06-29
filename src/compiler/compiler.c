// Simple compiler implementation for testing
#include "../../include/compiler.h"
#include "../../include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Arena allocator for AST nodes (same as parser)
typedef struct {
    char* buffer;
    size_t capacity, used;
} Arena;

static Arena compilerArena;

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

static ASTNode* new_node(void) {
    return arena_alloc(&compilerArena, sizeof(ASTNode));
}

// Simple token lookahead
static Token peekedToken = {0};
static bool hasPeekedToken = false;

Token peekToken() {
    if (!hasPeekedToken) {
        peekedToken = scan_token();
        hasPeekedToken = true;
    }
    return peekedToken;
}

Token nextToken() {
    if (hasPeekedToken) {
        hasPeekedToken = false;
        return peekedToken;
    }
    return scan_token();
}

// Forward declarations
ASTNode* parseExpression();
ASTNode* parseBinaryExpression(int minPrec);
ASTNode* parsePrimaryExpression();
ASTNode* parseVariableDeclaration();

// Real parsing function using lexer
ASTNode* parseSource(const char* source) {
    // Initialize arena allocator
    arena_init(&compilerArena, 1 << 16); // 64KB initial size
    
    // Initialize lexer with source code
    init_scanner(source);
    
    // Reset lookahead
    hasPeekedToken = false;
    
    // Check if this is a statement or expression
    Token firstToken = peekToken();
    if (firstToken.type == TOKEN_LET) {
        return parseVariableDeclaration();
    } else {
        // Parse as expression
        return parseExpression();
    }
}

// Parse variable declarations: let name = initializer
ASTNode* parseVariableDeclaration() {
    Token letToken = nextToken(); // consume 'let'
    if (letToken.type != TOKEN_LET) {
        return NULL; // Error
    }
    
    Token nameToken = nextToken();
    if (nameToken.type != TOKEN_IDENTIFIER) {
        return NULL; // Error: expected identifier
    }
    
    Token equalToken = nextToken();
    if (equalToken.type != TOKEN_EQUAL) {
        return NULL; // Error: expected '='
    }
    
    ASTNode* initializer = parseExpression();
    if (!initializer) {
        return NULL; // Error parsing initializer
    }
    
    // Create variable declaration node
    ASTNode* varNode = new_node();
    
    varNode->type = NODE_VAR_DECL;
    varNode->location.line = letToken.line;
    varNode->location.column = letToken.column;
    varNode->dataType = NULL;
    
    // Copy variable name
    int len = nameToken.length;
    char* name = arena_alloc(&compilerArena, len + 1);
    strncpy(name, nameToken.start, len);
    name[len] = '\0';
    
    varNode->varDecl.name = name;
    varNode->varDecl.isPublic = false;
    varNode->varDecl.initializer = initializer;
    varNode->varDecl.typeAnnotation = NULL;
    varNode->varDecl.isConst = false;
    
    return varNode;
}

// Parse expressions with operator precedence
ASTNode* parseExpression() {
    return parseBinaryExpression(0);
}

// Parse binary expressions with precedence climbing
ASTNode* parseBinaryExpression(int minPrec) {
    ASTNode* left = parsePrimaryExpression();
    if (!left) return NULL;
    
    while (true) {
        Token operator = peekToken();
        int prec = getOperatorPrecedence(operator.type);
        
        if (prec < minPrec || operator.type == TOKEN_EOF) {
            break;
        }
        
        // Consume the operator token
        nextToken();
        
        ASTNode* right = parseBinaryExpression(prec + 1);
        if (!right) return NULL;
        
        // Create binary expression node
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

// Parse primary expressions (numbers, strings, variables, parentheses)
ASTNode* parsePrimaryExpression() {
    Token token = nextToken();
    
    switch (token.type) {
        case TOKEN_NUMBER: {
            ASTNode* node = new_node();
            node->type = NODE_LITERAL;
            
            // Parse the number from token
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
            
            // Extract string content (remove quotes)
            int contentLen = token.length - 2; // Remove quotes
            char* content = malloc(contentLen + 1);
            strncpy(content, token.start + 1, contentLen);
            content[contentLen] = '\0';
            
            // Create string object - for now use a simple approach
            // Create a string value - we'll fix this when the value system is unified
            node->literal.value = I32_VAL(0); // Placeholder until string values work
            
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
            
            // Copy identifier name
            int len = token.length;
            char* name = arena_alloc(&compilerArena, len + 1);
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
                // Error: expected ')'
                return NULL;
            }
            return expr;
        }
        
        default:
            // Error: unexpected token
            return NULL;
    }
}

// Helper functions
int getOperatorPrecedence(TokenType type) {
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

const char* getOperatorString(TokenType type) {
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

void freeAST(ASTNode* node) {
    // Using arena allocation - just reset the arena
    arena_reset(&compilerArena);
    (void)node;
}

// Enhanced compilation function - converts expressions to bytecode
int compileExpressionToRegister(ASTNode* node, Compiler* compiler) {
    if (!node) return -1;
    
    switch (node->type) {
        case NODE_LITERAL: {
            uint8_t reg = allocateRegister(compiler);
            emitConstant(compiler, reg, node->literal.value);
            return reg;
        }
        
        case NODE_BINARY: {
            // Compile left and right operands
            int leftReg = compileExpressionToRegister(node->binary.left, compiler);
            if (leftReg < 0) return -1;
            
            int rightReg = compileExpressionToRegister(node->binary.right, compiler);
            if (rightReg < 0) return -1;
            
            uint8_t resultReg = allocateRegister(compiler);
            
            // Generate appropriate instruction based on operator
            const char* op = node->binary.op;
            if (strcmp(op, "+") == 0) {
                emitByte(compiler, OP_ADD_I32_R);
            } else if (strcmp(op, "-") == 0) {
                emitByte(compiler, OP_SUB_I32_R);
            } else if (strcmp(op, "*") == 0) {
                emitByte(compiler, OP_MUL_I32_R);
            } else if (strcmp(op, "/") == 0) {
                emitByte(compiler, OP_DIV_I32_R);
            } else if (strcmp(op, "%") == 0) {
                emitByte(compiler, OP_MOD_I32_R);
            } else if (strcmp(op, "==") == 0) {
                emitByte(compiler, OP_EQ_R);
            } else if (strcmp(op, "<") == 0) {
                emitByte(compiler, OP_LT_I32_R);
            } else {
                return -1; // Unsupported operator
            }
            
            // Emit operand registers
            emitByte(compiler, resultReg);
            emitByte(compiler, (uint8_t)leftReg);
            emitByte(compiler, (uint8_t)rightReg);
            
            return resultReg;
        }
        
        case NODE_IDENTIFIER: {
            // Look up variable in locals
            const char* name = node->identifier.name;
            for (int i = compiler->localCount - 1; i >= 0; i--) {
                if (compiler->locals[i].isActive && 
                    strcmp(compiler->locals[i].name, name) == 0) {
                    return compiler->locals[i].reg;
                }
            }
            
            // Variable not found - return 0 for now
            uint8_t reg = allocateRegister(compiler);
            emitConstant(compiler, reg, I32_VAL(0));
            return reg;
        }
        
        case NODE_VAR_DECL: {
            // Compile the initializer
            int initReg = compileExpressionToRegister(node->varDecl.initializer, compiler);
            if (initReg < 0) return -1;
            
            // Add variable to locals table
            if (compiler->localCount >= REGISTER_COUNT) {
                return -1; // Too many locals
            }
            
            int localIndex = compiler->localCount++;
            compiler->locals[localIndex].name = node->varDecl.name;
            compiler->locals[localIndex].reg = (uint8_t)initReg;
            compiler->locals[localIndex].isActive = true;
            
            return initReg;
        }
        
        default:
            return -1;
    }
}

// Wrapper function to maintain compatibility
bool compileExpression(ASTNode* node, Compiler* compiler) {
    return compileExpressionToRegister(node, compiler) >= 0;
}