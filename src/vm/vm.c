// register_vm.c - Register-based VM implementation
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "builtins.h"
#include "common.h"
#include "compiler.h"
#include "parser.h"
#include "memory.h"
#include "builtins.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static double get_time_vm() {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

// High-resolution timer for profiling
static double now_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1e9 / freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + ts.tv_nsec;
#endif
}

// Global VM instance
VM vm;

#if USE_COMPUTED_GOTO
void* vm_dispatch_table[OP_HALT + 1] = {0};
#endif
// Forward declarations
static InterpretResult run(void);
static void runtimeError(ErrorType type, SrcLocation location,
                         const char* format, ...);

// Memory allocation handled in memory.c

// Value operations
void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            printf("nil");
            break;
        case VAL_I32:
            printf("%d", AS_I32(value));
            break;
        case VAL_I64:
            printf("%lld", (long long)AS_I64(value));
            break;
        case VAL_U32:
            printf("%u", AS_U32(value));
            break;
        case VAL_U64:
            printf("%llu", (unsigned long long)AS_U64(value));
            break;
        case VAL_F64:
            printf("%g", AS_F64(value));
            break;
        case VAL_STRING:
            printf("%s", AS_STRING(value)->chars);
            break;
        case VAL_ARRAY: {
            ObjArray* array = AS_ARRAY(value);
            printf("[");
            for (int i = 0; i < array->length; i++) {
                if (i > 0) printf(", ");
                printValue(array->elements[i]);
            }
            printf("]");
            break;
        }
        case VAL_ERROR:
            printf("Error: %s", AS_ERROR(value)->message->chars);
            break;
        case VAL_RANGE_ITERATOR: {
            ObjRangeIterator* it = AS_RANGE_ITERATOR(value);
            printf("range(%lld..%lld)",
                   (long long)it->current,
                   (long long)it->end);
            break;
        }
        default:
            printf("<unknown>");
    }
}

bool valuesEqual(Value a, Value b) {
    if (a.type != b.type) return false;

    switch (a.type) {
        case VAL_BOOL:
            return AS_BOOL(a) == AS_BOOL(b);
        case VAL_NIL:
            return true;
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
        case VAL_STRING:
            return AS_STRING(a) == AS_STRING(b);
        case VAL_ARRAY:
            return AS_ARRAY(a) == AS_ARRAY(b);
        case VAL_ERROR:
            return AS_ERROR(a) == AS_ERROR(b);
        default:
            return false;
    }
}

// Type system moved to src/type/type_representation.c
// Forward declarations for type system functions
extern void init_extended_type_system(void);
extern Type* get_primitive_type_cached(TypeKind kind);

void initTypeSystem(void) {
    init_extended_type_system();
}

Type* getPrimitiveType(TypeKind kind) {
    return get_primitive_type_cached(kind);
}





void initVM(void) {
    initTypeSystem();

    initMemory();

    // Clear registers
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = NIL_VAL;
    }
    
    // Initialize typed registers for performance optimizations
    memset(&vm.typed_regs, 0, sizeof(TypedRegisters));
    for (int i = 0; i < 32; i++) {
        vm.typed_regs.heap_regs[i] = NIL_VAL;
    }
    for (int i = 0; i < 256; i++) {
        vm.typed_regs.reg_types[i] = REG_TYPE_NONE;
    }

    // Initialize globals
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.globals[i] = NIL_VAL;
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
        vm.variableNames[i].name = NULL;
        vm.variableNames[i].length = 0;
    }

    vm.variableCount = 0;
    vm.functionCount = 0;
    vm.frameCount = 0;
    vm.tryFrameCount = 0;
    vm.lastError = NIL_VAL;
    vm.instruction_count = 0;
    vm.astRoot = NULL;
    vm.filePath = NULL;
    vm.currentLine = 0;
    vm.currentColumn = 1;
    vm.moduleCount = 0;
    vm.nativeFunctionCount = 0;
    vm.gcCount = 0;
    vm.lastExecutionTime = 0.0;

    // Environment configuration
    const char* envTrace = getenv("ORUS_TRACE");
    vm.trace = envTrace && envTrace[0] != '\0';

    vm.chunk = NULL;
    vm.ip = NULL;
    
    // Dispatch table will be initialized on first run() call
    // No dummy warm-up needed - eliminates cold start penalty
}

void freeVM(void) {
    // Free all allocated objects
    freeObjects();

    // Clear globals
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.variableNames[i].name = NULL;
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
    }

    vm.astRoot = NULL;
    vm.chunk = NULL;
    vm.ip = NULL;
}



// Memory management implemented in memory.c

// Runtime error handling
static void runtimeError(ErrorType type, SrcLocation location,
                         const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
        location.line = vm.currentLine;
        location.column = vm.currentColumn;
    }

    ObjError* err = allocateError(type, buffer, location);
    vm.lastError = ERROR_VAL(err);
}

