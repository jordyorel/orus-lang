// register_vm.h - Register-based VM header
#ifndef REGISTER_VM_H
#define REGISTER_VM_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vm_constants.h"
#include "vm_string_ops.h"

// Register-based VM configuration
#define REGISTER_COUNT VM_MAX_REGISTERS
#define FRAMES_MAX VM_MAX_CALL_FRAMES
#define STACK_INIT_CAPACITY 256
#define TRY_MAX 16
#define MAX_NATIVES 256
#define UINT8_COUNT 256

// Value types
typedef enum {
    VAL_BOOL,
    VAL_I32,
    VAL_I64,
    VAL_U32,
    VAL_U64,
    VAL_F64,
    VAL_NUMBER,  // Generic number type for literals
    VAL_STRING,
    VAL_ARRAY,
    VAL_ERROR,
    VAL_RANGE_ITERATOR,
    VAL_FUNCTION,
    VAL_CLOSURE
} ValueType;

// Forward declarations
typedef struct ObjString ObjString;
typedef struct ObjArray ObjArray;
typedef struct ObjError ObjError;
typedef struct ObjRangeIterator ObjRangeIterator;
typedef struct ObjFunction ObjFunction;
typedef struct ObjClosure ObjClosure;
typedef struct ObjUpvalue ObjUpvalue;
typedef struct Obj Obj;

// Value representation
typedef struct {
    ValueType type;
    union {
        bool boolean;
        int32_t i32;
        int64_t i64;
        uint32_t u32;
        uint64_t u64;
        double f64;
        double number;  // Generic number for literals
        Obj* obj;
    } as;
} Value;

// Object types
typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_ERROR,
    OBJ_RANGE_ITERATOR,
    OBJ_FUNCTION,
    OBJ_CLOSURE,
    OBJ_UPVALUE
} ObjType;

#define OBJ_TYPE_COUNT 7

// Object header
struct Obj {
    ObjType type;
    struct Obj* next;
    bool isMarked;
};

// String object
struct ObjString {
    Obj obj;
    int length;
    char* chars;
    StringRope* rope;
    uint32_t hash;
};

// Array object
struct ObjArray {
    Obj obj;
    int length;
    int capacity;
    Value* elements;
};

// Error object
typedef enum {
    ERROR_RUNTIME,
    ERROR_TYPE,
    ERROR_NAME,
    ERROR_INDEX,
    ERROR_KEY,
    ERROR_VALUE,
    ERROR_ARGUMENT,
    ERROR_IMPORT,
    ERROR_ATTRIBUTE,
    ERROR_UNIMPLEMENTED,
    ERROR_SYNTAX,
    ERROR_INDENT,
    ERROR_TAB,
    ERROR_RECURSION,
    ERROR_IO,
    ERROR_OS,
    ERROR_EOF
} ErrorType;

struct ObjError {
    Obj obj;
    ErrorType type;
    ObjString* message;
    struct {
        const char* file;
        int line;
        int column;
    } location;
};

// Range iterator
struct ObjRangeIterator {
    Obj obj;
    int64_t current;
    int64_t end;
};

// Chunk (bytecode container)
typedef struct {
    int count;
    int capacity;
    uint8_t* code;
    int* lines;
    int* columns;
    struct {
        int count;
        int capacity;
        Value* values;
    } constants;
} Chunk;

// Function object
struct ObjFunction {
    Obj obj;
    int arity;
    int upvalueCount;
    Chunk* chunk;
    ObjString* name;
};

// Upvalue object (for closure capture)
struct ObjUpvalue {
    Obj obj;
    Value* location;        // Points to value (stack or heap)
    Value closed;           // Heap storage when closed
    struct ObjUpvalue* next; // Linked list for GC
};

// Closure object (for capturing upvalues)
struct ObjClosure {
    Obj obj;
    ObjFunction* function;
    ObjUpvalue** upvalues;  // Array of upvalue pointers
    int upvalueCount;       // Number of upvalues
};

// Source location
typedef struct {
    const char* file;
    int line;
    int column;
} SrcLocation;

// Type system
typedef enum {
    TYPE_UNKNOWN,
    TYPE_ERROR,    // Special error type for failed type inference
    TYPE_I32,
    TYPE_I64,
    TYPE_U32,
    TYPE_U64,
    TYPE_F64,
    TYPE_BOOL,
    TYPE_STRING,
    TYPE_VOID,
    TYPE_ARRAY,
    TYPE_FUNCTION,
    TYPE_ANY,
    TYPE_VAR,
    TYPE_GENERIC,
    TYPE_INSTANCE
} TypeKind;

