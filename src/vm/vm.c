// register_vm.c - Register-based VM implementation
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vm.h"
#include "common.h"
#include "compiler.h"
#include "parser.h"
#include "memory.h"

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


// Compiler operations
// Type system (simplified)
static Type primitiveTypes[TYPE_ANY + 1];

void initTypeSystem(void) {
    for (int i = 0; i <= TYPE_ANY; i++) {
        primitiveTypes[i].kind = (TypeKind)i;
    }
}

Type* getPrimitiveType(TypeKind kind) {
    if (kind <= TYPE_ANY) {
        return &primitiveTypes[kind];
    }
    return NULL;
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


// Main execution engine
static InterpretResult run(void) {
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT(index) (vm.chunk->constants.values[index])

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
                uint8_t constantIndex = READ_BYTE();
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
                    return INTERPRET_RUNTIME_ERROR;
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                int32_t result;

                if (__builtin_add_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.registers[dst] = I32_VAL(result);
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                int32_t result;

                if (__builtin_sub_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.registers[dst] = I32_VAL(result);
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                int32_t a = AS_I32(vm.registers[src1]);
                int32_t b = AS_I32(vm.registers[src2]);
                int32_t result;

                if (__builtin_mul_overflow(a, b, &result)) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Integer overflow");
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.registers[dst] = I32_VAL(result);
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    return INTERPRET_RUNTIME_ERROR;
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                int32_t b = AS_I32(vm.registers[src2]);
                if (b == 0) {
                    runtimeError(ERROR_VALUE, (SrcLocation){NULL, 0, 0},
                                 "Division by zero");
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.registers[dst] = I32_VAL(AS_I32(vm.registers[src1]) % b);
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
                    return INTERPRET_RUNTIME_ERROR;
                }

                vm.registers[dst] = BOOL_VAL(AS_I32(vm.registers[src1]) <
                                             AS_I32(vm.registers[src2]));
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
                    return INTERPRET_RUNTIME_ERROR;
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
                    return INTERPRET_OK;
                }
                break;
            }

            case OP_RETURN_VOID: {
                if (vm.frameCount > 0) {
                    CallFrame* frame = &vm.frames[--vm.frameCount];
                    vm.chunk = frame->previousChunk;
                    vm.ip = frame->returnAddress;
                } else {
                    return INTERPRET_OK;
                }
                break;
            }

            case OP_HALT:
                return INTERPRET_OK;

            default:
                runtimeError(ERROR_RUNTIME, (SrcLocation){NULL, 0, 0},
                             "Unknown opcode: %d", instruction);
                return INTERPRET_RUNTIME_ERROR;
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
                return INTERPRET_RUNTIME_ERROR;
            }
        }
    }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
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