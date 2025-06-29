#ifndef ORSU_AST_H
#define ORSU_AST_H

#include "common.h"
#include "lexer.h"
#include "type.h"
#include "value.h"

typedef enum {
    AST_LITERAL,
    AST_BINARY,
    AST_UNARY,
    AST_CAST,
    AST_VARIABLE,
    AST_ASSIGNMENT,
    AST_CALL,
    AST_ARRAY,
    AST_ARRAY_FILL,
    AST_ARRAY_SET,
    AST_SLICE,
    AST_STRUCT_LITERAL,
    AST_FIELD,
    AST_FIELD_SET,
    AST_LET,
    AST_STATIC,
    AST_CONST,
    AST_PRINT,
    AST_IF,
    AST_TERNARY,
    AST_BLOCK,
    AST_WHILE,
    AST_FOR,
    AST_FUNCTION,
    AST_TRY,
    AST_RETURN,
    AST_BREAK,
    AST_CONTINUE,
    AST_IMPORT,
    AST_USE
} ASTNodeType;

typedef struct {
    Token name;
    uint8_t index;
    Type** genericArgs;
    int genericArgCount;
} VariableData;

typedef struct {
    Token name;
    Type* type;
    struct ASTNode* initializer;
    uint8_t index;
    bool isMutable;
    bool isPublic;
} LetData;

typedef struct {
    Token name;
    Type* type;
    struct ASTNode* initializer;
    uint8_t index;
    bool isPublic;
} ConstData;

typedef struct {
    struct ASTNode* format;     // Format string expression
    struct ASTNode* arguments;  // Linked list of argument expressions
    int argCount;               // Number of arguments
    bool newline;               // Whether to append newline
} PrintData;

typedef struct {
    struct ASTNode* condition;
    struct ASTNode* thenBranch;
    struct ASTNode* elifConditions;  // Linked list of elif conditions
    struct ASTNode* elifBranches;    // Linked list of elif branches
    struct ASTNode* elseBranch;
} IfData;

typedef struct {
    struct ASTNode* condition;
    struct ASTNode* thenExpr;
    struct ASTNode* elseExpr;
} TernaryData;

typedef struct {
    struct ASTNode* statements;  // Linked list of statements
    bool scoped;                 // Whether this block introduces a new scope
} BlockData;

typedef struct {
    struct ASTNode* condition;  // Loop condition
    struct ASTNode* body;       // Loop body
} WhileData;

typedef struct {
    Token iteratorName;         // Iterator variable name
    uint8_t iteratorIndex;      // Iterator variable index
    struct ASTNode* startExpr;  // Start of range
    struct ASTNode* endExpr;    // End of range
    struct ASTNode* stepExpr;   // Step value (optional)
    struct ASTNode* body;       // Loop body
} ForData;

typedef struct {
    struct ASTNode* elements;   // Linked list of element expressions
    int elementCount;
} ArrayData;

typedef struct {
    struct ASTNode* value;   // Expression for the fill value
    struct ASTNode* length;  // Expression for the array length
    int lengthValue;         // Inferred constant length or -1 if not constant
} ArrayFillData;

typedef struct {
    ObjString* path;
} ImportData;

typedef struct {
    ObjString** parts;       // Module path components
    int partCount;
    ObjString** symbols;     // Imported symbols
    ObjString** symbolAliases; // Aliases for symbols
    int symbolCount;
    ObjString* alias;        // Alias for the module
    ObjString* path;         // Resolved file path
} UseData;

typedef struct {
    Token name;                 // Struct name
    struct ASTNode* values;     // Linked list of field value expressions
    int fieldCount;
    Type** genericArgs;         // Generic argument types
    int genericArgCount;
} StructLiteralData;

typedef struct {
    Token fieldName;            // Accessed field name
    int index;                  // Resolved index within the struct
} FieldAccessData;

typedef struct {
    Token name;                // Function name
    struct ASTNode* parameters; // Linked list of parameter nodes
    Type* returnType;          // Return type
    struct ASTNode* body;      // Function body
    uint8_t index;             // Function index in the function table
    bool isMethod;             // True if function has implicit self
    Type* implType;            // Struct type if method
    ObjString* mangledName;    // GC-managed mangled name
    ObjString** genericParams; // Generic parameter names
    GenericConstraint* genericConstraints; // Constraints for generics
    int genericCount;
    bool isPublic;             // Exported from module
} FunctionData;