// Forward declarations for type system
typedef struct TypeVar TypeVar;
typedef struct TypeScheme TypeScheme;
typedef struct TypeEnv TypeEnv;
typedef struct TypeArena TypeArena;
typedef struct HashMap HashMap;
typedef struct TypeExtension TypeExtension;

// Type Arena structure
struct TypeArena {
    uint8_t* memory;
    size_t size;
    size_t used;
    struct TypeArena* next;
};

#define ARENA_SIZE (64 * 1024)

typedef struct Type Type;
struct Type {
    TypeKind kind;
    union {
        struct {
            Type* elementType;
        } array;
        struct {
            int arity;
            Type** paramTypes;
            Type* returnType;
        } function;
        struct {
            void* var;  // TypeVar* but using void* to avoid forward declaration issues
        } var;
        struct {
            char* name;
            int paramCount;
            Type** params;
        } generic;
        struct {
            Type* base;
            Type** args;
            int argCount;
        } instance;
    } info;
    TypeExtension* ext;
};

// Function
typedef struct {
    int start;
    int arity;
    Chunk* chunk;
} Function;

// Native function
typedef Value (*NativeFn)(int argCount, Value* args);
typedef struct {
    ObjString* name;
    NativeFn function;
    int arity;
    Type* returnType;
} NativeFunction;

// Phase 1: Enhanced CallFrame structure for hierarchical register windows
typedef struct CallFrame {
    Value registers[FRAME_REGISTERS];     // Function-local registers
    struct CallFrame* parent;             // Parent scope
    struct CallFrame* next;               // Call stack linkage
    
    // Frame metadata
    uint16_t register_count;              // Registers in use
    uint16_t spill_start;                 // First spilled register ID (Phase 2)
    uint8_t module_id;                    // Module this frame belongs to
    uint8_t flags;                        // Frame properties
    
    // Legacy compatibility
    uint8_t* returnAddress;
    Chunk* previousChunk;
    uint8_t baseRegister;                // Base register for this frame (16-bit for extended registers)
    uint8_t functionIndex;
    uint16_t parameterBaseRegister;       // Base register for function parameters (frame/spill space)
    uint8_t savedRegisterCount;           // Number of saved registers
    uint16_t savedRegisterStart;          // Starting register index for saved registers
    Value savedRegisters[112];            // Saved frame (64) + temp (48) registers
} CallFrame;

// Shared function for parameter register allocation (used by both compiler and VM)
static inline uint16_t calculateParameterBaseRegister(int argCount) {
    // Hierarchical register allocation for unlimited parameters
    // Always start parameters at frame register 256 regardless of count
    // Individual parameters beyond frame space will spill automatically
    (void)argCount; // Suppress unused parameter warning
    return FRAME_REG_START;  // 256 - all parameters start here
}

// Phase 1: Register File Architecture
typedef struct RegisterFile {
    // Core register banks
    Value globals[GLOBAL_REGISTERS];      // Global state (preserve existing behavior)
    Value temps[TEMP_REGISTERS];          // Short-lived values
    
    // Dynamic frame management
    CallFrame* current_frame;             // Active function frame
    CallFrame* frame_stack;               // Call stack of frames
    
    // Phase 2: Spill area for unlimited scaling
    struct SpillManager* spilled_registers;  // When registers exhausted
    struct RegisterMetadata* metadata;      // Tracking register state
    
    // Phase 3: Module system
    struct ModuleManager* module_manager;   // Module register management
    
    // Phase 4: Advanced register optimizations
    struct RegisterCache* cache;            // Multi-level register caching
} RegisterFile;

// Phase 2: Register metadata for spilling
typedef struct RegisterMetadata {
    uint8_t is_temp : 1;        // Temporary register
    uint8_t is_global : 1;      // Global register  
    uint8_t is_spilled : 1;     // In spill area
    uint8_t refcount : 5;       // Reference counting
    uint8_t last_used;          // LRU tracking
} RegisterMetadata;

// Try frame
typedef struct {
    uint8_t* handler;
    uint8_t varIndex;
    int stackDepth;
} TryFrame;

// Module system
typedef struct {
    char* name;
    Value value;
    int index;
} Export;

typedef struct {
    char* name;
    char* module_name;
    Chunk* bytecode;
    Export exports[UINT8_COUNT];
    int export_count;
    bool executed;
    char* disk_path;
    long mtime;
    bool from_embedded;
} Module;

// Forward declaration to avoid circular dependency
typedef struct ASTNode ASTNode;

// Variable info
typedef struct {
    ObjString* name;
    int length;
} VariableInfo;

