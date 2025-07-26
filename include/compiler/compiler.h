#ifndef COMPILER_H
#define COMPILER_H

#include "public/common.h"
#include "vm/vm.h"
#include "ast.h"
#include "type/type.h"

void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeCompiler(Compiler* compiler);
bool compile(ASTNode* ast, Compiler* compiler, bool isModule);
bool compileNode(ASTNode* node, Compiler* compiler);

// Enhanced register allocation with lifetime tracking
void initRegisterAllocator(RegisterAllocator* allocator);
void freeRegisterAllocator(RegisterAllocator* allocator);
uint16_t allocateRegisterWithLifetime(Compiler* compiler, const char* name, ValueType type, bool isLoopVar);
void markVariableLastUse(Compiler* compiler, int localIndex, int instruction);
void endVariableLifetime(Compiler* compiler, int localIndex, int instruction);
uint16_t reuseOrAllocateRegister(Compiler* compiler, const char* name, ValueType type);
void optimizeLoopVariableLifetimes(Compiler* compiler, int loopStart, int loopEnd);


// Loop Invariant Code Motion (LICM) optimization
typedef struct {
    ASTNode** invariantNodes;       // Array of loop-invariant expressions
    int count;                      // Number of invariant expressions
    int capacity;                   // Capacity of invariantNodes array
    uint16_t* hoistedRegs;          // Registers for hoisted values (supports full VM register space)
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

// Shared utility functions (implemented in hybrid_compiler.c)
uint16_t allocateRegister(Compiler* compiler);
void freeRegister(Compiler* compiler, uint16_t reg);
void emitByte(Compiler* compiler, uint8_t byte);
void emitShort(Compiler* compiler, uint16_t value);
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2);
void emitConstant(Compiler* compiler, uint16_t reg, Value value);
void emitConstantExt(Compiler* compiler, uint16_t reg, Value value);
void emitMoveExt(Compiler* compiler, uint16_t dst_reg, uint16_t src_reg);
void emitMove(Compiler* compiler, uint16_t dst_reg, uint16_t src_reg);

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

// Phase 2.3: Comprehensive Lifetime Analysis & Register Reuse System
typedef struct {
    uint16_t reg;                  // Register number
    int birth_instruction;         // When register was allocated
    int last_use_instruction;      // Last use of this register
    bool is_active;               // Currently in use
    bool is_reusable;             // Can be reused after last_use
    ValueType type;               // Type stored in register
    char* variable_name;          // Variable name (for debugging)
} RegisterLifetime;

typedef struct {
    RegisterLifetime* lifetimes;   // Array of register lifetimes
    int count;                    // Number of tracked registers
    int capacity;                 // Capacity of lifetimes array
    
    // Free register pools by tier for optimal reuse
    uint16_t* free_global_regs;   // Pool of free global registers (0-255)
    int free_global_count;
    uint16_t* free_frame_regs;    // Pool of free frame registers (256-319)  
    int free_frame_count;
    uint16_t* free_temp_regs;     // Pool of free temp registers (320-351)
    int free_temp_count;
    uint16_t* free_module_regs;   // Pool of free module registers (352-479)
    int free_module_count;
    
    int current_instruction;      // Current bytecode instruction counter
} LifetimeAnalyzer;

// Lifetime analysis functions
void initLifetimeAnalyzer(LifetimeAnalyzer* analyzer);
void freeLifetimeAnalyzer(LifetimeAnalyzer* analyzer);
uint16_t allocateRegisterSmart(Compiler* compiler, const char* varName, ValueType type);
void markRegisterLastUse(Compiler* compiler, uint16_t reg, int instruction);
void freeRegisterSmart(Compiler* compiler, uint16_t reg);
void optimizeRegisterLifetimes(Compiler* compiler);
uint16_t reuseDeadRegister(Compiler* compiler, ValueType type);
void analyzeRegisterLifetimes(Compiler* compiler, ASTNode* ast);

#endif // COMPILER_H