// Debug operations
// Main execution engine
static InterpretResult run(void) {
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT(index) (vm.chunk->constants.values[index])

    double start_time = get_time_vm();
#define RETURN(val)                                                                          \
    do {                                                                                     \
        vm.lastExecutionTime = get_time_vm() - start_time;                                    \
        return (val);                                                                         \
    } while (0)

#if USE_COMPUTED_GOTO
    // Initialize dispatch table with label addresses - this only runs ONCE per process
    static bool global_dispatch_initialized = false;
    if (!global_dispatch_initialized) {
        // Phase 1.3 Optimization: Hot opcodes first for better cache locality
        // Most frequently used typed operations (hot path)
        vm_dispatch_table[OP_ADD_I32_TYPED] = &&LABEL_OP_ADD_I32_TYPED;
        vm_dispatch_table[OP_SUB_I32_TYPED] = &&LABEL_OP_SUB_I32_TYPED;
        vm_dispatch_table[OP_MUL_I32_TYPED] = &&LABEL_OP_MUL_I32_TYPED;
        vm_dispatch_table[OP_LT_I32_TYPED] = &&LABEL_OP_LT_I32_TYPED;
        vm_dispatch_table[OP_LE_I32_TYPED] = &&LABEL_OP_LE_I32_TYPED;
        vm_dispatch_table[OP_GT_I32_TYPED] = &&LABEL_OP_GT_I32_TYPED;
        vm_dispatch_table[OP_GE_I32_TYPED] = &&LABEL_OP_GE_I32_TYPED;
        
        // Short jumps for tight loops
        vm_dispatch_table[OP_JUMP_SHORT] = &&LABEL_OP_JUMP_SHORT;
        vm_dispatch_table[OP_JUMP_BACK_SHORT] = &&LABEL_OP_JUMP_BACK_SHORT;
        vm_dispatch_table[OP_JUMP_IF_NOT_SHORT] = &&LABEL_OP_JUMP_IF_NOT_SHORT;
        vm_dispatch_table[OP_LOOP_SHORT] = &&LABEL_OP_LOOP_SHORT;
        
        // Loop-critical operations
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        
        // Remaining typed operations
        vm_dispatch_table[OP_DIV_I32_TYPED] = &&LABEL_OP_DIV_I32_TYPED;
        vm_dispatch_table[OP_MOD_I32_TYPED] = &&LABEL_OP_MOD_I32_TYPED;
        vm_dispatch_table[OP_ADD_I64_TYPED] = &&LABEL_OP_ADD_I64_TYPED;
        vm_dispatch_table[OP_SUB_I64_TYPED] = &&LABEL_OP_SUB_I64_TYPED;
        vm_dispatch_table[OP_MUL_I64_TYPED] = &&LABEL_OP_MUL_I64_TYPED;
        vm_dispatch_table[OP_DIV_I64_TYPED] = &&LABEL_OP_DIV_I64_TYPED;
        vm_dispatch_table[OP_ADD_F64_TYPED] = &&LABEL_OP_ADD_F64_TYPED;
        vm_dispatch_table[OP_SUB_F64_TYPED] = &&LABEL_OP_SUB_F64_TYPED;
        vm_dispatch_table[OP_MUL_F64_TYPED] = &&LABEL_OP_MUL_F64_TYPED;
        vm_dispatch_table[OP_DIV_F64_TYPED] = &&LABEL_OP_DIV_F64_TYPED;
        
        // Constant loading (also hot)
        vm_dispatch_table[OP_LOAD_I32_CONST] = &&LABEL_OP_LOAD_I32_CONST;
        vm_dispatch_table[OP_LOAD_I64_CONST] = &&LABEL_OP_LOAD_I64_CONST;
        vm_dispatch_table[OP_LOAD_F64_CONST] = &&LABEL_OP_LOAD_F64_CONST;
        
        // Standard operations (less hot)
        vm_dispatch_table[OP_LOAD_CONST] = &&LABEL_OP_LOAD_CONST;
        vm_dispatch_table[OP_LOAD_NIL] = &&LABEL_OP_LOAD_NIL;
        vm_dispatch_table[OP_LOAD_TRUE] = &&LABEL_OP_LOAD_TRUE;
        vm_dispatch_table[OP_LOAD_FALSE] = &&LABEL_OP_LOAD_FALSE;
        vm_dispatch_table[OP_MOVE] = &&LABEL_OP_MOVE;
        vm_dispatch_table[OP_LOAD_GLOBAL] = &&LABEL_OP_LOAD_GLOBAL;
        vm_dispatch_table[OP_STORE_GLOBAL] = &&LABEL_OP_STORE_GLOBAL;
        vm_dispatch_table[OP_ADD_I32_R] = &&LABEL_OP_ADD_I32_R;
        vm_dispatch_table[OP_SUB_I32_R] = &&LABEL_OP_SUB_I32_R;
        vm_dispatch_table[OP_MUL_I32_R] = &&LABEL_OP_MUL_I32_R;
        vm_dispatch_table[OP_DIV_I32_R] = &&LABEL_OP_DIV_I32_R;
        vm_dispatch_table[OP_MOD_I32_R] = &&LABEL_OP_MOD_I32_R;
        vm_dispatch_table[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        vm_dispatch_table[OP_DEC_I32_R] = &&LABEL_OP_DEC_I32_R;
        vm_dispatch_table[OP_ADD_I64_R] = &&LABEL_OP_ADD_I64_R;
        vm_dispatch_table[OP_SUB_I64_R] = &&LABEL_OP_SUB_I64_R;
        vm_dispatch_table[OP_MUL_I64_R] = &&LABEL_OP_MUL_I64_R;
        vm_dispatch_table[OP_DIV_I64_R] = &&LABEL_OP_DIV_I64_R;
        vm_dispatch_table[OP_MOD_I64_R] = &&LABEL_OP_MOD_I64_R;
        vm_dispatch_table[OP_ADD_U32_R] = &&LABEL_OP_ADD_U32_R;
        vm_dispatch_table[OP_SUB_U32_R] = &&LABEL_OP_SUB_U32_R;
        vm_dispatch_table[OP_MUL_U32_R] = &&LABEL_OP_MUL_U32_R;
        vm_dispatch_table[OP_DIV_U32_R] = &&LABEL_OP_DIV_U32_R;
        vm_dispatch_table[OP_MOD_U32_R] = &&LABEL_OP_MOD_U32_R;
        vm_dispatch_table[OP_ADD_U64_R] = &&LABEL_OP_ADD_U64_R;
        vm_dispatch_table[OP_SUB_U64_R] = &&LABEL_OP_SUB_U64_R;
        vm_dispatch_table[OP_MUL_U64_R] = &&LABEL_OP_MUL_U64_R;
        vm_dispatch_table[OP_DIV_U64_R] = &&LABEL_OP_DIV_U64_R;
        vm_dispatch_table[OP_MOD_U64_R] = &&LABEL_OP_MOD_U64_R;
        vm_dispatch_table[OP_I32_TO_I64_R] = &&LABEL_OP_I32_TO_I64_R;
        vm_dispatch_table[OP_I32_TO_U32_R] = &&LABEL_OP_I32_TO_U32_R;
        vm_dispatch_table[OP_U32_TO_I32_R] = &&LABEL_OP_U32_TO_I32_R;
        vm_dispatch_table[OP_ADD_F64_R] = &&LABEL_OP_ADD_F64_R;
        vm_dispatch_table[OP_SUB_F64_R] = &&LABEL_OP_SUB_F64_R;
        vm_dispatch_table[OP_MUL_F64_R] = &&LABEL_OP_MUL_F64_R;
        vm_dispatch_table[OP_DIV_F64_R] = &&LABEL_OP_DIV_F64_R;

        // Bitwise operations
        vm_dispatch_table[OP_AND_I32_R] = &&LABEL_OP_AND_I32_R;
        vm_dispatch_table[OP_OR_I32_R] = &&LABEL_OP_OR_I32_R;
        vm_dispatch_table[OP_XOR_I32_R] = &&LABEL_OP_XOR_I32_R;
        vm_dispatch_table[OP_NOT_I32_R] = &&LABEL_OP_NOT_I32_R;
        vm_dispatch_table[OP_SHL_I32_R] = &&LABEL_OP_SHL_I32_R;
        vm_dispatch_table[OP_SHR_I32_R] = &&LABEL_OP_SHR_I32_R;

        vm_dispatch_table[OP_LT_F64_R] = &&LABEL_OP_LT_F64_R;
        vm_dispatch_table[OP_LE_F64_R] = &&LABEL_OP_LE_F64_R;
        vm_dispatch_table[OP_GT_F64_R] = &&LABEL_OP_GT_F64_R;
        vm_dispatch_table[OP_GE_F64_R] = &&LABEL_OP_GE_F64_R;
        vm_dispatch_table[OP_I32_TO_F64_R] = &&LABEL_OP_I32_TO_F64_R;
        vm_dispatch_table[OP_I64_TO_F64_R] = &&LABEL_OP_I64_TO_F64_R;
        vm_dispatch_table[OP_F64_TO_I32_R] = &&LABEL_OP_F64_TO_I32_R;
        vm_dispatch_table[OP_F64_TO_I64_R] = &&LABEL_OP_F64_TO_I64_R;
        vm_dispatch_table[OP_LT_I32_R] = &&LABEL_OP_LT_I32_R;
        vm_dispatch_table[OP_LE_I32_R] = &&LABEL_OP_LE_I32_R;
        vm_dispatch_table[OP_GT_I32_R] = &&LABEL_OP_GT_I32_R;
        vm_dispatch_table[OP_GE_I32_R] = &&LABEL_OP_GE_I32_R;
        vm_dispatch_table[OP_LT_I64_R] = &&LABEL_OP_LT_I64_R;
        vm_dispatch_table[OP_LE_I64_R] = &&LABEL_OP_LE_I64_R;
        vm_dispatch_table[OP_GT_I64_R] = &&LABEL_OP_GT_I64_R;
        vm_dispatch_table[OP_GE_I64_R] = &&LABEL_OP_GE_I64_R;
        vm_dispatch_table[OP_LT_U32_R] = &&LABEL_OP_LT_U32_R;
        vm_dispatch_table[OP_LE_U32_R] = &&LABEL_OP_LE_U32_R;
        vm_dispatch_table[OP_GT_U32_R] = &&LABEL_OP_GT_U32_R;
        vm_dispatch_table[OP_GE_U32_R] = &&LABEL_OP_GE_U32_R;
        vm_dispatch_table[OP_LT_U64_R] = &&LABEL_OP_LT_U64_R;
        vm_dispatch_table[OP_LE_U64_R] = &&LABEL_OP_LE_U64_R;
        vm_dispatch_table[OP_GT_U64_R] = &&LABEL_OP_GT_U64_R;
        vm_dispatch_table[OP_GE_U64_R] = &&LABEL_OP_GE_U64_R;
        vm_dispatch_table[OP_EQ_R] = &&LABEL_OP_EQ_R;
        vm_dispatch_table[OP_NE_R] = &&LABEL_OP_NE_R;
        vm_dispatch_table[OP_AND_BOOL_R] = &&LABEL_OP_AND_BOOL_R;
        vm_dispatch_table[OP_OR_BOOL_R] = &&LABEL_OP_OR_BOOL_R;
        vm_dispatch_table[OP_NOT_BOOL_R] = &&LABEL_OP_NOT_BOOL_R;
        vm_dispatch_table[OP_CONCAT_R] = &&LABEL_OP_CONCAT_R;
        vm_dispatch_table[OP_JUMP] = &&LABEL_OP_JUMP;
        vm_dispatch_table[OP_JUMP_IF_NOT_R] = &&LABEL_OP_JUMP_IF_NOT_R;
        vm_dispatch_table[OP_LOOP] = &&LABEL_OP_LOOP;
        vm_dispatch_table[OP_GET_ITER_R] = &&LABEL_OP_GET_ITER_R;
        vm_dispatch_table[OP_ITER_NEXT_R] = &&LABEL_OP_ITER_NEXT_R;
        vm_dispatch_table[OP_PRINT_MULTI_R] = &&LABEL_OP_PRINT_MULTI_R;
        vm_dispatch_table[OP_PRINT_R] = &&LABEL_OP_PRINT_R;
        vm_dispatch_table[OP_PRINT_NO_NL_R] = &&LABEL_OP_PRINT_NO_NL_R;
        vm_dispatch_table[OP_RETURN_R] = &&LABEL_OP_RETURN_R;
        vm_dispatch_table[OP_RETURN_VOID] = &&LABEL_OP_RETURN_VOID;
        
        // Note: Hot opcodes already assigned above for optimal cache locality
        
        vm_dispatch_table[OP_MOVE_I32] = &&LABEL_OP_MOVE_I32;
        vm_dispatch_table[OP_MOVE_I64] = &&LABEL_OP_MOVE_I64;
        vm_dispatch_table[OP_MOVE_F64] = &&LABEL_OP_MOVE_F64;
        
        // Phase 2.2: Fused instruction dispatch entries
        vm_dispatch_table[OP_ADD_I32_IMM] = &&LABEL_OP_ADD_I32_IMM;
        vm_dispatch_table[OP_SUB_I32_IMM] = &&LABEL_OP_SUB_I32_IMM;
        vm_dispatch_table[OP_MUL_I32_IMM] = &&LABEL_OP_MUL_I32_IMM;
        vm_dispatch_table[OP_CMP_I32_IMM] = &&LABEL_OP_CMP_I32_IMM;
        vm_dispatch_table[OP_INC_CMP_JMP] = &&LABEL_OP_INC_CMP_JMP;
        vm_dispatch_table[OP_DEC_CMP_JMP] = &&LABEL_OP_DEC_CMP_JMP;
        vm_dispatch_table[OP_MUL_ADD_I32] = &&LABEL_OP_MUL_ADD_I32;
        
        // Built-in functions
        vm_dispatch_table[OP_TIME_STAMP] = &&LABEL_OP_TIME_STAMP;
        
        vm_dispatch_table[OP_HALT] = &&LABEL_OP_HALT;
        
        // Mark dispatch table as initialized to prevent re-initialization
        global_dispatch_initialized = true;
    }

    uint8_t instruction;

// Phase 1.1 Optimization: Fast DISPATCH macro for production builds
#ifdef ORUS_DEBUG
    // Debug build: Keep full error checking and tracing
    #define DISPATCH() \
        do { \
            if (IS_ERROR(vm.lastError)) { \
                if (vm.tryFrameCount > 0) { \
                    TryFrame frame = vm.tryFrames[--vm.tryFrameCount]; \
                    vm.ip = frame.handler; \
                    vm.globals[frame.varIndex] = vm.lastError; \
                    vm.lastError = NIL_VAL; \
                } else { \
                    RETURN(INTERPRET_RUNTIME_ERROR); \
                } \
            } \
            if (vm.trace) { \
                printf("        "); \
                for (int i = 0; i < 8; i++) { \
                    printf("[ R%d: ", i); \
                    printValue(vm.registers[i]); \
                    printf(" ]"); \
                } \
                printf("\\n"); \
                disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code)); \
            } \
            vm.instruction_count++; \
            instruction = READ_BYTE(); \
            if (instruction > OP_HALT || vm_dispatch_table[instruction] == NULL) { \
                goto LABEL_UNKNOWN; \
            } \
            goto *vm_dispatch_table[instruction]; \
        } while (0)
    
    // Same as normal dispatch for debug builds
    #define DISPATCH_TYPED() DISPATCH()