// Register-based opcodes
typedef enum {
    // Constants and literals
    OP_LOAD_CONST,  // reg, const_index - Load constant into register
    OP_LOAD_TRUE,   // reg - Load true into register
    OP_LOAD_FALSE,  // reg - Load false into register

    // Register operations
    OP_MOVE,          // dst, src - Move value between registers
    OP_LOAD_GLOBAL,   // reg, global_index - Load global into register
    OP_STORE_GLOBAL,  // global_index, reg - Store register to global

    // Arithmetic - register-based (dst, src1, src2)
    OP_ADD_I32_R,
    OP_SUB_I32_R,
    OP_MUL_I32_R,
    OP_DIV_I32_R,
    OP_MOD_I32_R,
    OP_INC_I32_R,
    OP_DEC_I32_R,

    OP_ADD_I64_R,
    OP_SUB_I64_R,
    OP_MUL_I64_R,
    OP_DIV_I64_R,
    OP_MOD_I64_R,

    OP_ADD_U32_R,
    OP_SUB_U32_R,
    OP_MUL_U32_R,
    OP_DIV_U32_R,
    OP_MOD_U32_R,

    OP_ADD_U64_R,
    OP_SUB_U64_R,
    OP_MUL_U64_R,
    OP_DIV_U64_R,
    OP_MOD_U64_R,

    OP_ADD_F64_R,
    OP_SUB_F64_R,
    OP_MUL_F64_R,
    OP_DIV_F64_R,
    OP_MOD_F64_R,

    // Bitwise operations
    OP_AND_I32_R,
    OP_OR_I32_R,
    OP_XOR_I32_R,
    OP_NOT_I32_R,
    OP_SHL_I32_R,
    OP_SHR_I32_R,

    // Comparison (dst, src1, src2)
    OP_EQ_R,
    OP_NE_R,
    OP_LT_I32_R,
    OP_LE_I32_R,
    OP_GT_I32_R,
    OP_GE_I32_R,

    OP_LT_I64_R,
    OP_LE_I64_R,
    OP_GT_I64_R,
    OP_GE_I64_R,

    OP_LT_F64_R,
    OP_LE_F64_R,
    OP_GT_F64_R,
    OP_GE_F64_R,

    OP_LT_U32_R,
    OP_LE_U32_R,
    OP_GT_U32_R,
    OP_GE_U32_R,

    OP_LT_U64_R,
    OP_LE_U64_R,
    OP_GT_U64_R,
    OP_GE_U64_R,

    // Logical operations
    OP_AND_BOOL_R,
    OP_OR_BOOL_R,
    OP_NOT_BOOL_R,

    // Type conversions (dst, src)
    OP_I32_TO_F64_R,
    OP_I32_TO_I64_R,
    OP_I64_TO_I32_R,
    OP_I64_TO_F64_R,
    OP_F64_TO_I32_R,
    OP_F64_TO_I64_R,
    OP_BOOL_TO_I32_R,
    OP_I32_TO_BOOL_R,
    OP_I32_TO_U32_R,
    OP_U32_TO_I32_R,
    OP_F64_TO_U32_R,
    OP_U32_TO_F64_R,
    OP_I32_TO_U64_R,
    OP_I64_TO_U64_R,
    OP_U64_TO_I32_R,
    OP_U64_TO_I64_R,
    OP_U32_TO_U64_R,
    OP_U64_TO_U32_R,
    OP_F64_TO_U64_R,
    OP_U64_TO_F64_R,

    // String operations
    OP_CONCAT_R,
    OP_TO_STRING_R,

    // Array operations
    OP_MAKE_ARRAY_R,  // dst, start_reg, count
    OP_ARRAY_GET_R,   // dst, array_reg, index_reg
    OP_ARRAY_SET_R,   // array_reg, index_reg, value_reg
    OP_ARRAY_LEN_R,   // dst, array_reg

    // Control flow
    OP_JUMP,
    OP_JUMP_IF_R,      // condition_reg, offset
    OP_JUMP_IF_NOT_R,  // condition_reg, offset
    OP_LOOP,
    OP_GET_ITER_R,     // dst_iter, iterable_reg
    OP_ITER_NEXT_R,    // dst_value, iter_reg, has_value_reg

    // Function calls
    OP_CALL_R,         // func_reg, first_arg_reg, arg_count, result_reg
    OP_CALL_NATIVE_R,  // native_index, first_arg_reg, arg_count, result_reg
    OP_TAIL_CALL_R,    // func_reg, first_arg_reg, arg_count, result_reg (tail call optimization)
    OP_RETURN_R,       // value_reg (or no operand for void)
    OP_RETURN_VOID,

    // Frame register operations
    OP_LOAD_FRAME,     // reg, frame_offset - Load from frame register
    OP_STORE_FRAME,    // frame_offset, reg - Store to frame register
    OP_ENTER_FRAME,    // frame_size - Allocate new call frame
    OP_EXIT_FRAME,     // - Deallocate current call frame
    OP_MOVE_FRAME,     // dst_frame_offset, src_frame_offset - Move between frame registers

    // Spill register operations for unlimited parameters
    OP_LOAD_SPILL,     // reg, spill_id_high, spill_id_low - Load from spill register (16-bit ID)
    OP_STORE_SPILL,    // spill_id_high, spill_id_low, reg - Store to spill register (16-bit ID)

    // Module register operations
    OP_LOAD_MODULE,    // reg, module_id, module_offset - Load from module register
    OP_STORE_MODULE,   // module_id, module_offset, reg - Store to module register
    OP_LOAD_MODULE_NAME, // module_name_index - Load module by name
    OP_SWITCH_MODULE,  // module_id - Switch active module context
    OP_EXPORT_VAR,     // var_name_index, reg - Export variable from current module
    OP_IMPORT_VAR,     // var_name_index, src_module_id - Import variable from module

    // Closure operations
    OP_CLOSURE_R,      // dst_reg, function_reg, upvalue_count, upvalue_indices...
    OP_GET_UPVALUE_R,  // dst_reg, upvalue_index
    OP_SET_UPVALUE_R,  // upvalue_index, value_reg
    OP_CLOSE_UPVALUE_R, // local_reg (close upvalue pointing to this local)

    // I/O
    OP_PRINT_MULTI_R,     // first_reg, count, newline_flag
    OP_PRINT_MULTI_SEP_R, // first_reg, count, sep_reg, newline_flag
    OP_PRINT_R,           // reg
    OP_PRINT_NO_NL_R,     // reg

    // Short jump optimizations (1-byte offset instead of 2)
    OP_JUMP_SHORT,         // 1-byte forward jump (0-255)
    OP_JUMP_BACK_SHORT,    // 1-byte backward jump (0-255)
    OP_JUMP_IF_NOT_SHORT,  // Conditional short jump
    OP_LOOP_SHORT,         // Short loop (backward jump)

    // Typed register operations for performance (bypass Value boxing)
    OP_ADD_I32_TYPED,      // dst_reg, left_reg, right_reg
    OP_SUB_I32_TYPED,
    OP_MUL_I32_TYPED,
    OP_DIV_I32_TYPED,
    OP_MOD_I32_TYPED,
    
    OP_ADD_I64_TYPED,
    OP_SUB_I64_TYPED,
    OP_MUL_I64_TYPED,
    OP_DIV_I64_TYPED,
    OP_MOD_I64_TYPED,
    
    OP_ADD_F64_TYPED,
    OP_SUB_F64_TYPED,
    OP_MUL_F64_TYPED,
    OP_DIV_F64_TYPED,
    OP_MOD_F64_TYPED,
    
    OP_ADD_U32_TYPED,
    OP_SUB_U32_TYPED,
    OP_MUL_U32_TYPED,
    OP_DIV_U32_TYPED,
    OP_MOD_U32_TYPED,
    
    OP_ADD_U64_TYPED,
    OP_SUB_U64_TYPED,
    OP_MUL_U64_TYPED,
    OP_DIV_U64_TYPED,
    OP_MOD_U64_TYPED,
    
    // Typed comparisons
    OP_LT_I32_TYPED,
    OP_LE_I32_TYPED,
    OP_GT_I32_TYPED,
    OP_GE_I32_TYPED,
    
    OP_LT_I64_TYPED,
    OP_LE_I64_TYPED,
    OP_GT_I64_TYPED,
    OP_GE_I64_TYPED,
    
    OP_LT_F64_TYPED,
    OP_LE_F64_TYPED,
    OP_GT_F64_TYPED,
    OP_GE_F64_TYPED,
    
    OP_LT_U32_TYPED,
    OP_LE_U32_TYPED,
    OP_GT_U32_TYPED,
    OP_GE_U32_TYPED,
    
    OP_LT_U64_TYPED,
    OP_LE_U64_TYPED,
    OP_GT_U64_TYPED,
    OP_GE_U64_TYPED,
    
    // Typed loads
    OP_LOAD_I32_CONST,     // reg, value
    OP_LOAD_I64_CONST,     // reg, value
    OP_LOAD_F64_CONST,     // reg, value
    
    // Typed moves
    OP_MOVE_I32,           // dst_reg, src_reg
    OP_MOVE_I64,           // dst_reg, src_reg
    OP_MOVE_F64,           // dst_reg, src_reg
    
    // TODO: Removed mixed-type op for Rust-style strict typing

    
    // Built-in functions
    OP_TIME_STAMP,         // reg - Get current timestamp in nanoseconds
    
    // Fused instructions for better performance
    // Immediate arithmetic (constant folding optimizations)
    OP_ADD_I32_IMM,        // dst_reg, src_reg, immediate_value
    OP_SUB_I32_IMM,        // dst_reg, src_reg, immediate_value  
    OP_MUL_I32_IMM,        // dst_reg, src_reg, immediate_value
    OP_CMP_I32_IMM,        // dst_reg, src_reg, immediate_value -> bool
    
    // Load and operate patterns
    OP_LOAD_ADD_I32,       // dst_reg = memory[src_reg] + operand_reg
    OP_LOAD_CMP_I32,       // dst_reg = memory[src_reg] < operand_reg
    
    // Loop optimization fused instructions  
    OP_INC_CMP_JMP,        // increment_reg, limit_reg, jump_offset (fused i++; if(i<limit) jump)
    OP_DEC_CMP_JMP,        // decrement_reg, zero_test, jump_offset (fused i--; if(i>0) jump)
    
    // Multiply-add and other multi-operation fusions
    OP_MUL_ADD_I32,        // dst_reg = src1_reg * src2_reg + src3_reg (FMA pattern)
    OP_LOAD_INC_STORE,     // memory[address_reg]++ (atomic increment pattern)
    
    // Other
    OP_IMPORT_R,
    OP_GC_PAUSE,
    OP_GC_RESUME,
    OP_NEG_I32_R,    // dst_reg, src_reg - Negate i32 value
    
    // Extended opcodes for 16-bit register access (Phase 2)
    OP_LOAD_CONST_EXT,  // reg16, const16 - Load constant into extended register
    OP_MOVE_EXT,        // dst_reg16, src_reg16 - Move between extended registers
    OP_STORE_EXT,       // reg16, addr16 - Store extended register to memory
    OP_LOAD_EXT,        // reg16, addr16 - Load from memory to extended register
    
    OP_HALT
} OpCode;