typedef struct {
    Token name;               // Function name
    struct ASTNode* arguments; // Linked list of argument nodes
    uint8_t index;            // Function index in the function table
    bool* convertArgs;         // Array of flags for argument conversion
    int argCount;             // Number of arguments
    Type* staticType;         // Struct type if called as Struct.fn
    ObjString* mangledName;    // GC-managed mangled name if method call
    int nativeIndex;           // -1 if not a native function
    int builtinOp;             // Specialized opcode for builtins (-1 if none)
    Type** genericArgs;        // Generic argument types
    int genericArgCount;
} CallData;

typedef struct {
    struct ASTNode* value;    // Return value expression
} ReturnData;

typedef struct {
    struct ASTNode* tryBlock;
    Token errorName;
    struct ASTNode* catchBlock;
    uint8_t errorIndex;
} TryData;

typedef struct {
    Type* type;              // Target type for cast
} CastData;

typedef struct ASTNode {
    Obj obj;
    ASTNodeType type;
    struct ASTNode* left;
    struct ASTNode* right;
    struct ASTNode* next;
    union {
        Value literal;
        struct {
            Token operator;
            int arity;
            bool convertLeft;   // Flag to indicate if left operand needs conversion
            bool convertRight;  // Flag to indicate if right operand needs conversion
        } operation;
        VariableData variable;
        LetData let;
        LetData staticVar;
        ConstData constant;
        PrintData print;
        IfData ifStmt;
        TernaryData ternary;
        BlockData block;
        WhileData whileStmt;
        ForData forStmt;
        ArrayData array;
        ArrayFillData arrayFill;
        StructLiteralData structLiteral;
        FieldAccessData field;
        struct {
            struct ASTNode* index;
        } arraySet;
        struct {
            struct ASTNode* start;
            struct ASTNode* end;
        } slice;
        FieldAccessData fieldSet;
        FunctionData function;
        CallData call;
        TryData tryStmt;
        ReturnData returnStmt;
        ImportData importStmt;
        UseData useStmt;
        CastData cast;
    } data;
    Type* valueType;
    int line; // Source line number for diagnostics
} ASTNode;

ASTNode* createLiteralNode(Value value);
ASTNode* createBinaryNode(Token operator, ASTNode * left, ASTNode* right);
ASTNode* createUnaryNode(Token operator, ASTNode * operand);
ASTNode* createVariableNode(Token name, uint8_t index);
ASTNode* createLetNode(Token name, Type* type, ASTNode* initializer, bool isMutable, bool isPublic);
ASTNode* createStaticNode(Token name, Type* type, ASTNode* initializer, bool isMutable);
ASTNode* createConstNode(Token name, Type* type, ASTNode* initializer, bool isPublic);
ASTNode* createPrintNode(ASTNode* format, ASTNode* arguments, int argCount, bool newline, int line);
ASTNode* createAssignmentNode(Token name, ASTNode* value);
ASTNode* createIfNode(ASTNode* condition, ASTNode* thenBranch, ASTNode* elifConditions, ASTNode* elifBranches, ASTNode* elseBranch);
ASTNode* createTernaryNode(ASTNode* condition, ASTNode* thenExpr, ASTNode* elseExpr);
ASTNode* createBlockNode(ASTNode* statements, bool scoped);
ASTNode* createWhileNode(ASTNode* condition, ASTNode* body);
ASTNode* createForNode(Token iteratorName, ASTNode* startExpr, ASTNode* endExpr, ASTNode* stepExpr, ASTNode* body);
ASTNode* createFunctionNode(Token name, ASTNode* parameters, Type* returnType,
                            ASTNode* body, ObjString** generics,
                            GenericConstraint* constraints,
                            int genericCount, bool isPublic);
ASTNode* createCallNode(Token name, ASTNode* arguments, int argCount, Type* staticType,
                        Type** genericArgs, int genericArgCount);
ASTNode* createTryNode(ASTNode* tryBlock, Token errorName, ASTNode* catchBlock);
ASTNode* createReturnNode(ASTNode* value);
ASTNode* createArrayNode(ASTNode* elements, int elementCount);
ASTNode* createArrayFillNode(ASTNode* value, ASTNode* length);
ASTNode* createArraySetNode(ASTNode* array, ASTNode* index, ASTNode* value);
ASTNode* createSliceNode(ASTNode* array, ASTNode* start, ASTNode* end);
ASTNode* createStructLiteralNode(Token name, ASTNode* values, int fieldCount,
                                 Type** genericArgs, int genericArgCount);
ASTNode* createFieldAccessNode(ASTNode* object, Token name);
ASTNode* createFieldSetNode(ASTNode* object, Token name, ASTNode* value);
ASTNode* createBreakNode();
ASTNode* createContinueNode();
ASTNode* createImportNode(Token path);
ASTNode* createUseNode(UseData data);
ASTNode* createCastNode(ASTNode* expr, Type* type);

void freeASTNode(ASTNode* node);

#endif