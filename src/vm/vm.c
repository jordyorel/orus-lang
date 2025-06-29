// register_vm.c - Register-based VM implementation
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vm.h"
#include "common.h"
#include "compiler.h"
#include "parser.h"

// Global VM instance
VM vm;

// Forward declarations
static InterpretResult run(void);
static void runtimeError(ErrorType type, SrcLocation location,
                         const char* format, ...);
bool compileExpression(ASTNode* node, Compiler* compiler);
int compileExpressionToRegister(ASTNode* node, Compiler* compiler);

// Memory allocation macros
#define GROW_CAPACITY(capacity) ((capacity) < 8 ? 8 : (capacity) * 2)
#define GROW_ARRAY(type, pointer, oldCount, newCount)     \
    (type*)reallocate(pointer, sizeof(type) * (oldCount), \
                      sizeof(type) * (newCount))
#define FREE_ARRAY(type, pointer, oldCount) \
    reallocate(pointer, sizeof(type) * (oldCount), 0)

// Memory management
static void* reallocate(void* pointer, size_t oldSize, size_t newSize) {
    vm.bytesAllocated += newSize - oldSize;

    if (newSize == 0) {
        free(pointer);
        return NULL;
    }

    void* result = realloc(pointer, newSize);
    if (result == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        exit(1);
    }

    return result;
}

// Object allocation
static Obj* allocateObject(size_t size) {
    Obj* object = (Obj*)reallocate(NULL, 0, size);
    object->next = vm.objects;
    object->isMarked = false;
    vm.objects = object;
    return object;
}

ObjString* allocateString(const char* chars, int length) {
    ObjString* string = (ObjString*)allocateObject(sizeof(ObjString));
    string->obj.type = OBJ_STRING;
    string->length = length;
    string->chars = (char*)malloc(length + 1);
    memcpy(string->chars, chars, length);
    string->chars[length] = '\0';

    // Simple hash
    uint32_t hash = 2166136261u;
    for (int i = 0; i < length; i++) {
        hash ^= (uint8_t)chars[i];
        hash *= 16777619;
    }
    string->hash = hash;

    return string;
}

ObjArray* allocateArray(int capacity) {
    ObjArray* array = (ObjArray*)allocateObject(sizeof(ObjArray));
    array->obj.type = OBJ_ARRAY;
    array->length = 0;
    array->capacity = capacity;
    array->elements = GROW_ARRAY(Value, NULL, 0, capacity);
    return array;
}

ObjError* allocateError(ErrorType type, const char* message,
                        SrcLocation location) {
    ObjError* error = (ObjError*)allocateObject(sizeof(ObjError));
    error->obj.type = OBJ_ERROR;
    error->type = type;
    error->message = allocateString(message, strlen(message));
    error->location.file = location.file;
    error->location.line = location.line;
    error->location.column = location.column;
    return error;
}

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

// Chunk operations
void initChunk(Chunk* chunk) {
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    chunk->lines = NULL;
    chunk->columns = NULL;
    chunk->constants.count = 0;
    chunk->constants.capacity = 0;
    chunk->constants.values = NULL;
}

void freeChunk(Chunk* chunk) {
    FREE_ARRAY(uint8_t, chunk->code, chunk->capacity);
    FREE_ARRAY(int, chunk->lines, chunk->capacity);
    FREE_ARRAY(int, chunk->columns, chunk->capacity);
    FREE_ARRAY(Value, chunk->constants.values, chunk->constants.capacity);
    initChunk(chunk);
}

void writeChunk(Chunk* chunk, uint8_t byte, int line, int column) {
    if (chunk->capacity < chunk->count + 1) {
        int oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code =
            GROW_ARRAY(uint8_t, chunk->code, oldCapacity, chunk->capacity);
        chunk->lines =
            GROW_ARRAY(int, chunk->lines, oldCapacity, chunk->capacity);
        chunk->columns =
            GROW_ARRAY(int, chunk->columns, oldCapacity, chunk->capacity);
    }

    chunk->code[chunk->count] = byte;
    chunk->lines[chunk->count] = line;
    chunk->columns[chunk->count] = column;
    chunk->count++;
}

int addConstant(Chunk* chunk, Value value) {
    if (chunk->constants.capacity < chunk->constants.count + 1) {
        int oldCapacity = chunk->constants.capacity;
        chunk->constants.capacity = GROW_CAPACITY(oldCapacity);
        chunk->constants.values =
            GROW_ARRAY(Value, chunk->constants.values, oldCapacity,
                       chunk->constants.capacity);
    }

    chunk->constants.values[chunk->constants.count] = value;
    return chunk->constants.count++;
}

// Compiler operations
void initCompiler(Compiler* compiler, Chunk* chunk, const char* fileName,
                  const char* source) {
    compiler->chunk = chunk;
    compiler->fileName = fileName;
    compiler->source = source;
    compiler->nextRegister = 0;
    compiler->maxRegisters = 0;
    compiler->localCount = 0;
    compiler->hadError = false;
}