// Loop context for break/continue statements
#include "internal/intvec.h"
#include "runtime/jumptable.h"

// Variable lifetime tracking for register optimization
typedef struct {
    int start;              // First instruction where variable is live
    int end;                // Last instruction where variable is live (-1 if still alive)
    uint8_t reg;            // Assigned register
    char* name;             // Variable name for debugging
    ValueType type;         // Variable type
    bool spilled;           // Whether variable was spilled to memory
    bool isLoopVar;         // Whether this is a loop induction variable
    
    // Enhanced lifetime analysis fields
    int firstUse;           // First use of this variable
    int lastUse;            // Last use of this variable  
    bool escapes;           // Whether variable escapes current scope
    bool nestedLoopUsage;   // Whether used in nested loops
    bool crossesLoopBoundary; // Whether lifetime crosses loop boundaries
    bool isShortLived;      // Whether variable has short lifetime
    bool isLoopInvariant;   // Whether variable is loop invariant
    int priority;           // Priority for register allocation
} LiveRange;

// Enhanced register allocator with lifetime tracking
typedef struct {
    LiveRange* ranges;      // Array of live ranges
    int count;              // Number of live ranges
    int capacity;           // Capacity of ranges array
    uint8_t* freeRegs;      // List of available registers
    int freeCount;          // Number of free registers
    int* lastUse;           // Last use instruction for each register [REGISTER_COUNT]
    bool* registers;        // Register allocation bitmap
    int spillCount;         // Number of spilled variables
} RegisterAllocator;


