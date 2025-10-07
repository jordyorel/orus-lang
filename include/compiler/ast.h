// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/ast.h
// Author: Jordy Orel KONDA
// Copyright (c) 2022 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares abstract syntax tree node structures and helper types for the Orus compiler front-end.


#ifndef ORUS_AST_H
#define ORUS_AST_H

#include "public/common.h"
#include "vm/vm.h"

// Value, Type, and SrcLocation are now available from vm.h

// Forward declaration
typedef struct ASTNode ASTNode;

// Function parameter representation
typedef struct {
    char* name;
    ASTNode* typeAnnotation;  // Optional type annotation
} FunctionParam;

// Struct field representation
typedef struct {
    char* name;
    ASTNode* typeAnnotation;  // Field type annotation (required)
    ASTNode* defaultValue;    // Optional default value expression
} StructField;

typedef struct {
    char* name;
    ASTNode* value;
} StructLiteralField;

typedef struct {
    char* name;
    ASTNode* typeAnnotation;  // Payload type for the variant field (optional)
} EnumVariantField;

typedef struct {
    char* name;
    EnumVariantField* fields;
    int fieldCount;
} EnumVariant;

// Different kinds of AST nodes supported in the minimal language
typedef enum {
    NODE_PROGRAM,
    NODE_VAR_DECL,
    NODE_IDENTIFIER,
    NODE_LITERAL,
    NODE_ARRAY_LITERAL,
    NODE_ARRAY_FILL,
    NODE_INDEX_ACCESS,
    NODE_BINARY,
    NODE_ASSIGN,
    NODE_ARRAY_ASSIGN,
    NODE_ARRAY_SLICE,
    NODE_PRINT,
    NODE_TIME_STAMP,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR_RANGE,
    NODE_FOR_ITER,
    NODE_TRY,
    NODE_BLOCK,
    NODE_TERNARY,
    NODE_UNARY,
    NODE_TYPE,
    NODE_BREAK,
    NODE_CONTINUE,
    NODE_PASS,
    NODE_FUNCTION,
    NODE_CALL,
    NODE_RETURN,
    NODE_CAST,       // Add cast node for 'as' operator
    NODE_STRUCT_DECL,
    NODE_IMPL_BLOCK,
    NODE_STRUCT_LITERAL,
    NODE_MEMBER_ACCESS,
    NODE_MEMBER_ASSIGN,
    NODE_ENUM_DECL,
    NODE_IMPORT,
    NODE_ENUM_MATCH_TEST,
    NODE_ENUM_PAYLOAD,
    NODE_ENUM_MATCH_CHECK,
    NODE_MATCH_EXPRESSION
} NodeType;

typedef struct ImportSymbol {
    char* name;
    char* alias;
} ImportSymbol;

typedef struct MatchArm {
    bool isWildcard;
    bool isEnumCase;
    char* enumTypeName;
    char* variantName;
    char** payloadNames;
    int payloadCount;
    int variantIndex;
    ASTNode* valuePattern;
    ASTNode* body;
    ASTNode* condition;
    ASTNode** payloadAccesses;
    SrcLocation location;
} MatchArm;