#else
    // Production build: Ultra-fast dispatch with no checks
    #define DISPATCH() goto *vm_dispatch_table[*vm.ip++]
    
    // Even faster for typed operations - no error checking needed
    #define DISPATCH_TYPED() goto *vm_dispatch_table[*vm.ip++]
#endif
    DISPATCH();

LABEL_OP_LOAD_CONST: {
        uint8_t reg = READ_BYTE();
        uint16_t constantIndex = READ_SHORT();
        vm.registers[reg] = READ_CONSTANT(constantIndex);
        DISPATCH();
    }

LABEL_OP_LOAD_NIL: {
        uint8_t reg = READ_BYTE();
        vm.registers[reg] = NIL_VAL;
        DISPATCH();
    }

LABEL_OP_LOAD_TRUE: {
        uint8_t reg = READ_BYTE();
        vm.registers[reg] = BOOL_VAL(true);
        DISPATCH();
    }

LABEL_OP_LOAD_FALSE: {
        uint8_t reg = READ_BYTE();
        vm.registers[reg] = BOOL_VAL(false);
        DISPATCH();
    }

LABEL_OP_MOVE: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        vm.registers[dst] = vm.registers[src];
        DISPATCH();
    }

LABEL_OP_LOAD_GLOBAL: {
        uint8_t reg = READ_BYTE();
        uint8_t globalIndex = READ_BYTE();
        if (globalIndex >= vm.variableCount || vm.globalTypes[globalIndex] == NULL) {
            runtimeError(ERROR_NAME, (SrcLocation){NULL, 0, 0}, "Undefined variable");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[reg] = vm.globals[globalIndex];
        DISPATCH();
    }

LABEL_OP_STORE_GLOBAL: {
        uint8_t globalIndex = READ_BYTE();
        uint8_t reg = READ_BYTE();
        vm.globals[globalIndex] = vm.registers[reg];
        DISPATCH();
    }

LABEL_OP_ADD_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int32_t a = AS_I32(vm.registers[src1]);
        int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I32_VAL(a + b);
#else
        int32_t result;
        if (__builtin_add_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_SUB_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int32_t a = AS_I32(vm.registers[src1]);
        int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I32_VAL(a - b);
#else
        int32_t result;
        if (__builtin_sub_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_MUL_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int32_t a = AS_I32(vm.registers[src1]);
        int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I32_VAL(a * b);
#else
        int32_t result;
        if (__builtin_mul_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_DIV_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int32_t b = AS_I32(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) / b);
        DISPATCH();
    }

LABEL_OP_MOD_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int32_t b = AS_I32(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) % b);
        DISPATCH();
    }

LABEL_OP_INC_I32_R: {
        uint8_t reg = READ_BYTE();
#if USE_FAST_ARITH
        vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) + 1);
#else
        int32_t val = AS_I32(vm.registers[reg]);
        int32_t result;
        if (__builtin_add_overflow(val, 1, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[reg] = I32_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_DEC_I32_R: {
        uint8_t reg = READ_BYTE();
#if USE_FAST_ARITH
        vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) - 1);
#else
        int32_t val = AS_I32(vm.registers[reg]);
        int32_t result;
        if (__builtin_sub_overflow(val, 1, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[reg] = I32_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_ADD_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int64_t a = AS_I64(vm.registers[src1]);
        int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I64_VAL(a + b);
#else
        int64_t result;
        if (__builtin_add_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_SUB_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int64_t a = AS_I64(vm.registers[src1]);
        int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I64_VAL(a - b);
#else
        int64_t result;
        if (__builtin_sub_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_MUL_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int64_t a = AS_I64(vm.registers[src1]);
        int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
        vm.registers[dst] = I64_VAL(a * b);
#else
        int64_t result;
        if (__builtin_mul_overflow(a, b, &result)) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Integer overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL(result);
#endif
        DISPATCH();
    }

LABEL_OP_DIV_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int64_t b = AS_I64(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
        DISPATCH();
    }

LABEL_OP_MOD_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        int64_t b = AS_I64(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
        DISPATCH();
    }

LABEL_OP_ADD_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) + AS_U32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_SUB_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) - AS_U32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_MUL_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) * AS_U32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_DIV_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        uint32_t b = AS_U32(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) / b);
        DISPATCH();
    }

LABEL_OP_MOD_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        uint32_t b = AS_U32(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) % b);
        DISPATCH();
    }

LABEL_OP_ADD_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                         "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        uint64_t a = AS_U64(vm.registers[src1]);
        uint64_t b = AS_U64(vm.registers[src2]);
        
        // Check for overflow: if a + b < a, then overflow occurred
        if (UINT64_MAX - a < b) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                         "u64 addition overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        vm.registers[dst] = U64_VAL(a + b);
        DISPATCH();
    }

LABEL_OP_SUB_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                         "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        uint64_t a = AS_U64(vm.registers[src1]);
        uint64_t b = AS_U64(vm.registers[src2]);
        
        // Check for underflow: if a < b, then underflow would occur
        if (a < b) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                         "u64 subtraction underflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        vm.registers[dst] = U64_VAL(a - b);
        DISPATCH();
    }

LABEL_OP_MUL_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                         "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        uint64_t a = AS_U64(vm.registers[src1]);
        uint64_t b = AS_U64(vm.registers[src2]);
        
        // Check for multiplication overflow: if a != 0 && result / a != b
        if (a != 0 && b > UINT64_MAX / a) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                         "u64 multiplication overflow");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        vm.registers[dst] = U64_VAL(a * b);
        DISPATCH();
    }