typedef struct {
    JumpTable breakJumps;     // Patches for break statements
    JumpTable continueJumps;  // Jump targets for continue
    int continueTarget;    // Target for continue (loop start)
    int scopeDepth;        // Scope depth when loop was entered
    const char* label;     // Optional loop label
    int loopVarIndex;      // Index of loop variable in locals array (-1 if none)
    int loopVarStartInstr; // Instruction where loop variable becomes live
} LoopContext;

// Loop Invariant Code Motion (LICM) instruction-level data structures
typedef struct {
    int instructionOffset;    // Offset in bytecode where invariant operation occurs
    uint8_t operation;        // The operation opcode
    uint8_t operand1;         // First operand register
    uint8_t operand2;         // Second operand register  
    uint8_t result;           // Result register
    bool canHoist;            // Whether this operation can be safely hoisted
    bool hasBeenHoisted;      // Whether this has already been hoisted
} InvariantNode;

typedef struct {
    InvariantNode* invariantNodes;  // Array of detected invariant operations
    int count;                      // Number of invariant operations found
    int capacity;                   // Capacity of arrays
    uint8_t* hoistedRegs;          // Registers used for hoisted values
    uint8_t* originalInstructions; // Original instruction opcodes
    bool* canHoist;                // Which operations can be hoisted
} InstructionLICMAnalysis;

// Forward declarations  
struct TypeInferer;