uint8_t allocateRegister(Compiler* compiler) {
    if (compiler->nextRegister >= (REGISTER_COUNT - 1)) {
        compiler->hadError = true;
        return 0;
    }

    uint8_t reg = compiler->nextRegister++;
    if (compiler->nextRegister > compiler->maxRegisters) {
        compiler->maxRegisters = compiler->nextRegister;
    }

    return reg;
}

void freeRegister(Compiler* compiler, uint8_t reg) {
    // In a simple allocator, we can just decrement if it's the last allocated
    if (reg == compiler->nextRegister - 1) {
        compiler->nextRegister--;
    }
}

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
    vm.objects = NULL;
    vm.bytesAllocated = 0;
    vm.gcPaused = false;
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

// Memory management
void freeObjects(void) {
    Obj* object = vm.objects;
    while (object != NULL) {
        Obj* next = object->next;

        switch (object->type) {
            case OBJ_STRING:
                free(((ObjString*)object)->chars);
                break;
            case OBJ_ARRAY:
                FREE_ARRAY(Value, ((ObjArray*)object)->elements,
                           ((ObjArray*)object)->capacity);
                break;
            case OBJ_ERROR:
                // ObjError's message is freed when the string is freed
                break;
            case OBJ_RANGE_ITERATOR:
                // No additional cleanup needed
                break;
        }

        free(object);
        object = next;
    }

    vm.objects = NULL;
}

void collectGarbage(void) {
    // Simplified GC - mark all reachable objects
    // TODO: Implement proper mark-and-sweep
}

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
void disassembleChunk(Chunk* chunk, const char* name) {
    printf("== %s ==\n", name);

    for (int offset = 0; offset < chunk->count;) {
        offset = disassembleInstruction(chunk, offset);
    }
}

int disassembleInstruction(Chunk* chunk, int offset) {
    printf("%04d ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];
    switch (instruction) {
        case OP_LOAD_CONST: {
            uint8_t reg = chunk->code[offset + 1];
            uint8_t constant = chunk->code[offset + 2];
            printf("%-16s R%d, #%d '", "LOAD_CONST", reg, constant);
            printValue(chunk->constants.values[constant]);
            printf("'\n");
            return offset + 3;
        }

        case OP_LOAD_NIL: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "LOAD_NIL", reg);
            return offset + 2;
        }

        case OP_MOVE: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src = chunk->code[offset + 2];
            printf("%-16s R%d, R%d\n", "MOVE", dst, src);
            return offset + 3;
        }

        case OP_ADD_I32_R: {
            uint8_t dst = chunk->code[offset + 1];
            uint8_t src1 = chunk->code[offset + 2];
            uint8_t src2 = chunk->code[offset + 3];
            printf("%-16s R%d, R%d, R%d\n", "ADD_I32", dst, src1, src2);
            return offset + 4;
        }

        case OP_PRINT_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "PRINT", reg);
            return offset + 2;
        }

        case OP_RETURN_R: {
            uint8_t reg = chunk->code[offset + 1];
            printf("%-16s R%d\n", "RETURN", reg);
            return offset + 2;
        }

        case OP_HALT:
            printf("%-16s\n", "HALT");
            return offset + 1;

        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
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

// Simple compiler for testing
void emitByte(Compiler* compiler, uint8_t byte) {
    writeChunk(compiler->chunk, byte, 1, 1);
}

// For future use
__attribute__((unused))
void emitBytes(Compiler* compiler, uint8_t byte1, uint8_t byte2) {
    emitByte(compiler, byte1);
    emitByte(compiler, byte2);
}

void emitConstant(Compiler* compiler, uint8_t reg, Value value) {
    int constant = addConstant(compiler->chunk, value);
    if (constant > UINT8_MAX) {
        compiler->hadError = true;
        return;
    }
    emitByte(compiler, OP_LOAD_CONST);
    emitByte(compiler, reg);
    emitByte(compiler, (uint8_t)constant);
}

// Basic compilation (simplified for testing)
bool compile(ASTNode* ast, Compiler* compiler, bool isModule) {
    UNUSED(isModule);

    if (!ast) {
        return false;
    }

    if (ast->type == NODE_PROGRAM) {
        for (int i = 0; i < ast->program.count; i++) {
            ASTNode* stmt = ast->program.declarations[i];
            int reg = compileExpressionToRegister(stmt, compiler);
            if (reg < 0) return false;
            if (!isModule && stmt->type != NODE_VAR_DECL) {
                emitByte(compiler, OP_PRINT_R);
                emitByte(compiler, (uint8_t)reg);
            }
        }
        return true;
    }

    int resultReg = compileExpressionToRegister(ast, compiler);

    if (resultReg >= 0 && !isModule && ast->type != NODE_VAR_DECL) {
        emitByte(compiler, OP_PRINT_R);
        emitByte(compiler, (uint8_t)resultReg);
    }

    return resultReg >= 0;
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