LABEL_OP_DIV_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                         "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        uint64_t b = AS_U64(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                         "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) / b);
        DISPATCH();
    }

LABEL_OP_MOD_U64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();

        if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                         "Operands must be u64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        uint64_t b = AS_U64(vm.registers[src2]);
        if (b == 0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                         "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }

        vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) % b);
        DISPATCH();
    }

LABEL_OP_I32_TO_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_I32_TO_U32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_U32_TO_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_U32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be u32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
        DISPATCH();
    }

// F64 Arithmetic Operations
LABEL_OP_ADD_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_SUB_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_MUL_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_DIV_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        double a = AS_F64(vm.registers[src1]);
        double b = AS_F64(vm.registers[src2]);
        
        // IEEE 754 compliant: division by zero produces infinity, not error
        double result = a / b;
        
        // The result may be infinity, -infinity, or NaN
        // These are valid f64 values according to IEEE 754
        vm.registers[dst] = F64_VAL(result);
        DISPATCH();
    }

// Bitwise Operations
LABEL_OP_AND_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_OR_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_XOR_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_NOT_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_SHL_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_SHR_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
        DISPATCH();
    }

// F64 Comparison Operations
LABEL_OP_LT_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) < AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_LE_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) <= AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_GT_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) > AS_F64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_GE_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) >= AS_F64(vm.registers[src2]));
        DISPATCH();
    }

// F64 Type Conversion Operations
LABEL_OP_I32_TO_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_I32(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_I64_TO_F64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_I64(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_F64_TO_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_F64(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_F64_TO_I64_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        if (!IS_F64(vm.registers[src])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
        DISPATCH();
    }

LABEL_OP_LT_I32_R: {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) < AS_I32(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_EQ_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    vm.registers[dst] = BOOL_VAL(valuesEqual(vm.registers[src1], vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_NE_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    vm.registers[dst] = BOOL_VAL(!valuesEqual(vm.registers[src1], vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LE_I32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <= AS_I32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GT_I32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) > AS_I32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GE_I32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >= AS_I32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LT_I64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) < AS_I64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LE_I64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <= AS_I64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GT_I64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) > AS_I64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GE_I64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
        vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >= AS_I64(vm.registers[src2]));
        DISPATCH();
    }

LABEL_OP_LT_U32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) < AS_U32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LE_U32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) <= AS_U32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GT_U32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) > AS_U32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GE_U32_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) >= AS_U32(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LT_U64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) < AS_U64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_LE_U64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) <= AS_U64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GT_U64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) > AS_U64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_GE_U64_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be u64");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) >= AS_U64(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_AND_BOOL_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be bool");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) && AS_BOOL(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_OR_BOOL_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be bool");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) || AS_BOOL(vm.registers[src2]));
    DISPATCH();
}

LABEL_OP_NOT_BOOL_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    if (!IS_BOOL(vm.registers[src])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be bool");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = BOOL_VAL(!AS_BOOL(vm.registers[src]));
    DISPATCH();
}

LABEL_OP_CONCAT_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    if (!IS_STRING(vm.registers[src1]) || !IS_STRING(vm.registers[src2])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be string");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    ObjString* a = AS_STRING(vm.registers[src1]);
    ObjString* b = AS_STRING(vm.registers[src2]);
    int newLen = a->length + b->length;
    char* buf = malloc(newLen + 1);
    memcpy(buf, a->chars, a->length);
    memcpy(buf + a->length, b->chars, b->length);
    buf[newLen] = '\0';
    ObjString* res = allocateString(buf, newLen);
    free(buf);
    vm.registers[dst] = STRING_VAL(res);
    DISPATCH();
}

LABEL_OP_JUMP: {
        uint16_t offset = READ_SHORT();
        vm.ip += offset;
        DISPATCH();
    }

LABEL_OP_JUMP_IF_NOT_R: {
        uint8_t reg = READ_BYTE();
        uint16_t offset = READ_SHORT();
        if (!IS_BOOL(vm.registers[reg])) {
            runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        if (!AS_BOOL(vm.registers[reg])) {
            vm.ip += offset;
        }
        DISPATCH();
    }

LABEL_OP_LOOP: {
    uint16_t offset = READ_SHORT();
    vm.ip -= offset;
    DISPATCH();
}

LABEL_OP_GET_ITER_R: {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    Value v = vm.registers[src];
    if (!IS_RANGE_ITERATOR(v)) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Value not iterable");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    vm.registers[dst] = v;
    DISPATCH();
}

LABEL_OP_ITER_NEXT_R: {
    uint8_t dst = READ_BYTE();
    uint8_t iterReg = READ_BYTE();
    uint8_t hasReg = READ_BYTE();
    if (!IS_RANGE_ITERATOR(vm.registers[iterReg])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, "Invalid iterator");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    ObjRangeIterator* it = AS_RANGE_ITERATOR(vm.registers[iterReg]);
    if (it->current >= it->end) {
        vm.registers[hasReg] = BOOL_VAL(false);
    } else {
        vm.registers[dst] = I64_VAL(it->current);
        it->current++;
        vm.registers[hasReg] = BOOL_VAL(true);
    }
    DISPATCH();
}

LABEL_OP_PRINT_MULTI_R: {
        uint8_t first = READ_BYTE();
        uint8_t count = READ_BYTE();
        uint8_t nl = READ_BYTE();
        builtin_print(&vm.registers[first], count, nl != 0);
        DISPATCH();
    }

LABEL_OP_PRINT_R: {
        uint8_t reg = READ_BYTE();
        
        // Check if this is a typed register first (reg is uint8_t, so always < 256)
        if (vm.typed_regs.reg_types[reg] != REG_TYPE_NONE) {
            switch (vm.typed_regs.reg_types[reg]) {
                case REG_TYPE_I32:
                    printf("%d", vm.typed_regs.i32_regs[reg]);
                    break;
                case REG_TYPE_I64:
                    printf("%lld", (long long)vm.typed_regs.i64_regs[reg]);
                    break;
                case REG_TYPE_F64:
                    printf("%g", vm.typed_regs.f64_regs[reg]);
                    break;
                default:
                    printValue(vm.registers[reg]);
                    break;
            }
        } else {
            printValue(vm.registers[reg]);
        }
        
        printf("\n");
        fflush(stdout);
        DISPATCH();
    }

LABEL_OP_PRINT_NO_NL_R: {
        uint8_t reg = READ_BYTE();
        printValue(vm.registers[reg]);
        fflush(stdout);
        DISPATCH();
    }

LABEL_OP_RETURN_R: {
        uint8_t reg = READ_BYTE();
        Value returnValue = vm.registers[reg];
        if (vm.frameCount > 0) {
            CallFrame* frame = &vm.frames[--vm.frameCount];
            vm.chunk = frame->previousChunk;
            vm.ip = frame->returnAddress;
            vm.registers[frame->baseRegister] = returnValue;
        } else {
            vm.lastExecutionTime = get_time_vm() - start_time;
            RETURN(INTERPRET_OK);
        }
        DISPATCH();
    }

LABEL_OP_RETURN_VOID: {
        if (vm.frameCount > 0) {
            CallFrame* frame = &vm.frames[--vm.frameCount];
            vm.chunk = frame->previousChunk;
            vm.ip = frame->returnAddress;
        } else {
            vm.lastExecutionTime = get_time_vm() - start_time;
            RETURN(INTERPRET_OK);
        }
        DISPATCH();
    }

// Short jump optimizations for performance
LABEL_OP_JUMP_SHORT: {
    uint8_t offset = READ_BYTE();
    vm.ip += offset;
    DISPATCH();
}

LABEL_OP_JUMP_BACK_SHORT: {
    uint8_t offset = READ_BYTE();
    vm.ip -= offset;
    DISPATCH();
}

LABEL_OP_JUMP_IF_NOT_SHORT: {
    uint8_t reg = READ_BYTE();
    uint8_t offset = READ_BYTE();
    
    if (!IS_BOOL(vm.registers[reg])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    if (!AS_BOOL(vm.registers[reg])) {
        vm.ip += offset;
    }
    DISPATCH();
}

LABEL_OP_LOOP_SHORT: {
    uint8_t offset = READ_BYTE();
    vm.ip -= offset;
    DISPATCH();
}

// Typed arithmetic operations for maximum performance (bypass Value boxing)
LABEL_OP_ADD_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_SUB_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] - vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] * vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_DIV_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    // Keep zero check for safety, but remove type checks
    if (vm.typed_regs.i32_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] / vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_MOD_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    if (vm.typed_regs.i32_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Modulo by zero");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] % vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_ADD_I64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] + vm.typed_regs.i64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_SUB_I64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] - vm.typed_regs.i64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_I64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] * vm.typed_regs.i64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_DIV_I64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    if (vm.typed_regs.i64_regs[right] == 0) {
        runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] / vm.typed_regs.i64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_ADD_F64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] + vm.typed_regs.f64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_SUB_F64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] - vm.typed_regs.f64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_F64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] * vm.typed_regs.f64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_DIV_F64_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] / vm.typed_regs.f64_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_LT_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] < vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_LE_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] <= vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_GT_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] > vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_GE_I32_TYPED: {
    uint8_t dst = *vm.ip++;
    uint8_t left = *vm.ip++;
    uint8_t right = *vm.ip++;
    
    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] >= vm.typed_regs.i32_regs[right];
    
    DISPATCH_TYPED();
}

