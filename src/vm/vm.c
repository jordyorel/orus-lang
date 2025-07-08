// register_vm.c - Register-based VM implementation
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ✅ Phase 1: Auto-detect computed goto support
#ifndef USE_COMPUTED_GOTO
  #if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
  #else
    #define USE_COMPUTED_GOTO 0
  #endif
#endif

#include "vm.h"
#include "vm_dispatch.h"
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

double get_time_vm(void) {
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
void runtimeError(ErrorType type, SrcLocation location,
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

// Runtime error handling
void runtimeError(ErrorType type, SrcLocation location,
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
    return vm_run_dispatch();
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