struct ASTNode {
    NodeType type;
    SrcLocation location;
    Type* dataType;
    union {
        struct {
            ASTNode** declarations;
            int count;
            char* moduleName;
        } program;
        struct {
            char* name;
            bool isPublic;
            bool isGlobal;
            ASTNode* initializer;
            ASTNode* typeAnnotation;
            bool isMutable;
        } varDecl;
        struct {
            char* moduleName;
            char* moduleAlias;
            ImportSymbol* symbols;
            int symbolCount;
            bool importAll;
            bool importModule;           // True when importing module as namespace (no direct symbols)
        } import;
        struct {
            char* name;
        } identifier;
        struct {
            Value value;
            bool hasExplicitSuffix;
        } literal;
        struct {
            ASTNode** elements;
            int count;
        } arrayLiteral;
        struct {
            ASTNode* value;
            ASTNode* lengthExpr;
            char* lengthIdentifier;
            bool hasResolvedLength;
            int resolvedLength;
        } arrayFill;
        struct {
            ASTNode* array;
            ASTNode* index;
            bool isStringIndex;
        } indexAccess;
        struct {
            char* op;
            ASTNode* left;
            ASTNode* right;
        } binary;
        struct {
            char* name;
            ASTNode* value;
        } assign;
        struct {
            ASTNode* target;   // NODE_INDEX_ACCESS target
            ASTNode* value;    // Value being assigned
        } arrayAssign;
        struct {
            ASTNode* array;
            ASTNode* start;
            ASTNode* end;
        } arraySlice;
        struct {
            ASTNode** values;
            int count;
        } print;
        struct {
            ASTNode* condition;
            ASTNode* thenBranch;
            ASTNode* elseBranch;
        } ifStmt;
        struct {
            ASTNode* condition;
            ASTNode* body;
            char* label;
        } whileStmt;
        struct {
            char* varName;
            ASTNode* start;
            ASTNode* end;
            ASTNode* step;
            bool inclusive;
            ASTNode* body;
            char* label;
        } forRange;
        struct {
            char* varName;
            ASTNode* iterable;
            ASTNode* body;
            char* label;
        } forIter;
        struct {
            ASTNode* tryBlock;
            char* catchVar;
            ASTNode* catchBlock;
        } tryStmt;
        struct {
            ASTNode** statements;
            int count;
            bool createsScope;
        } block;
        struct {
            ASTNode* condition;
            ASTNode* trueExpr;
            ASTNode* falseExpr;
        } ternary;
        struct {
            char* op;
            ASTNode* operand;
        } unary;
        struct {
            char* name;
            bool isNullable;
            bool isArrayType;
            ASTNode* arrayElementType;
            bool arrayHasLength;
            int arrayLength;
            char* arrayLengthIdentifier;
            ASTNode** genericArgs;
            int genericArgCount;
        } typeAnnotation;
        struct {
            char* label;
        } breakStmt;
        struct {
            char* label;
        } continueStmt;
        struct {
            char* name;                    // Function name
            FunctionParam* params;         // Parameters
            int paramCount;                // Number of parameters
            ASTNode* returnType;           // Optional return type annotation
            ASTNode* body;                 // Function body (block)
            bool isPublic;                 // Whether function is public
            bool isMethod;                 // Whether function is defined in impl block
            bool isInstanceMethod;         // Whether method has implicit self receiver
            char* methodStructName;        // Owning struct for methods
            bool hasCoreIntrinsic;         // Whether the function binds to a core intrinsic
            char* coreIntrinsicSymbol;     // Symbol name for the bound intrinsic
        } function;
        struct {
            ASTNode* callee;               // Function expression
            ASTNode** args;                // Arguments
            int argCount;                  // Number of arguments
            bool arity_error_reported;     // Tracks if we've already reported an arity error
        } call;
        struct {
            ASTNode* value;                // Return value (NULL for void return)
        } returnStmt;
        struct {
            ASTNode* expression;           // Expression to cast
            ASTNode* targetType;           // Target type
            bool parenthesized;            // Whether the cast was explicitly parenthesized
        } cast;
        struct {
            char* name;                    // Struct name
            bool isPublic;                 // Whether the struct is public
            StructField* fields;           // Struct field definitions
            int fieldCount;                // Number of fields
        } structDecl;
        struct {
            char* structName;              // Name of struct being implemented
            bool isPublic;                 // Whether the impl block is public
            ASTNode** methods;             // Method definitions (function nodes)
            int methodCount;               // Number of methods
        } implBlock;
        struct {
            char* structName;              // Name of the struct being instantiated
            char* moduleAlias;             // Optional module alias qualifier (e.g., geometry.Point)
            const char* resolvedModuleName;// Fully qualified module name after resolution
            StructLiteralField* fields;    // Field assignments
            int fieldCount;
        } structLiteral;
        struct {
            ASTNode* object;               // Base expression or type identifier
            char* member;                  // Member name
            bool isMethod;                 // True if member resolves to method
            bool isInstanceMethod;         // True if method expects implicit self
            bool resolvesToEnum;           // True if the lookup hit an enum type/value
            bool resolvesToEnumVariant;    // True if the member is an enum variant
            int enumVariantIndex;          // Variant slot inside the enum definition
            int enumVariantArity;          // Number of payload fields the variant expects
            const char* enumTypeName;      // Cached enum type name for backend lowering
            bool resolvesToModule;         // True if member access resolves to a module export
            const char* moduleName;        // Fully qualified module name for the export
            const char* moduleAliasBinding;// Internal alias binding name used for symbol registration
            ModuleExportKind moduleExportKind; // Kind of export resolved from module
            uint16_t moduleRegisterIndex;  // Register index for exported globals/functions (if applicable)
        } member;
        struct {
            ASTNode* target;               // Member access node
            ASTNode* value;                // Assigned value
        } memberAssign;
        struct {
            char* name;            // Enum name
            bool isPublic;         // Whether the enum is public
            EnumVariant* variants; // Declared variants
            int variantCount;      // Number of variants
            char** genericParams;  // Optional generic parameter names
            int genericParamCount; // Number of generic parameters
        } enumDecl;
        struct {
            ASTNode* value;           // Enum expression being tested
            char* enumTypeName;       // Declared enum type name in the pattern
            char* variantName;        // Variant name being tested
            int variantIndex;         // Resolved variant slot (filled during typing)
            int expectedPayloadCount; // Expected payload arity from the pattern
        } enumMatchTest;
        struct {
            ASTNode* value;       // Enum expression providing payload
            char* enumTypeName;   // Enum type name from the pattern
            char* variantName;    // Variant supplying the payload
            int variantIndex;     // Resolved variant index (set during typing)
            int fieldIndex;       // Payload slot to extract
        } enumPayload;
        struct {
            ASTNode* value;       // Enum expression being matched
            char* enumTypeName;   // Enum type referenced by the pattern (if explicit)
            char** variantNames;  // Variants explicitly handled
            int variantCount;     // Number of explicit variant arms
            bool hasWildcard;     // Whether a wildcard arm exists
        } enumMatchCheck;
        struct {
            ASTNode* subject;     // Expression being matched
            char* tempName;       // Synthesized binding for the scrutinee
            MatchArm* arms;       // Parsed match arms
            int armCount;         // Number of match arms
            bool hasWildcard;     // Whether a wildcard arm exists
        } matchExpr;
    };
};

#endif // ORUS_AST_H