LABEL_OP_LOAD_I32_CONST: {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    int32_t value = READ_CONSTANT(constantIndex).as.i32;
    
    vm.typed_regs.i32_regs[reg] = value;
    vm.typed_regs.reg_types[reg] = REG_TYPE_I32;
    
    DISPATCH();
}

LABEL_OP_LOAD_I64_CONST: {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    int64_t value = READ_CONSTANT(constantIndex).as.i64;
    
    vm.typed_regs.i64_regs[reg] = value;
    vm.typed_regs.reg_types[reg] = REG_TYPE_I64;
    
    DISPATCH();
}

LABEL_OP_LOAD_F64_CONST: {
    uint8_t reg = READ_BYTE();
    uint16_t constantIndex = READ_SHORT();
    double value = READ_CONSTANT(constantIndex).as.f64;
    
    vm.typed_regs.f64_regs[reg] = value;
    vm.typed_regs.reg_types[reg] = REG_TYPE_F64;
    
    DISPATCH();
}

LABEL_OP_MOVE_I32: {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    
    vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
    
    DISPATCH();
}

LABEL_OP_MOVE_I64: {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    
    vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
    
    DISPATCH();
}

LABEL_OP_MOVE_F64: {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    
    vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[src];
    vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
    
    DISPATCH();
}

LABEL_OP_TIME_STAMP: {
    uint8_t dst = READ_BYTE();
    
    // Get high-precision timestamp in nanoseconds
    int64_t timestamp = builtin_time_stamp();
    
    // Store in both typed register and regular register for compatibility
    vm.typed_regs.i64_regs[dst] = timestamp;
    vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
    vm.registers[dst] = I64_VAL(timestamp);
    
    DISPATCH();
}

// Phase 2.2: Fused instruction implementations for better performance
LABEL_OP_ADD_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    // Compiler ensures this is only emitted for i32 operations, so trust it
    if (!IS_I32(vm.registers[src])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    int32_t result = AS_I32(vm.registers[src]) + imm;
    vm.registers[dst] = I32_VAL(result);
    
    DISPATCH_TYPED();
}

LABEL_OP_SUB_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    // Compiler ensures this is only emitted for i32 operations, so trust it
    if (!IS_I32(vm.registers[src])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    int32_t result = AS_I32(vm.registers[src]) - imm;
    vm.registers[dst] = I32_VAL(result);
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    // Compiler ensures this is only emitted for i32 operations, so trust it
    if (!IS_I32(vm.registers[src])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    int32_t result = AS_I32(vm.registers[src]) * imm;
    vm.registers[dst] = I32_VAL(result);
    
    DISPATCH_TYPED();
}

LABEL_OP_CMP_I32_IMM: {
    uint8_t dst = *vm.ip++;
    uint8_t src = *vm.ip++;
    int32_t imm = *(int32_t*)vm.ip;
    vm.ip += 4;
    
    vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[src] < imm;
    
    DISPATCH_TYPED();
}

LABEL_OP_INC_CMP_JMP: {
    uint8_t reg = *vm.ip++;
    uint8_t limit_reg = *vm.ip++;
    int16_t offset = *(int16_t*)vm.ip;
    vm.ip += 2;
    
    // Compiler ensures this is only emitted for i32 operations, so trust it
    if (!IS_I32(vm.registers[reg]) || !IS_I32(vm.registers[limit_reg])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
        RETURN(INTERPRET_RUNTIME_ERROR);
    }
    
    int32_t incremented = AS_I32(vm.registers[reg]) + 1;
    vm.registers[reg] = I32_VAL(incremented);
    if (incremented < AS_I32(vm.registers[limit_reg])) {
        vm.ip += offset;
    }
    
    DISPATCH_TYPED();
}

LABEL_OP_DEC_CMP_JMP: {
    uint8_t reg = *vm.ip++;
    uint8_t zero_test = *vm.ip++;
    int16_t offset = *(int16_t*)vm.ip;
    vm.ip += 2;
    
    // Fused decrement + compare + conditional jump
    if (--vm.typed_regs.i32_regs[reg] > vm.typed_regs.i32_regs[zero_test]) {
        vm.ip += offset;
    }
    
    DISPATCH_TYPED();
}

LABEL_OP_MUL_ADD_I32: {
    uint8_t dst = *vm.ip++;
    uint8_t mul1 = *vm.ip++;
    uint8_t mul2 = *vm.ip++;
    uint8_t add = *vm.ip++;
    
    // Fused multiply-add (single operation on modern CPUs)
    vm.typed_regs.i32_regs[dst] = 
        vm.typed_regs.i32_regs[mul1] * vm.typed_regs.i32_regs[mul2] + 
        vm.typed_regs.i32_regs[add];
    
    DISPATCH_TYPED();
}

LABEL_OP_HALT:
    vm.lastExecutionTime = get_time_vm() - start_time;
    RETURN(INTERPRET_OK);

LABEL_UNKNOWN: __attribute__((unused))
    runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                 "Unknown opcode: %d", instruction);
    RETURN(INTERPRET_RUNTIME_ERROR);

#else  // USE_COMPUTED_GOTO

    for (;;) {
        if (vm.trace) {
            // Debug trace
            printf("        ");
            for (int i = 0; i < 8; i++) {
                printf("[ R%d: ", i);
                printValue(vm.registers[i]);
                printf(" ]");
            }
            printf("\n");

            disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
        }

        vm.instruction_count++;

        uint8_t instruction = READ_BYTE();

        switch (instruction) {
            case OP_LOAD_CONST: {
                uint8_t reg = READ_BYTE();
                uint16_t constantIndex = READ_SHORT();
                vm.registers[reg] = READ_CONSTANT(constantIndex);
                break;
            }

            case OP_LOAD_NIL: {
                uint8_t reg = READ_BYTE();
                vm.registers[reg] = NIL_VAL;
                break;
            }

            case OP_LOAD_TRUE: {
                uint8_t reg = READ_BYTE();
                vm.registers[reg] = BOOL_VAL(true);
                break;
            }

            case OP_LOAD_FALSE: {
                uint8_t reg = READ_BYTE();
                vm.registers[reg] = BOOL_VAL(false);
                break;
            }

            case OP_MOVE: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                vm.registers[dst] = vm.registers[src];
                break;
            }

            case OP_LOAD_GLOBAL: {
                uint8_t reg = READ_BYTE();
                uint8_t globalIndex = READ_BYTE();
                if (globalIndex >= vm.variableCount ||
                    vm.globalTypes[globalIndex] == NULL) {
                    runtimeError(ERROR_NAME, (SrcLocation){NULL, 0, 0},
                                 "Undefined variable");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[reg] = vm.globals[globalIndex];
                break;
            }

            case OP_STORE_GLOBAL: {
                uint8_t globalIndex = READ_BYTE();
                uint8_t reg = READ_BYTE();
                vm.globals[globalIndex] = vm.registers[reg];
                break;
            }

            // Arithmetic operations
            case OP_ADD_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I32_VAL(a + b);
#else
                int32_t result;
                if (__builtin_add_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I32_VAL(result);
#endif
                break;
            }

            case OP_SUB_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I32_VAL(a - b);
#else
                int32_t result;
                if (__builtin_sub_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I32_VAL(result);
#endif
                break;
            }

            case OP_MUL_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I32_VAL(a * b);
#else
                int32_t result;
                if (__builtin_mul_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I32_VAL(result);
#endif
                break;
            }

            case OP_DIV_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) / b);
                break;
            }

            case OP_MOD_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) % b);
                break;
            }

            case OP_INC_I32_R: {
                uint8_t reg = READ_BYTE();
#if USE_FAST_ARITH
                vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) + 1);
