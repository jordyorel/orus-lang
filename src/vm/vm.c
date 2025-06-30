// register_vm.c - Register-based VM implementation
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vm.h"
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

// Global VM instance
VM vm;

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

// VM initialization
void initVM(void) {
    initTypeSystem();

    initMemory();

    // Clear registers
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = NIL_VAL;
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
    static void* dispatchTable[OP_HALT + 1] = {0};
    if (!dispatchTable[OP_HALT]) {
        dispatchTable[OP_LOAD_CONST] = &&LABEL_OP_LOAD_CONST;
        dispatchTable[OP_LOAD_NIL] = &&LABEL_OP_LOAD_NIL;
        dispatchTable[OP_LOAD_TRUE] = &&LABEL_OP_LOAD_TRUE;
        dispatchTable[OP_LOAD_FALSE] = &&LABEL_OP_LOAD_FALSE;
        dispatchTable[OP_MOVE] = &&LABEL_OP_MOVE;
        dispatchTable[OP_LOAD_GLOBAL] = &&LABEL_OP_LOAD_GLOBAL;
        dispatchTable[OP_STORE_GLOBAL] = &&LABEL_OP_STORE_GLOBAL;
        dispatchTable[OP_ADD_I32_R] = &&LABEL_OP_ADD_I32_R;
        dispatchTable[OP_SUB_I32_R] = &&LABEL_OP_SUB_I32_R;
        dispatchTable[OP_MUL_I32_R] = &&LABEL_OP_MUL_I32_R;
        dispatchTable[OP_DIV_I32_R] = &&LABEL_OP_DIV_I32_R;
        dispatchTable[OP_MOD_I32_R] = &&LABEL_OP_MOD_I32_R;
        dispatchTable[OP_INC_I32_R] = &&LABEL_OP_INC_I32_R;
        dispatchTable[OP_DEC_I32_R] = &&LABEL_OP_DEC_I32_R;
        dispatchTable[OP_ADD_I64_R] = &&LABEL_OP_ADD_I64_R;
        dispatchTable[OP_SUB_I64_R] = &&LABEL_OP_SUB_I64_R;
        dispatchTable[OP_MUL_I64_R] = &&LABEL_OP_MUL_I64_R;
        dispatchTable[OP_DIV_I64_R] = &&LABEL_OP_DIV_I64_R;
        dispatchTable[OP_MOD_I64_R] = &&LABEL_OP_MOD_I64_R;
        dispatchTable[OP_I32_TO_I64_R] = &&LABEL_OP_I32_TO_I64_R;
        dispatchTable[OP_ADD_F64_R] = &&LABEL_OP_ADD_F64_R;
        dispatchTable[OP_SUB_F64_R] = &&LABEL_OP_SUB_F64_R;
        dispatchTable[OP_MUL_F64_R] = &&LABEL_OP_MUL_F64_R;
        dispatchTable[OP_DIV_F64_R] = &&LABEL_OP_DIV_F64_R;
        dispatchTable[OP_LT_F64_R] = &&LABEL_OP_LT_F64_R;
        dispatchTable[OP_LE_F64_R] = &&LABEL_OP_LE_F64_R;
        dispatchTable[OP_GT_F64_R] = &&LABEL_OP_GT_F64_R;
        dispatchTable[OP_GE_F64_R] = &&LABEL_OP_GE_F64_R;
        dispatchTable[OP_I32_TO_F64_R] = &&LABEL_OP_I32_TO_F64_R;
        dispatchTable[OP_I64_TO_F64_R] = &&LABEL_OP_I64_TO_F64_R;
        dispatchTable[OP_F64_TO_I32_R] = &&LABEL_OP_F64_TO_I32_R;
        dispatchTable[OP_F64_TO_I64_R] = &&LABEL_OP_F64_TO_I64_R;
        dispatchTable[OP_LT_I32_R] = &&LABEL_OP_LT_I32_R;
        dispatchTable[OP_LE_I32_R] = &&LABEL_OP_LE_I32_R;
        dispatchTable[OP_GT_I32_R] = &&LABEL_OP_GT_I32_R;
        dispatchTable[OP_GE_I32_R] = &&LABEL_OP_GE_I32_R;
        dispatchTable[OP_LT_I64_R] = &&LABEL_OP_LT_I64_R;
        dispatchTable[OP_LE_I64_R] = &&LABEL_OP_LE_I64_R;
        dispatchTable[OP_GT_I64_R] = &&LABEL_OP_GT_I64_R;
        dispatchTable[OP_GE_I64_R] = &&LABEL_OP_GE_I64_R;
        dispatchTable[OP_EQ_R] = &&LABEL_OP_EQ_R;
        dispatchTable[OP_NE_R] = &&LABEL_OP_NE_R;
        dispatchTable[OP_AND_BOOL_R] = &&LABEL_OP_AND_BOOL_R;
        dispatchTable[OP_OR_BOOL_R] = &&LABEL_OP_OR_BOOL_R;
        dispatchTable[OP_CONCAT_R] = &&LABEL_OP_CONCAT_R;
        dispatchTable[OP_JUMP] = &&LABEL_OP_JUMP;
        dispatchTable[OP_JUMP_IF_NOT_R] = &&LABEL_OP_JUMP_IF_NOT_R;
        dispatchTable[OP_LOOP] = &&LABEL_OP_LOOP;
        dispatchTable[OP_GET_ITER_R] = &&LABEL_OP_GET_ITER_R;
        dispatchTable[OP_ITER_NEXT_R] = &&LABEL_OP_ITER_NEXT_R;
        dispatchTable[OP_PRINT_MULTI_R] = &&LABEL_OP_PRINT_MULTI_R;
        dispatchTable[OP_PRINT_R] = &&LABEL_OP_PRINT_R;
        dispatchTable[OP_PRINT_NO_NL_R] = &&LABEL_OP_PRINT_NO_NL_R;
        dispatchTable[OP_RETURN_R] = &&LABEL_OP_RETURN_R;
        dispatchTable[OP_RETURN_VOID] = &&LABEL_OP_RETURN_VOID;
        dispatchTable[OP_HALT] = &&LABEL_OP_HALT;
    }


    uint8_t instruction;

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
        if (instruction > OP_HALT || dispatchTable[instruction] == NULL) { \
            goto LABEL_UNKNOWN; \
        } \
        goto *dispatchTable[instruction]; \
    } while (0)
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
        double b = AS_F64(vm.registers[src2]);
        if (b == 0.0) {
            runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
            RETURN(INTERPRET_RUNTIME_ERROR);
        }
        vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) / b);
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
        printValue(vm.registers[reg]);
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

LABEL_OP_HALT:
    vm.lastExecutionTime = get_time_vm() - start_time;
    RETURN(INTERPRET_OK);

LABEL_UNKNOWN:
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
                double b = AS_F64(vm.registers[src2]);
                if (b == 0.0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0}, "Division by zero");
                    return INTERPRET_RUNTIME_ERROR;
                }
                vm.registers[dst] = F64_VAL(AS_F64(vm.registers[src1]) / b);
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
                printValue(vm.registers[reg]);
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
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);

    Compiler compiler;
    initCompiler(&compiler, &chunk, "<repl>", source);

    // Parse the source into an AST
    ASTNode* ast = parseSource(source);
    if (!ast) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    // Compile the AST to bytecode
    if (!compile(ast, &compiler, false)) {
        freeAST(ast);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    // Add a halt instruction at the end
    emitByte(&compiler, OP_HALT);

    // Execute the chunk
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.frameCount = 0;

    InterpretResult result = run();

    freeAST(ast);
    freeChunk(&chunk);
    return result;
}

InterpretResult interpret_module(const char* path) {
    UNUSED(path);
    // Simplified module interpretation
    return interpret("");
}