// Compile-time scope analysis structures
typedef struct ScopeVariable {
    char* name;              // Variable name
    ValueType type;          // Variable type
    int declarationPoint;    // Instruction where variable is declared
    int firstUse;            // First use of the variable
    int lastUse;             // Last use of the variable
    bool escapes;            // Whether variable escapes current scope
    bool isLoopVar;          // Whether this is a loop induction variable
    bool isLoopInvariant;    // Whether variable is loop invariant
    bool crossesLoopBoundary; // Whether lifetime crosses loop boundaries
    uint8_t reg;             // Assigned register
    int priority;            // Priority for register allocation
    
    // Closure capture analysis
    bool isCaptured;         // Whether variable is captured by nested function
    bool isUpvalue;          // Whether variable is an upvalue from parent scope
    int captureDepth;        // Scope depth where variable is captured
    int captureCount;        // Number of times variable is captured
    bool needsHeapAllocation; // Whether variable needs heap allocation for capture
    
    // Dead variable elimination
    bool isDead;             // Whether variable is never used after declaration
    bool isWriteOnly;        // Whether variable is only written to, never read
    bool isReadOnly;         // Whether variable is only read from, never written
    int useCount;            // Number of times variable is used
    int writeCount;          // Number of times variable is written to
    bool hasComplexLifetime; // Whether variable has complex lifetime across scopes
    
    struct ScopeVariable* next; // Linked list for hash table
} ScopeVariable;

typedef struct ScopeInfo {
    int depth;               // Scope depth
    int startInstruction;    // First instruction in scope
    int endInstruction;      // Last instruction in scope (-1 if still active)
    ScopeVariable* variables; // Hash table of variables in this scope
    int variableCount;       // Number of variables in scope
    bool isLoopScope;        // Whether this is a loop scope
    bool hasNestedScopes;    // Whether this scope contains nested scopes
    
    // Register allocation info
    uint8_t* usedRegisters;  // Bitmap of registers used in this scope
    int registerCount;       // Number of registers allocated in this scope
    
    // Lifetime analysis
    int* variableLifetimes;  // Array of variable lifetimes
    bool* canShareRegisters; // Which variables can share registers
    
    struct ScopeInfo* parent; // Parent scope
    struct ScopeInfo* children; // Child scopes
    struct ScopeInfo* sibling;  // Next sibling scope
} ScopeInfo;

typedef struct {
    ScopeInfo* rootScope;    // Root scope of the program
    ScopeInfo* currentScope; // Current active scope
    ScopeInfo** scopeStack;  // Stack of active scopes
    int scopeStackSize;      // Size of scope stack
    int scopeStackCapacity;  // Capacity of scope stack
    
    // Analysis results
    int totalScopes;         // Total number of scopes analyzed
    int maxNestingDepth;     // Maximum nesting depth found
    int totalVariables;      // Total number of variables
    
    // Register allocation optimization
    uint8_t* globalRegisterUsage; // Global register usage bitmap
    int* registerInterference;    // Register interference graph
    bool* canCoalesce;           // Which registers can be coalesced
    
    // Cross-scope optimization opportunities
    ScopeVariable** hoistableVariables; // Variables that can be hoisted
    int hoistableCount;                 // Number of hoistable variables
    
    // Lifetime analysis results
    int* variableLifespans;  // Lifespan of each variable
    bool* shortLivedVars;    // Variables with short lifespans
    bool* longLivedVars;     // Variables with long lifespans
    
    // Closure capture analysis
    ScopeVariable** capturedVariables; // Variables captured by closures
    int capturedCount;                  // Number of captured variables
    int* captureDepths;                 // Depth of each capture
    bool hasNestedFunctions;            // Whether code contains nested functions
    
    // Dead variable elimination
    ScopeVariable** deadVariables;      // Variables identified as dead
    int deadCount;                      // Number of dead variables
    ScopeVariable** writeOnlyVariables; // Variables that are only written to
    int writeOnlyCount;                 // Number of write-only variables
    int eliminatedInstructions;         // Number of instructions eliminated
    int savedRegisters;                 // Number of registers saved
} ScopeAnalyzer;

// Forward declaration to avoid circular dependency
struct LifetimeAnalyzer;