#else
                int32_t val = AS_I32(vm.registers[reg]);
                int32_t result;
                if (__builtin_add_overflow(val, 1, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[reg] = I32_VAL(result);
#endif
                break;
            }

            case OP_DEC_I32_R: {
                uint8_t reg = READ_BYTE();
#if USE_FAST_ARITH
                vm.registers[reg] = I32_VAL(AS_I32(vm.registers[reg]) - 1);
#else
                int32_t val = AS_I32(vm.registers[reg]);
                int32_t result;
                if (__builtin_sub_overflow(val, 1, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[reg] = I32_VAL(result);
#endif
                break;
            }

            // I64 arithmetic operations
            case OP_ADD_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I64_VAL(a + b);
#else
                int64_t result;
                if (__builtin_add_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I64_VAL(result);
#endif
                break;
            }

            case OP_SUB_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I64_VAL(a - b);
#else
                int64_t result;
                if (__builtin_sub_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I64_VAL(result);
#endif
                break;
            }

            case OP_MUL_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int64_t a = AS_I64(vm.registers[src1]);
                int64_t b = AS_I64(vm.registers[src2]);
#if USE_FAST_ARITH
                vm.registers[dst] = I64_VAL(a * b);
#else
                int64_t result;
                if (__builtin_mul_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I64_VAL(result);
#endif
                break;
            }

            case OP_DIV_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int64_t b = AS_I64(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) / b);
                break;
            }

            case OP_MOD_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                int64_t b = AS_I64(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = I64_VAL(AS_I64(vm.registers[src1]) % b);
                break;
            }

            case OP_ADD_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                
                // Check for overflow: if a + b < a, then overflow occurred
                if (UINT32_MAX - a < b) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u32 addition overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U32_VAL(a + b);
                break;
            }

            case OP_SUB_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                
                // Check for underflow: if a < b, then underflow would occur
                if (a < b) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u32 subtraction underflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U32_VAL(a - b);
                break;
            }

            case OP_MUL_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint32_t a = AS_U32(vm.registers[src1]);
                uint32_t b = AS_U32(vm.registers[src2]);
                
                // Check for multiplication overflow: if a != 0 && result / a != b
                if (a != 0 && b > UINT32_MAX / a) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u32 multiplication overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U32_VAL(a * b);
                break;
            }

            case OP_DIV_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint32_t b = AS_U32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) / b);
                break;
            }

            case OP_MOD_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint32_t b = AS_U32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U32_VAL(AS_U32(vm.registers[src1]) % b);
                break;
            }

            case OP_ADD_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();

                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                
                // Check for overflow: if a + b < a, then overflow occurred
                if (UINT64_MAX - a < b) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 addition overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U64_VAL(a + b);
                break;
            }

            case OP_SUB_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();

                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                
                // Check for underflow: if a < b, then underflow would occur
                if (a < b) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 subtraction underflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U64_VAL(a - b);
                break;
            }

            case OP_MUL_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();

                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint64_t a = AS_U64(vm.registers[src1]);
                uint64_t b = AS_U64(vm.registers[src2]);
                
                // Check for multiplication overflow: if a != 0 && result / a != b
                if (a != 0 && b > UINT64_MAX / a) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 multiplication overflow");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U64_VAL(a * b);
                break;
            }

            case OP_DIV_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();

                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint64_t b = AS_U64(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) / b);
                break;
            }

            case OP_MOD_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();

                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                uint64_t b = AS_U64(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = U64_VAL(AS_U64(vm.registers[src1]) % b);
                break;
            }

            case OP_I32_TO_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I64_VAL((int64_t)AS_I32(vm.registers[src]));
                break;
            }

            case OP_I32_TO_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U32_VAL((uint32_t)AS_I32(vm.registers[src]));
                break;
            }

            case OP_U32_TO_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_U32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I32_VAL((int32_t)AS_U32(vm.registers[src]));
                break;
            }

            case OP_F64_TO_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = read_BYTE();
                if (!IS_F64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be f64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                double val = AS_F64(vm.registers[src]);
                if (val < 0.0 || val > (double)UINT32_MAX) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "f64 value out of u32 range");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U32_VAL((uint32_t)val);
                break;
            }

            case OP_U32_TO_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = read_BYTE();
                if (!IS_U32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = F64_VAL((double)AS_U32(vm.registers[src]));
                break;
            }

            case OP_I32_TO_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                int32_t val = AS_I32(vm.registers[src]);
                if (val < 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Cannot convert negative i32 to u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U64_VAL((uint64_t)val);
                break;
            }

            case OP_I64_TO_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                int64_t val = AS_I64(vm.registers[src]);
                if (val < 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Cannot convert negative i64 to u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U64_VAL((uint64_t)val);
                break;
            }

            case OP_U64_TO_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_U64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                uint64_t val = AS_U64(vm.registers[src]);
                if (val > (uint64_t)INT32_MAX) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 value too large for i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I32_VAL((int32_t)val);
                break;
            }

            case OP_U64_TO_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_U64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                uint64_t val = AS_U64(vm.registers[src]);
                if (val > (uint64_t)INT64_MAX) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 value too large for i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = I64_VAL((int64_t)val);
                break;
            }

            case OP_U32_TO_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_U32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U64_VAL((uint64_t)AS_U32(vm.registers[src]));
                break;
            }

            case OP_U64_TO_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_U64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                uint64_t val = AS_U64(vm.registers[src]);
                if (val > (uint64_t)UINT32_MAX) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "u64 value too large for u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U32_VAL((uint32_t)val);
                break;
            }

            case OP_F64_TO_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_F64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be f64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                double val = AS_F64(vm.registers[src]);
                if (val < 0.0 || val > (double)UINT64_MAX) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "f64 value out of u64 range");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = U64_VAL((uint64_t)val);
                break;
            }

            case OP_U64_TO_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = read_BYTE();
                if (!IS_U64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Source must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = F64_VAL((double)AS_U64(vm.registers[src]));
                break;
            }

            // F64 Arithmetic Operations
            case OP_ADD_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) + AS_F64(vm.registers[src2]));
                break;
            }

            case OP_SUB_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) - AS_F64(vm.registers[src2]));
                break;
            }

            case OP_MUL_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) * AS_F64(vm.registers[src2]));
                break;
            }

            case OP_DIV_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                double a = AS_F64(vm.registers[src1]);
                double b = AS_F64(vm.registers[src2]);
                
                // IEEE 754 compliant: division by zero produces infinity, not error
                double result = a / b;
                
                // The result may be infinity, -infinity, or NaN
                // These are valid f64 values according to IEEE 754
                vm.registers[dst] = F64_VAL(result);
                break;
            }

            // Bitwise Operations
            case OP_AND_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) & AS_I32(vm.registers[src2]));
                break;
            }

            case OP_OR_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) | AS_I32(vm.registers[src2]));
                break;
            }

            case OP_XOR_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) ^ AS_I32(vm.registers[src2]));
                break;
            }

            case OP_NOT_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operand must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(~AS_I32(vm.registers[src]));
                break;
            }

            case OP_SHL_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) << AS_I32(vm.registers[src2]));
                break;
            }

            case OP_SHR_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) >> AS_I32(vm.registers[src2]));
                break;
            }

            // F64 Comparison Operations
            case OP_LT_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) < AS_F64(vm.registers[src2]));
                break;
            }

            case OP_LE_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) <= AS_F64(vm.registers[src2]));
                break;
            }

            case OP_GT_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) > AS_F64(vm.registers[src2]));
                break;
            }

            case OP_GE_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_F64(vm.registers[src1]) || !IS_F64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Operands must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = BOOL_VAL(AS_F64(vm.registers[src1]) >= AS_F64(vm.registers[src2]));
                break;
            }

            // F64 Type Conversion Operations
            case OP_I32_TO_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I32(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i32");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL((double)AS_I32(vm.registers[src]));
                break;
            }

            case OP_I64_TO_F64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_I64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be i64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL((double)AS_I64(vm.registers[src]));
                break;
            }

            case OP_F64_TO_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_F64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I32_VAL((int32_t)AS_F64(vm.registers[src]));
                break;
            }

            case OP_F64_TO_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                if (!IS_F64(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Source must be f64");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = I64_VAL((int64_t)AS_F64(vm.registers[src]));
                break;
            }

            // Comparison operations
            case OP_LT_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) ||
                    !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <
                                             AS_I32(vm.registers[src2]));
                break;
            }

            case OP_LE_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <=
                                             AS_I32(vm.registers[src2]));
                break;
            }

            case OP_GT_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >
                                             AS_I32(vm.registers[src2]));
                break;
            }

            case OP_GE_I32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I32(vm.registers[src1]) || !IS_I32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) >=
                                             AS_I32(vm.registers[src2]));
                break;
            }

            // I64 comparison operations
            case OP_LT_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) ||
                    !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <
                                             AS_I64(vm.registers[src2]));
                break;
            }

            case OP_LE_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) <=
                                             AS_I64(vm.registers[src2]));
                break;
            }

            case OP_GT_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >
                                             AS_I64(vm.registers[src2]));
                break;
            }

            case OP_GE_I64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_I64(vm.registers[src1]) || !IS_I64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be i64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }


                vm.registers[dst] = BOOL_VAL(AS_I64(vm.registers[src1]) >=
                                             AS_I64(vm.registers[src2]));
                break;
            }

            case OP_LT_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) < AS_U32(vm.registers[src2]));
                break;
            }

            case OP_LE_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) <= AS_U32(vm.registers[src2]));
                break;
            }

            case OP_GT_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) > AS_U32(vm.registers[src2]));
                break;
            }

            case OP_GE_U32_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();
                if (!IS_U32(vm.registers[src1]) || !IS_U32(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u32");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U32(vm.registers[src1]) >= AS_U32(vm.registers[src2]));
                break;
            }

            case OP_LT_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();
                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) < AS_U64(vm.registers[src2]));
                break;
            }

            case OP_LE_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();
                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) <= AS_U64(vm.registers[src2]));
                break;
            }

            case OP_GT_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();
                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) > AS_U64(vm.registers[src2]));
                break;
            }

            case OP_GE_U64_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = read_BYTE();
                uint8_t src2 = read_BYTE();
                if (!IS_U64(vm.registers[src1]) || !IS_U64(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be u64");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }
                vm.registers[dst] = BOOL_VAL(AS_U64(vm.registers[src1]) >= AS_U64(vm.registers[src2]));
                break;
            }

            case OP_EQ_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                vm.registers[dst] = BOOL_VAL(
                    valuesEqual(vm.registers[src1], vm.registers[src2]));
                break;
            }

            case OP_NE_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                vm.registers[dst] = BOOL_VAL(
                    !valuesEqual(vm.registers[src1], vm.registers[src2]));
                break;
            }

            case OP_AND_BOOL_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be bool");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) &&
                                             AS_BOOL(vm.registers[src2]));
                break;
            }

            case OP_OR_BOOL_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_BOOL(vm.registers[src1]) || !IS_BOOL(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be bool");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(AS_BOOL(vm.registers[src1]) ||
                                             AS_BOOL(vm.registers[src2]));
                break;
            }

            case OP_NOT_BOOL_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();

                if (!IS_BOOL(vm.registers[src])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operand must be bool");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                vm.registers[dst] = BOOL_VAL(!AS_BOOL(vm.registers[src]));
                break;
            }

            case OP_CONCAT_R: {
                uint8_t dst = READ_BYTE();
                uint8_t src1 = READ_BYTE();
                uint8_t src2 = READ_BYTE();

                if (!IS_STRING(vm.registers[src1]) || !IS_STRING(vm.registers[src2])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Operands must be string");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                ObjString* a = AS_STRING(vm.registers[src1]);
                ObjString* b = AS_STRING(vm.registers[src2]);
                int newLen = a->length + b->length;
                char* buf = malloc(newLen + 1);
                memcpy(buf, a->chars, a->length);
                memcpy(buf + a->length, b->chars, b->length);
                buf[newLen] = '\0';
                ObjString* res = allocateString(buf, newLen);
                free(buf);
                vm.registers[dst] = STRING_VAL(res);
                break;
            }

            // Control flow
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vm.ip += offset;
                break;
            }

            case OP_JUMP_IF_NOT_R: {
                uint8_t reg = READ_BYTE();
                uint16_t offset = READ_SHORT();

                if (!IS_BOOL(vm.registers[reg])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0},
                                 "Condition must be boolean");
                    RETURN(INTERPRET_RUNTIME_ERROR);
                }

                if (!AS_BOOL(vm.registers[reg])) {
                    vm.ip += offset;
                }
                break;
            }

            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vm.ip -= offset;
                break;
            }

            // I/O operations
            case OP_PRINT_R: {
                uint8_t reg = READ_BYTE();
                
                // Check if this is a typed register first
                if (reg < REGISTER_COUNT && vm.typed_regs.reg_types[reg] != REG_TYPE_NONE) {
                    switch (vm.typed_regs.reg_types[reg]) {
                        case REG_TYPE_I32:
                            printf("%d", vm.typed_regs.i32_regs[reg]);
                            break;
                        case REG_TYPE_I64:
                            printf("%lld", (long long)vm.typed_regs.i64_regs[reg]);
                            break;
                        case REG_TYPE_F64:
                            printf("%g", vm.typed_regs.f64_regs[reg]);
                            break;
                        default:
                            printValue(vm.registers[reg]);
                            break;
                    }
                } else {
                    printValue(vm.registers[reg]);
                }
                
                printf("\n");
                fflush(stdout);
                break;
            }

            case OP_PRINT_NO_NL_R: {
                uint8_t reg = READ_BYTE();
                printValue(vm.registers[reg]);
                fflush(stdout);
                break;
            }

            // Function operations
            case OP_RETURN_R: {
                uint8_t reg = READ_BYTE();
                Value returnValue = vm.registers[reg];

                if (vm.frameCount > 0) {
                    CallFrame* frame = &vm.frames[--vm.frameCount];
                    vm.chunk = frame->previousChunk;
                    vm.ip = frame->returnAddress;

                    // Store return value in the result register
                    vm.registers[frame->baseRegister] = returnValue;
                } else {
                    // Top-level return
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    RETURN(INTERPRET_OK);
                }
                break;
            }

            case OP_RETURN_VOID: {
                if (vm.frameCount > 0) {
                    CallFrame* frame = &vm.frames[--vm.frameCount];
                    vm.chunk = frame->previousChunk;
                    vm.ip = frame->returnAddress;
                } else {
                    vm.lastExecutionTime = get_time_vm() - start_time;
                    RETURN(INTERPRET_OK);
                }
                break;
            }

            // Short jump optimizations for performance  
            case OP_JUMP_SHORT: {
                uint8_t offset = READ_BYTE();
                vm.ip += offset;
                break;
            }

            case OP_JUMP_BACK_SHORT: {
                uint8_t offset = READ_BYTE();
                vm.ip -= offset;
                break;
            }

            case OP_JUMP_IF_NOT_SHORT: {
                uint8_t reg = READ_BYTE();
                uint8_t offset = READ_BYTE();
                
                if (!IS_BOOL(vm.registers[reg])) {
                    runtimeError(ERROR_TYPE, (SrcLocation){NULL, 0, 0}, "Condition must be boolean");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                if (!AS_BOOL(vm.registers[reg])) {
                    vm.ip += offset;
                }
                break;
            }

            case OP_LOOP_SHORT: {
                uint8_t offset = READ_BYTE();
                vm.ip -= offset;
                break;
            }

            // Typed arithmetic operations for maximum performance (bypass Value boxing)
            case OP_ADD_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] + vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            case OP_SUB_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] - vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            case OP_MUL_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] * vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            case OP_DIV_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                if (vm.typed_regs.i32_regs[right] == 0) {
                    runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] / vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            case OP_MOD_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                if (vm.typed_regs.i32_regs[right] == 0) {
                    runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Modulo by zero");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[left] % vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            // Additional typed operations (I64, F64, comparisons, loads, moves)
            case OP_ADD_I64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] + vm.typed_regs.i64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_SUB_I64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] - vm.typed_regs.i64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_MUL_I64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] * vm.typed_regs.i64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_DIV_I64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                if (vm.typed_regs.i64_regs[right] == 0) {
                    runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0}, "Division by zero");
                    return INTERPRET_RUNTIME_ERROR;
                }
                
                vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[left] / vm.typed_regs.i64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_ADD_F64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] + vm.typed_regs.f64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                
                break;
            }

            case OP_SUB_F64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] - vm.typed_regs.f64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                
                break;
            }

            case OP_MUL_F64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] * vm.typed_regs.f64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                
                break;
            }

            case OP_DIV_F64_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[left] / vm.typed_regs.f64_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                
                break;
            }

            case OP_LT_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] < vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                
                break;
            }

            case OP_LE_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] <= vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                
                break;
            }

            case OP_GT_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] > vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                
                break;
            }

            case OP_GE_I32_TYPED: {
                uint8_t dst = READ_BYTE();
                uint8_t left = READ_BYTE();
                uint8_t right = READ_BYTE();
                
                vm.typed_regs.bool_regs[dst] = vm.typed_regs.i32_regs[left] >= vm.typed_regs.i32_regs[right];
                vm.typed_regs.reg_types[dst] = REG_TYPE_BOOL;
                
                break;
            }

            case OP_LOAD_I32_CONST: {
                uint8_t reg = READ_BYTE();
                uint16_t constantIndex = READ_SHORT();
                int32_t value = READ_CONSTANT(constantIndex).as.i32;
                
                vm.typed_regs.i32_regs[reg] = value;
                vm.typed_regs.reg_types[reg] = REG_TYPE_I32;
                
                break;
            }

            case OP_LOAD_I64_CONST: {
                uint8_t reg = READ_BYTE();
                uint16_t constantIndex = READ_SHORT();
                int64_t value = READ_CONSTANT(constantIndex).as.i64;
                
                vm.typed_regs.i64_regs[reg] = value;
                vm.typed_regs.reg_types[reg] = REG_TYPE_I64;
                
                break;
            }

            case OP_LOAD_F64_CONST: {
                uint8_t reg = READ_BYTE();
                uint16_t constantIndex = READ_SHORT();
                double value = READ_CONSTANT(constantIndex).as.f64;
                
                vm.typed_regs.f64_regs[reg] = value;
                vm.typed_regs.reg_types[reg] = REG_TYPE_F64;
                
                break;
            }

            case OP_MOVE_I32: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                
                vm.typed_regs.i32_regs[dst] = vm.typed_regs.i32_regs[src];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I32;
                
                break;
            }

            case OP_MOVE_I64: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                
                vm.typed_regs.i64_regs[dst] = vm.typed_regs.i64_regs[src];
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_MOVE_F64: {
                uint8_t dst = READ_BYTE();
                uint8_t src = READ_BYTE();
                
                vm.typed_regs.f64_regs[dst] = vm.typed_regs.f64_regs[src];
                vm.typed_regs.reg_types[dst] = REG_TYPE_F64;
                
                break;
            }

            case OP_TIME_STAMP: {
                uint8_t dst = READ_BYTE();
                
                // Get high-precision timestamp in nanoseconds
                int64_t timestamp = builtin_time_stamp();
                
                // Store in typed register as i64
                vm.typed_regs.i64_regs[dst] = timestamp;
                vm.typed_regs.reg_types[dst] = REG_TYPE_I64;
                
                break;
            }

            case OP_HALT:
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_OK);

            default:
                runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                             "Unknown opcode: %d", instruction);
                vm.lastExecutionTime = get_time_vm() - start_time;
                RETURN(INTERPRET_RUNTIME_ERROR);
        }

        if (IS_ERROR(vm.lastError)) {
            // Handle runtime errors
            if (vm.tryFrameCount > 0) {
                // Exception handling
                TryFrame frame = vm.tryFrames[--vm.tryFrameCount];
                vm.ip = frame.handler;
                vm.globals[frame.varIndex] = vm.lastError;
                vm.lastError = NIL_VAL;
            } else {
                RETURN(INTERPRET_RUNTIME_ERROR);
            }
        }
    }
