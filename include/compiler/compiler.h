#ifndef ORUS_COMPILER_H
#define ORUS_COMPILER_H

#include "vm/vm.h"
#include "compiler/ast.h"
#include "compiler/typed_ast.h"
#include <stdio.h>
#include <stdbool.h>

// Forward declarations for dual register system
struct DualRegisterAllocator;
struct RegisterAllocation;

// ===== EXISTING SINGLE-PASS COMPILER (LEGACY) =====
// Compiler structure is defined in vm/vm.h

// Legacy compiler interface functions
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName, const char* source);
void freeCompiler(Compiler* compiler);
bool compileProgram(ASTNode* ast, Compiler* compiler, bool isModule);
void emitByte(Compiler* compiler, uint8_t byte);

// ===== NEW MULTI-PASS COMPILER INFRASTRUCTURE =====

// Forward declarations
typedef struct MultiPassRegisterAllocator MultiPassRegisterAllocator;
typedef struct SymbolTable SymbolTable;
typedef struct ScopeStack ScopeStack;
// Simple constant pool matching VM's Chunk constants structure
typedef struct ConstantPool {
    int count;
    int capacity;
    Value* values;
} ConstantPool;
typedef struct ErrorReporter ErrorReporter;
typedef struct OptimizationContext OptimizationContext;

// Bytecode buffer for VM instruction generation
typedef struct BytecodeBuffer {
    uint8_t* instructions;     // VM instruction bytes
    int capacity;              // Buffer capacity
    int count;                 // Current instruction count
    
    // Metadata for debugging
    int* source_lines;         // Line numbers for each instruction  
    int* source_columns;       // Column numbers for each instruction
    
    // Jump patching
    struct JumpPatch* patches; // Forward jump patches
    int patch_count;
    int patch_capacity;
} BytecodeBuffer;

typedef struct JumpPatch {
    int instruction_offset;    // Where the jump instruction is
    int target_label;          // Label to patch to
} JumpPatch;

// Main compilation context for multi-pass compiler
typedef struct CompilerContext {
    TypedASTNode* input_ast;           // Input from HM type inference
    TypedASTNode* optimized_ast;       // After optimization pass
    
    // Register allocation - DUAL SYSTEM
    MultiPassRegisterAllocator* allocator;        // Legacy allocator (compatibility)
    struct DualRegisterAllocator* dual_allocator; // New dual system (forward declaration)
    int next_temp_register;            // R192-R239 temps
    int next_local_register;           // R64-R191 locals
    int next_global_register;          // R0-R63 globals
    
    // Symbol management (TODO: implement in Phase 2)
    SymbolTable* symbols;              // Variable → register mapping
    ScopeStack* scopes;                // Lexical scope tracking
    
    // Bytecode output
    BytecodeBuffer* bytecode;          // Final VM instructions
    ConstantPool* constants;           // Literal values (42, "hello")
    
    // Error handling (TODO: implement in Phase 2)
    ErrorReporter* errors;             // Compilation error tracking
    bool has_compilation_errors;       // Track if any compilation errors occurred
    
    // Debugging
    bool enable_visualization;         // Show TypedAST between passes
    FILE* debug_output;               // Where to output debug info
    
    // Optimization settings (TODO: implement in Phase 2)
    OptimizationContext* opt_ctx;
    
    // Loop control context for break/continue statements
    int current_loop_start;            // Bytecode offset of current loop start
    int current_loop_end;              // Bytecode offset of current loop end
} CompilerContext;

// Bytecode emission functions
BytecodeBuffer* init_bytecode_buffer(void);
void free_bytecode_buffer(BytecodeBuffer* buffer);
void emit_byte_to_buffer(BytecodeBuffer* buffer, uint8_t byte);
void emit_instruction_to_buffer(BytecodeBuffer* buffer, uint8_t opcode, uint8_t reg1, uint8_t reg2, uint8_t reg3);

// DUAL REGISTER SYSTEM - Smart Instruction Emission
void emit_arithmetic_instruction_smart(CompilerContext* ctx, const char* op, 
                                     struct RegisterAllocation* dst, 
                                     struct RegisterAllocation* left, 
                                     struct RegisterAllocation* right);

OpCode get_typed_opcode(const char* op, RegisterType type);
OpCode get_standard_opcode(const char* op, RegisterType type);
int emit_jump_placeholder(BytecodeBuffer* buffer, uint8_t jump_opcode);
void patch_jump(BytecodeBuffer* buffer, int jump_offset, int target_offset);

// Constant pool functions
ConstantPool* init_constant_pool(void);
void free_constant_pool(ConstantPool* pool);
int add_constant(ConstantPool* pool, Value value);
Value get_constant(ConstantPool* pool, int index);

// Main multi-pass compilation functions
CompilerContext* init_compiler_context(TypedASTNode* typed_ast);
bool compile_to_bytecode(CompilerContext* ctx);
void free_compiler_context(CompilerContext* ctx);

// Pipeline coordination
bool run_optimization_pass(CompilerContext* ctx);
bool run_codegen_pass(CompilerContext* ctx);

#endif // ORUS_COMPILER_H