// Compiler state for register allocation
typedef struct {
    Chunk* chunk;
    const char* fileName;
    const char* source;
    uint16_t nextRegister;  // Next available register (supports full VM register space)
    int currentLine;       // Current line for error reporting
    int currentColumn;     // Current column for error reporting
    uint16_t maxRegisters;  // Maximum registers used (supports full VM register space)
    struct {
        char* name;
        uint16_t reg;  // Register allocation for local (supports full VM register space)
        bool isActive;
        int depth;
        bool isMutable;
        ValueType type; // Type of the variable
        int liveRangeIndex; // Index into register allocator's live ranges (-1 if none)
        bool isSpilled; // Whether variable has been spilled to memory
        // Phase 3 safe type tracking fields
        bool hasKnownType; // Whether we know the exact type of this variable
        ValueType knownType; // The known type (only valid if hasKnownType is true)
    } locals[MAX_LOCAL_VARIABLES];  // Support many local variables with spilling
    int localCount;
    int scopeDepth;
    int scopeStack[MAX_LOCAL_VARIABLES];  // Match locals array size
    LoopContext loopStack[16];  // Stack of nested loop contexts
    int loopDepth;              // Current loop nesting depth
    int loopStart;              // Start instruction of current loop
    JumpTable pendingJumps;     // Track all pending forward jumps for cascade updates
    RegisterAllocator regAlloc; // Enhanced register allocator with lifetime tracking
    struct TypeInferer* typeInferer;   // Type inference engine for Phase 3.1 optimization
    ScopeAnalyzer scopeAnalyzer; // Compile-time scope analysis and optimization
    // Phase 3.1: Compile-time register type tracking for aggressive optimization
    ValueType registerTypes[REGISTER_COUNT]; // Track known types of registers at compile time
    bool hadError;
    
    // Tail call optimization context
    void* tailCallContext;  // TailCallContext* - avoid circular dependency
    
    // Loop optimization framework
    struct {
        bool enabled;
        int unrollCount;
        int strengthReductionCount;
        int boundsEliminationCount;
        int licmCount;
        int totalOptimizations;
    } optimizer;
    
    // Function compilation context
    int currentFunctionParameterCount;  // Parameter count for dynamic register allocation
    
    // Phase 2.3: Comprehensive lifetime analysis and register reuse system
    struct LifetimeAnalyzer* lifetimeAnalyzer;  // Smart register reuse and lifetime tracking
} Compiler;

// Typed registers for optimization (unboxed values)
typedef struct {
    // Numeric registers (unboxed for performance)
    int32_t i32_regs[32];
    int64_t i64_regs[32];
    uint32_t u32_regs[32];
    uint64_t u64_regs[32];
    double f64_regs[32];
    bool bool_regs[32];

    // Heap object registers (boxed)
    Value heap_regs[32];

    // Register type tracking (for debugging/safety)
    uint8_t reg_types[256];  // Track which register bank each logical register maps to
} TypedRegisters;

// Register type enum
typedef enum {
    REG_TYPE_NONE = 0,
    REG_TYPE_I32,
    REG_TYPE_I64,
    REG_TYPE_U32,
    REG_TYPE_U64,
    REG_TYPE_F64,
    REG_TYPE_BOOL,
    REG_TYPE_HEAP
} RegisterType;

// Profiling counters
typedef struct {
    uint64_t instruction_counts[VM_DISPATCH_TABLE_SIZE];
} VMProfile;

// Phase 1: Register access functions (forward declarations)
// These are implemented in register_file.c

// VM state
typedef struct {
    // Phase 1: New register file architecture
    RegisterFile register_file;
    
    // Legacy registers (for backward compatibility during transition)
    Value registers[REGISTER_COUNT];
    
    // Typed registers for performance optimization
    TypedRegisters typed_regs;

    // Call frames
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    // Bytecode execution
    Chunk* chunk;
    uint8_t* ip;

    // Globals
    Value globals[UINT8_COUNT];
    Type* globalTypes[UINT8_COUNT];
    bool publicGlobals[UINT8_COUNT];
    bool mutableGlobals[UINT8_COUNT];
    VariableInfo variableNames[UINT8_COUNT];
    int variableCount;

    // Functions
    Function functions[UINT8_COUNT];
    Type* functionDecls[UINT8_COUNT];
    int functionCount;

    // Native functions
    NativeFunction nativeFunctions[MAX_NATIVES];
    int nativeFunctionCount;

    // Error handling
    TryFrame tryFrames[TRY_MAX];
    int tryFrameCount;
    Value lastError;

    // Module system
    ObjString* loadedModules[UINT8_COUNT];
    int moduleCount;

    // Memory management
    Obj* objects;
    size_t bytesAllocated;
    size_t gcCount;
    bool gcPaused;

    // Upvalue management
    ObjUpvalue* openUpvalues;  // Linked list of open upvalues

    // Execution state
    uint64_t instruction_count;
    ASTNode* astRoot;
    const char* filePath;
    int currentLine;
    int currentColumn;

    double lastExecutionTime;

    VMProfile profile;

    // Configuration
    bool trace;
    const char* stdPath;
    const char* cachePath;
    bool devMode;
    bool suppressWarnings;
    bool promotionHints;
    bool isShuttingDown;  // Flag to indicate VM is in cleanup/shutdown phase
    
    // Call frame stack pointers
    CallFrame* callFrames;     // Array of call frames (for legacy compatibility)
    CallFrame* currentCallFrame; // Current active call frame
    CallFrame* frameStack;     // Stack of call frames
} VM;