#endif  // USE_COMPUTED_GOTO

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef RETURN
}


// Main interpretation functions
InterpretResult interpret(const char* source) {
    double t0 = now_ns();
    
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);

    Compiler compiler;
    initCompiler(&compiler, &chunk, "<repl>", source);

    double t1 = now_ns();
    fprintf(stderr, "[PROFILE] Init (chunk+compiler): %.3f ms\n", (t1 - t0) / 1e6);

    // Parse the source into an AST
    double t2 = now_ns();
    ASTNode* ast = parseSource(source);
    double t3 = now_ns();
    fprintf(stderr, "[PROFILE] parseSource: %.3f ms\n", (t3 - t2) / 1e6);
    
    if (!ast) {
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    // Compile the AST to bytecode
    double t4 = now_ns();
    if (!compile(ast, &compiler, false)) {
        freeAST(ast);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    double t5 = now_ns();
    fprintf(stderr, "[PROFILE] compile(AST→bytecode): %.3f ms\n", (t5 - t4) / 1e6);

    // Add a halt instruction at the end
    double t6 = now_ns();
    emitByte(&compiler, OP_HALT);
    double t7 = now_ns();
    fprintf(stderr, "[PROFILE] emit+patch: %.3f ms\n", (t7 - t6) / 1e6);

    // Execute the chunk
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.frameCount = 0;

    double t8 = now_ns();
    InterpretResult result = run();
    double t9 = now_ns();
    fprintf(stderr, "[PROFILE] run() execution: %.3f ms\n", (t9 - t8) / 1e6);

    freeAST(ast);
    freeCompiler(&compiler);
    freeChunk(&chunk);
    
    double t10 = now_ns();
    fprintf(stderr, "[PROFILE] TOTAL interpret(): %.3f ms\n", (t10 - t0) / 1e6);
    
    return result;
}

// Helper function to read file contents
static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        return NULL;
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        free(buffer);
        fclose(file);
        return NULL;
    }
    
    buffer[bytesRead] = '\0';
    
    fclose(file);
    return buffer;
}

