#ifndef COMPILER_H
#define COMPILER_H

#include "public/common.h"
#include "vm/vm.h"
#include "ast.h"
#include "type/type.h"

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeCompiler(Compiler* compiler);
uint8_t allocateRegister(Compiler* compiler);
void freeRegister(Compiler* compiler, uint8_t reg);
bool compile(ASTNode* ast, Compiler* compiler, bool isModule);
bool compileNode(ASTNode* node, Compiler* compiler);

// Enhanced register allocation with lifetime tracking
void initRegisterAllocator(RegisterAllocator* allocator);
void freeRegisterAllocator(RegisterAllocator* allocator);
uint8_t allocateRegisterWithLifetime(Compiler* compiler, const char* name, ValueType type, bool isLoopVar);
void markVariableLastUse(Compiler* compiler, int localIndex, int instruction);
void endVariableLifetime(Compiler* compiler, int localIndex, int instruction);
uint8_t reuseOrAllocateRegister(Compiler* compiler, const char* name, ValueType type);
void optimizeLoopVariableLifetimes(Compiler* compiler, int loopStart, int loopEnd);


// Loop Invariant Code Motion (LICM) optimization
typedef struct {
    ASTNode** invariantNodes;       // Array of loop-invariant expressions
    int count;                      // Number of invariant expressions
    int capacity;                   // Capacity of invariantNodes array
    uint8_t* hoistedRegs;          // Registers for hoisted values
    int* originalInstructions;      // Original instruction positions
    bool* canHoist;                // Whether each expression can be safely hoisted
} LICMAnalysis;

void initLICMAnalysis(LICMAnalysis* analysis);
void freeLICMAnalysis(LICMAnalysis* analysis);
bool performLICM(Compiler* compiler, int loopStart, int loopEnd, LoopContext* loopCtx);
bool isLoopInvariant(ASTNode* expr, LoopContext* loopCtx, Compiler* compiler);
bool canSafelyHoist(ASTNode* expr, LoopContext* loopCtx);
void hoistInvariantCode(Compiler* compiler, LICMAnalysis* analysis, int preHeaderPos);
bool hasSideEffects(ASTNode* expr);
bool dependsOnLoopVariable(ASTNode* expr, LoopContext* loopCtx);
void collectLoopInvariantExpressions(ASTNode* node, LICMAnalysis* analysis, LoopContext* loopCtx, Compiler* compiler);

void emitByte(Compiler* compiler, uint8_t byte);
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2);
void emitConstant(Compiler* compiler, uint8_t reg, Value value);

// Compilation helpers
bool compileExpression(ASTNode* node, Compiler* compiler);
int compileExpressionToRegister(ASTNode* node, Compiler* compiler);
int compile_typed_expression_to_register(ASTNode* node, Compiler* compiler);
int compileExpressionToRegister_new(ASTNode* node, Compiler* compiler);

// Phase 3.1: Type inference integration for optimization
void initCompilerTypeInference(Compiler* compiler);
void freeCompilerTypeInference(Compiler* compiler);
Type* inferExpressionType(Compiler* compiler, ASTNode* expr);
bool resolveVariableType(Compiler* compiler, const char* name, Type* inferredType);
ValueType typeKindToValueType(TypeKind kind);
TypeKind valueTypeToTypeKind(ValueType vtype);

// Phase 3.2: Emit typed instructions when types are known
bool canEmitTypedInstruction(Compiler* compiler, ASTNode* left, ASTNode* right, ValueType* outType);
void emitTypedBinaryOp(Compiler* compiler, const char* op, ValueType type, uint8_t dst, uint8_t left, uint8_t right);

#define TYPED_EXPRESSIONS 1
#define TYPED_STATEMENTS 1
#define NO_JUMP (-1)

typedef enum {
    EXP_VOID,
    EXP_NIL,
    EXP_TRUE,
    EXP_FALSE,
    EXP_K,
    EXP_LOCAL,
    EXP_TEMP
} ExpKind;

typedef struct TypedExpDesc {
    ExpKind kind;
    ValueType type;
    bool isConstant;
    union {
        struct {
            int info;
            ValueType regType;
            bool isTemporary;
        } s;
        struct {
            Value value;
            int constIndex;
        } constant;
    } u;
    int t;
    int f;
} TypedExpDesc;

// Phase 2: TypedExpDesc-based statement compilation
int compile_typed_statement(ASTNode* node, Compiler* compiler);
void compile_typed_if_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result);
void compile_typed_while_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result);
void compile_typed_for_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result);
void compile_typed_block_statement(ASTNode* node, Compiler* compiler, TypedExpDesc* result);
void compile_typed_call(Compiler* compiler, ASTNode* node, TypedExpDesc* result);

#endif // COMPILER_H