// Global VM instance
extern VM vm;

// Interpretation results
typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR
} InterpretResult;

// Value macros
#define BOOL_VAL(value) ((Value){VAL_BOOL, {.boolean = value}})
#define I32_VAL(value) ((Value){VAL_I32, {.i32 = value}})
#define I64_VAL(value) ((Value){VAL_I64, {.i64 = value}})
#define U32_VAL(value) ((Value){VAL_U32, {.u32 = value}})
#define U64_VAL(value) ((Value){VAL_U64, {.u64 = value}})
#define F64_VAL(value) ((Value){VAL_F64, {.f64 = value}})
#define STRING_VAL(value) ((Value){VAL_STRING, {.obj = (Obj*)value}})
#define ARRAY_VAL(obj) ((Value){VAL_ARRAY, {.obj = (Obj*)obj}})
#define ERROR_VAL(object) ((Value){VAL_ERROR, {.obj = (Obj*)object}})
#define FUNCTION_VAL(value) ((Value){VAL_FUNCTION, {.obj = (Obj*)value}})
#define CLOSURE_VAL(value) ((Value){VAL_CLOSURE, {.obj = (Obj*)value}})

#define AS_BOOL(value) ((value).as.boolean)
#define AS_I32(value) ((value).as.i32)
#define AS_I64(value) ((value).as.i64)
#define AS_U32(value) ((value).as.u32)
#define AS_U64(value) ((value).as.u64)
#define AS_F64(value) ((value).as.f64)
#define AS_OBJ(value) ((value).as.obj)
#define AS_STRING(value) ((ObjString*)(value).as.obj)
#define AS_ARRAY(value) ((ObjArray*)(value).as.obj)
#define AS_ERROR(value) ((ObjError*)(value).as.obj)
#define AS_RANGE_ITERATOR(value) ((ObjRangeIterator*)(value).as.obj)
#define AS_FUNCTION(value) ((ObjFunction*)(value).as.obj)
#define AS_CLOSURE(value) ((ObjClosure*)(value).as.obj)

#define IS_BOOL(value) ((value).type == VAL_BOOL)
#define IS_I32(value) ((value).type == VAL_I32)
#define IS_I64(value) ((value).type == VAL_I64)
#define IS_U32(value) ((value).type == VAL_U32)
#define IS_U64(value) ((value).type == VAL_U64)
#define IS_F64(value) ((value).type == VAL_F64)
#define IS_STRING(value) ((value).type == VAL_STRING)
#define IS_ARRAY(value) ((value).type == VAL_ARRAY)
#define IS_ERROR(value) ((value).type == VAL_ERROR)
#define IS_RANGE_ITERATOR(value) ((value).type == VAL_RANGE_ITERATOR)
#define IS_FUNCTION(value) ((value).type == VAL_FUNCTION)
#define IS_CLOSURE(value) ((value).type == VAL_CLOSURE)

// Function declarations
/** Initialize the global VM state and subsystems. */
void initVM(void);

/** Release all resources associated with the VM. */
void freeVM(void);

/** Perform startup warmup routines for optimal JIT selection. */
void warmupVM(void);
#if USE_COMPUTED_GOTO
extern void* vm_dispatch_table[OP_HALT + 1];
void initDispatchTable(void);
#endif
/** Execute Orus source code provided as a null-terminated string. */
InterpretResult interpret(const char* source);

/** Execute an Orus module loaded from disk. */
InterpretResult interpret_module(const char* path);

// Chunk operations
void initChunk(Chunk* chunk);
void freeChunk(Chunk* chunk);
void writeChunk(Chunk* chunk, uint8_t byte, int line, int column);
int addConstant(Chunk* chunk, Value value);

// Value operations
void printValue(Value value);
bool valuesEqual(Value a, Value b);

// Object allocation
ObjString* allocateString(const char* chars, int length);
ObjArray* allocateArray(int capacity);
ObjError* allocateError(ErrorType type, const char* message,
                        SrcLocation location);
ObjFunction* allocateFunction(void);
ObjClosure* allocateClosure(ObjFunction* function);
ObjUpvalue* allocateUpvalue(Value* slot);

// Memory management
void collectGarbage(void);
void freeObjects(void);

// Upvalue management
ObjUpvalue* captureUpvalue(Value* local);
void closeUpvalues(Value* last);

// Type system
void initTypeSystem(void);
Type* getPrimitiveType(TypeKind kind);

// Debug functions
void disassembleChunk(Chunk* chunk, const char* name);
int disassembleInstruction(Chunk* chunk, int offset);

#endif  // REGISTER_VM_H