// Helper function to get file modification time
__attribute__((unused)) static long getFileModTime(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return st.st_mtime;
    }
    return -1;
}

// Helper function to check if a module is already loaded
static bool isModuleLoaded(const char* path) {
    for (int i = 0; i < vm.moduleCount; i++) {
        if (vm.loadedModules[i] && strcmp(vm.loadedModules[i]->chars, path) == 0) {
            return true;
        }
    }
    return false;
}

// Helper function to add module to loaded modules list
static void addLoadedModule(const char* path) {
    if (vm.moduleCount < UINT8_COUNT) {
        vm.loadedModules[vm.moduleCount] = allocateString(path, strlen(path));
        vm.moduleCount++;
    }
}

InterpretResult interpret_module(const char* path) {
    double t0 = now_ns();
    
    if (!path) {
        fprintf(stderr, "Module path cannot be null.\n");
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Check if module is already loaded to prevent circular dependencies
    if (isModuleLoaded(path)) {
        // Module already loaded, return success
        return INTERPRET_OK;
    }
    
    // Read the module file
    double t1 = now_ns();
    char* source = readFile(path);
    double t2 = now_ns();
    fprintf(stderr, "[PROFILE] ReadFile: %.3f ms\n", (t2 - t1) / 1e6);
    
    if (!source) {
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);
    
    // Extract module name from path (filename without extension)
    const char* fileName = strrchr(path, '/');
    if (!fileName) fileName = strrchr(path, '\\'); // Windows path separator
    if (!fileName) fileName = path;
    else fileName++; // Skip the separator
    
    // Create compiler for the module
    Compiler compiler;
    initCompiler(&compiler, &chunk, fileName, source);
    
    double t3 = now_ns();
    fprintf(stderr, "[PROFILE] Init (chunk+compiler): %.3f ms\n", (t3 - t2) / 1e6);
    
    // Parse the module source into an AST
    double t4 = now_ns();
    ASTNode* ast = parseSource(source);
    double t5 = now_ns();
    fprintf(stderr, "[PROFILE] parseSource: %.3f ms\n", (t5 - t4) / 1e6);
    
    if (!ast) {
        fprintf(stderr, "Failed to parse module: %s\n", path);
        free(source);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    
    // Compile the AST to bytecode (mark as module)
    double t6 = now_ns();
    if (!compile(ast, &compiler, true)) {
        fprintf(stderr, "Failed to compile module: %s\n", path);
        freeAST(ast);
        free(source);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    double t7 = now_ns();
    fprintf(stderr, "[PROFILE] compile(AST→bytecode): %.3f ms\n", (t7 - t6) / 1e6);
    
    // Add a halt instruction at the end
    double t8 = now_ns();
    emitByte(&compiler, OP_HALT);
    double t9 = now_ns();
    fprintf(stderr, "[PROFILE] emit+patch: %.3f ms\n", (t9 - t8) / 1e6);
    
    // Store current VM state
    Chunk* oldChunk = vm.chunk;
    uint8_t* oldIP = vm.ip;
    const char* oldFilePath = vm.filePath;
    
    // Set VM state for module execution
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.filePath = path;
    
    // Execute the module
    double t10 = now_ns();
    InterpretResult result = run();
    double t11 = now_ns();
    fprintf(stderr, "[PROFILE] run() execution: %.3f ms\n", (t11 - t10) / 1e6);
    
    // Restore VM state
    vm.chunk = oldChunk;
    vm.ip = oldIP;
    vm.filePath = oldFilePath;
    
    // Add module to loaded modules list if successful
    if (result == INTERPRET_OK) {
        addLoadedModule(path);
    } else {
        fprintf(stderr, "Runtime error in module: %s\n", path);
    }
    
    // Clean up
    freeAST(ast);
    free(source);
    freeCompiler(&compiler);
    freeChunk(&chunk);
    
    double t12 = now_ns();
    fprintf(stderr, "[PROFILE] TOTAL interpret_module(): %.3f ms\n", (t12 - t0) / 1e6);
    
    return result;
}