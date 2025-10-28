// Orus Language Project

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
typedef struct CompilerProfilingFeedback CompilerProfilingFeedback;

// Bytecode buffer for VM instruction generation
typedef struct BytecodeBuffer {
    uint8_t* instructions;     // VM instruction bytes
    int capacity;              // Buffer capacity
    int count;                 // Current instruction count

    // Metadata for debugging
    int* source_lines;         // Line numbers for each instruction
    int* source_columns;       // Column numbers for each instruction
    const char** source_files; // Source file for each instruction (optional)

    // Current emission location (threaded from AST nodes)
    SrcLocation current_location;
    bool has_current_location;

    // Jump patching
    struct JumpPatch* patches; // Forward/backward jump patches
    int patch_count;
    int patch_capacity;

} BytecodeBuffer;

typedef struct JumpPatch {
    int instruction_offset;    // Offset of the jump opcode
    int operand_offset;        // Offset of the encoded jump operand
    int operand_size;          // Number of bytes reserved for the operand
    int target_label;          // Resolved target (for diagnostics)
    uint8_t opcode;            // Original opcode emitted for the jump
} JumpPatch;

// Upvalue capture information for closures
typedef struct {
    bool isLocal;   // Captured variable comes from enclosing function's locals
    uint8_t index;  // Register index or upvalue slot in enclosing function
} UpvalueInfo;

// Main compilation context for multi-pass compiler
typedef struct CompilerContext {
    TypedASTNode* input_ast;           // Input from HM type inference
    TypedASTNode* optimized_ast;       // After optimization pass
    
    // Register allocation - DUAL SYSTEM
    struct DualRegisterAllocator* allocator;      // Unified dual allocator facade
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

    // Function compilation context
    bool compiling_function;           // Are we currently compiling inside a function?
    int function_scope_depth;          // Scope depth of current function's root
    
    // Debugging
    bool enable_visualization;         // Show TypedAST between passes
    bool dump_bytecode;                // Emit bytecode dumps when requested
    FILE* debug_output;               // Where to output debug info
    
    // Optimization settings (TODO: implement in Phase 2)
    OptimizationContext* opt_ctx;
    
    // Loop control context for break/continue statements
    int current_loop_start;            // Bytecode offset of current loop start
    int current_loop_end;              // Bytecode offset of current loop end
    int current_loop_continue;         // Bytecode offset for continue statements (for for-loops)
    uint16_t current_loop_id;          // Active loop identifier (legacy branch-cache metadata)
    uint16_t next_loop_id;             // Monotonic loop identifier allocator
    int* break_statements;             // Array of break statement bytecode offsets to patch
    int break_count;                   // Number of break statements in current loop
    int break_capacity;                // Capacity of break_statements array
    int* continue_statements;          // Array of continue statement bytecode offsets to patch
    int continue_count;                // Number of continue statements in current loop
    int continue_capacity;             // Capacity of continue_statements array

    // Branch tracking for conditional initialization semantics
    int branch_depth;
    
    // Function compilation context (NEW)
    int current_function_index;        // Currently compiling function index (-1 if global)
    BytecodeBuffer** function_chunks;  // Separate bytecode for each function
    int* function_arities;             // Arities for each function
    char** function_names;             // Debug names preserved for specialization feedback
    BytecodeBuffer** function_specialized_chunks; // Specialized variants generated from profiling data
    BytecodeBuffer** function_deopt_stubs;        // Bytecode stubs used for deoptimization bookkeeping
    uint64_t* function_hot_counts;     // Profiling hit counts attached to each function slot
    int function_count;                // Number of functions compiled
    int function_capacity;             // Capacity of function_chunks array

    // Closure compilation context
    UpvalueInfo* upvalues;             // Captured variables for current function
    int upvalue_count;                 // Number of captured upvalues
    int upvalue_capacity;              // Capacity of upvalues array

    // Module export tracking
    bool is_module;                    // Whether we're compiling a module
    ModuleExportEntry* module_exports; // Collected exports
    int module_export_count;           // Number of collected exports
    int module_export_capacity;        // Allocated capacity for exports
    ModuleImportEntry* module_imports; // Collected imports
    int module_import_count;           // Number of collected imports
    int module_import_capacity;        // Allocated capacity for imports

    CompilerProfilingFeedback* profiling_feedback; // Snapshot of VM profiling data driving specialization
} CompilerContext;

// Bytecode emission functions
BytecodeBuffer* init_bytecode_buffer(void);
void free_bytecode_buffer(BytecodeBuffer* buffer);
void emit_byte_to_buffer(BytecodeBuffer* buffer, uint8_t byte);

// Location helpers for bytecode emission metadata
void bytecode_set_location(BytecodeBuffer* buffer, SrcLocation location);
void bytecode_set_synthetic_location(BytecodeBuffer* buffer);

void emit_word_to_buffer(BytecodeBuffer* buffer, uint16_t word);
void emit_instruction_to_buffer(BytecodeBuffer* buffer, uint8_t opcode, uint8_t reg1, uint8_t reg2, uint8_t reg3);

// DUAL REGISTER SYSTEM - Smart Instruction Emission
void emit_arithmetic_instruction_smart(CompilerContext* ctx, const char* op, 
                                     struct RegisterAllocation* dst, 
                                     struct RegisterAllocation* left, 
                                     struct RegisterAllocation* right);

OpCode get_typed_opcode(const char* op, RegisterType type);
OpCode get_standard_opcode(const char* op, RegisterType type);
int emit_jump_placeholder(BytecodeBuffer* buffer, uint8_t jump_opcode);
bool patch_jump(BytecodeBuffer* buffer, int patch_index, int target_offset);
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

// Function compilation management (NEW)
int register_function(CompilerContext* ctx, const char* name, int arity, BytecodeBuffer* chunk);
BytecodeBuffer* get_function_chunk(CompilerContext* ctx, int function_index);
void finalize_functions_to_vm(CompilerContext* ctx); // Copy compiled functions to VM

#endif // ORUS_COMPILER_H
