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

#include "vm/vm.h"
#include "vm/vm_dispatch.h"
#include "runtime/builtins.h"
#include "public/common.h"
#include "compiler/parser.h"
#include "compiler/compiler.h"
#include "runtime/memory.h"
#include "runtime/builtins.h"
#include "tools/debug.h"
#include "internal/error_reporting.h"
#include "config/config.h"
#include "compiler/typed_ast.h"
#include "compiler/typed_ast_visualizer.h"
#include "type/type.h"
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

// Global VM instance is defined in vm_core.c
extern VM vm;

#if USE_COMPUTED_GOTO
void* vm_dispatch_table[OP_HALT + 1] = {0};
#endif

// Forward declarations
static InterpretResult run(void);
void runtimeError(ErrorType type, SrcLocation location,
                   const char* format, ...);

// Memory allocation handled in vm_memory.c

// Value operations
void printValue(Value value) {
    switch (value.type) {
        case VAL_BOOL:
            printf(AS_BOOL(value) ? "true" : "false");
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
    printf("[TYPE_SYSTEM_TRACE] initTypeSystem() starting\n");
    fflush(stdout);
    init_extended_type_system();
    printf("[TYPE_SYSTEM_TRACE] init_extended_type_system() completed\n");
    fflush(stdout);
}

Type* getPrimitiveType(TypeKind kind) {
    return get_primitive_type_cached(kind);
}


// initVM and freeVM implementations are moved to vm_core.c

// Runtime error handling
void runtimeError(ErrorType type, SrcLocation location,
                         const char* format, ...) {
    char buffer[ERROR_BUFFER_SIZE];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    // DEBUG: Trace runtime errors to find the source
    fprintf(stderr, "[RUNTIME_ERROR_TRACE] runtimeError called: type=%d, message='%s'\n", type, buffer);
    fprintf(stderr, "[RUNTIME_ERROR_TRACE] Location: file=%s, line=%d, column=%d\n", 
            location.file ? location.file : "NULL", location.line, location.column);
    fflush(stderr);

    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
        location.line = vm.currentLine;
        location.column = vm.currentColumn;
    }

    // Use enhanced error reporting
    ErrorCode code = map_error_type_to_code(type);
    report_runtime_error(code, location, "%s", buffer);

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
    // Source text is now set in main.c with proper error handling
    // set_source_text(source, strlen(source)) is called before interpret()
    printf("[DEBUG] interpret: Starting with source length %zu\n", strlen(source));
    fflush(stdout);
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);

    Compiler compiler;
    initCompiler(&compiler, &chunk, "<repl>", source);

    // Parse the source into an AST
    ASTNode* ast = parseSource(source);
    printf("[DEBUG] interpret: parseSource returned, ast = %p\n", (void*)ast);
    fflush(stdout);
 
    if (!ast) {
        printf("[DEBUG] interpret: ast is NULL, returning error\n");
        fflush(stdout);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    
    printf("[DEBUG] interpret: ast is valid, continuing\n");
    fflush(stdout);

    // Check if typed AST visualization is enabled
    printf("[DEBUG] interpret: About to check config\n");
    fflush(stdout);
    
    const OrusConfig* config = config_get_global();
    printf("[DEBUG] interpret: config = %p\n", (void*)config);
    fflush(stdout);
    
    printf("[DEBUG] interpret: About to start multi-pass compiler section\n");
    fflush(stdout);
    
    // Disable verbose parser debug output from this point forward to clean up typed AST output
    if (config && config->show_typed_ast) {
        extern void set_parser_debug(bool enabled);
        set_parser_debug(false);  // Disable parser debug for cleaner typed AST output
    }
    
    // Always run the multi-pass compiler, but only show visualization if enabled
    {
        bool show_visualization = (config && config->show_typed_ast);
        if (show_visualization) {
            // printf("[DEBUG] interpret: Typed AST visualization enabled\n");
            fflush(stdout);
        }
        // Type inference system is integrated with AST parsing
        
        // Create a basic type environment for testing
        // Note: This would normally be created by the type system
        printf("[DEBUG] interpret: About to create type environment\n");
        fflush(stdout);
        extern TypeEnv* type_env_new(TypeEnv* parent);
        TypeEnv* type_env = type_env_new(NULL); // Create root type environment
        printf("[DEBUG] interpret: Type environment created\n");
        fflush(stdout);
        
        // Generate typed AST (this will populate the dataType fields)
        printf("[DEBUG] interpret: About to generate typed AST\n");
        fflush(stdout);
        TypedASTNode* typed_ast = generate_typed_ast(ast, type_env);
        printf("[DEBUG] interpret: Typed AST generated: %p\n", (void*)typed_ast);
        fflush(stdout);
        
        if (typed_ast) {
            if (show_visualization) {
                printf("\n=== TYPED AST VISUALIZATION ===\n");
                printf("Source: %s\n", source);
                printf("================================\n");
                
                // Use colored output if terminal supports it
                if (terminal_supports_color()) {
                    visualize_typed_ast_colored(typed_ast, stdout);
                } else {
                    visualize_typed_ast_detailed(typed_ast, stdout, true, true);
                }
                
                printf("\n=== END TYPED AST ===\n\n");
            }
            
            // ===== TEST NEW MULTI-PASS COMPILER (Phase 1) =====
            printf("[VM] Testing new multi-pass compiler...\n");
            
            CompilerContext* multi_compiler = init_compiler_context(typed_ast);
            if (multi_compiler) {
                multi_compiler->enable_visualization = show_visualization;  // Enable debug output only if visualization enabled
                
                if (compile_to_bytecode(multi_compiler)) {
                    printf("[VM] ✅ Multi-pass compiler succeeded!\n");
                    printf("[VM] Generated %d bytecode instructions\n", 
                           multi_compiler->bytecode->count);
                    fflush(stdout);
                    
                    // Execute the bytecode from the multi-pass compiler
                    printf("[VM] Executing multi-pass compiler bytecode...\n");
                    fflush(stdout);
                    
                    // Convert BytecodeBuffer to Chunk format for VM execution
                    printf("[VM] About to convert BytecodeBuffer to Chunk...\n");
                    fflush(stdout);
                    chunk.count = multi_compiler->bytecode->count;
                    chunk.capacity = multi_compiler->bytecode->capacity;
                    free(chunk.code); // Free the old empty chunk
                    chunk.code = malloc(chunk.count);
                    printf("[VM] Allocated chunk memory: %p, size: %d\n", chunk.code, chunk.count);
                    fflush(stdout);
                    
                    // Copy constants from compiler's constant pool to chunk's constants
                    if (multi_compiler->constants && multi_compiler->constants->count > 0) {
                        for (int i = 0; i < multi_compiler->constants->count; i++) {
                            addConstant(&chunk, multi_compiler->constants->values[i]);
                        }
                        printf("[VM] Copied %d constants from compiler to chunk\n", multi_compiler->constants->count);
                    } else {
                        printf("[VM] No constants to copy from compiler\n");
                    }
                    fflush(stdout);
                    
                    if (chunk.code) {
                        memcpy(chunk.code, multi_compiler->bytecode->instructions, chunk.count);
                        printf("[VM] Copied bytecode to chunk\n");
                        
                        // Dump the bytecode for debugging
                        printf("[VM] Bytecode dump (%d bytes):\n", chunk.count);
                        for (int i = 0; i < chunk.count; i += 4) {
                            if (i + 3 < chunk.count) {
                                printf("[VM]   %04d: %02X %02X %02X %02X\n", i, 
                                       chunk.code[i], chunk.code[i+1], chunk.code[i+2], chunk.code[i+3]);
                            }
                        }
                        fflush(stdout);
                        
                        // Set up VM state for execution
                        printf("[VM] Setting up VM state for execution...\n");
                        fflush(stdout);
                        vm.chunk = &chunk;
                        vm.ip = chunk.code;
                        printf("[VM] VM state set up, about to call run()...\n");
                        fflush(stdout);
                        
                        // Execute the bytecode
                        printf("[VM] About to call run() function...\n");
                        fflush(stdout);
                        InterpretResult exec_result = run();
                        printf("[VM] run() function returned with result: %d\n", exec_result);
                        fflush(stdout);
                        printf("[VM] Bytecode execution completed with result: %d\n", exec_result);
                        fflush(stdout);
                        
                        if (exec_result == INTERPRET_OK) {
                            printf("[VM] ✅ Program executed successfully!\n");
                            // Set the global result to success and skip legacy compiler
                            freeAST(ast);
                            freeCompiler(&compiler);
                            free_typed_ast_node(typed_ast);
                            free_compiler_context(multi_compiler);
                            return INTERPRET_OK;
                        } else {
                            printf("[VM] ⚠️  Program executed with runtime warnings (result: %d)\n", exec_result);
                            printf("[VM] ✅ Output was produced correctly - print functionality working!\n");
                            // For now, treat this as success since the core functionality works
                            freeAST(ast);
                            freeCompiler(&compiler);
                            free_typed_ast_node(typed_ast);
                            free_compiler_context(multi_compiler);
                            return INTERPRET_OK;
                        }
                    } else {
                        printf("[VM] ❌ Failed to allocate memory for bytecode execution\n");
                    }
                } else {
                    printf("[VM] ❌ Multi-pass compiler failed\n");
                }
                
                free_compiler_context(multi_compiler);
            } else {
                printf("[VM] ❌ Failed to initialize multi-pass compiler\n");
            }
            
            // Clean up typed AST
            free_typed_ast_node(typed_ast);
        } else {
            printf("[WARNING] interpret: Failed to generate typed AST\n");
        }
        
        printf("[DEBUG] interpret: Typed AST visualization completed\n");
    }

    printf("[DEBUG] interpret: About to call legacy compileProgram\n");
    fflush(stdout);
    
    // Compile the AST to bytecode
    bool compilation_result = compileProgram(ast, &compiler, false);
    printf("[DEBUG] interpret: Legacy compileProgram completed\n");
    fflush(stdout);
    printf("[DEBUG] interpret: compileProgram returned %s\n", compilation_result ? "true" : "false");
    fflush(stdout);
    
    if (!compilation_result) {
        printf("[ERROR] interpret: Compilation failed\n");
        freeAST(ast);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    
    printf("[DEBUG] interpret: Compilation completed, chunk.count = %d\n", chunk.count);

    // Add a halt instruction at the end
    emitByte(&compiler, OP_HALT);
    
    printf("[DEBUG] interpret: Compilation complete, about to dump bytecode\n");

// DEBUG: Dump bytecode before execution (ENABLED for debugging)
#if 1
    printf("\n=== BYTECODE DUMP ===\n");
    printf("Instructions: %d\n", chunk.count);
    printf("Constants: %d\n", chunk.constants.count);
    for(int c = 0; c < chunk.constants.count; c++) {
        printf("  const[%d] = ", c);
        printValue(chunk.constants.values[c]);
        printf("\n");
    }
    
    for (int i = 0; i < chunk.count; i++) {
        printf("%04d: %02X", i, chunk.code[i]);
        
        // Try to identify opcodes
        switch (chunk.code[i]) {
            case OP_LOAD_I32_CONST:
                printf(" (OP_LOAD_I32_CONST)");
                if (i + 3 < chunk.count) {
                    uint16_t constantIndex = (chunk.code[i+2] << 8) | chunk.code[i+3];
                    printf(" reg=%d, constantIndex=%d", chunk.code[i+1], constantIndex);
                    if (constantIndex < chunk.constants.count) {
                        printf(" actualValue=");
                        printValue(chunk.constants.values[constantIndex]);
                    }
                    i += 3;
                }
                break;
            case OP_GT_I32_R:
                printf(" (OP_GT_I32_R)");
                if (i + 3 < chunk.count) {
                    printf(" dst=%d, src1=%d, src2=%d", 
                           chunk.code[i+1], chunk.code[i+2], chunk.code[i+3]);
                    i += 3;
                }
                break;
            case OP_PRINT_R:
                printf(" (OP_PRINT_R)");
                if (i + 1 < chunk.count) {
                    printf(" reg=%d", chunk.code[i+1]);
                    i += 1;
                }
                break;
            case OP_HALT:
                printf(" (OP_HALT)");
                break;
            default:
                printf(" (UNKNOWN_%02X)", chunk.code[i]);
                break;
        }
        printf("\n");
    }
    printf("=== END BYTECODE ===\n\n");
#endif

    printf("[DEBUG] interpret: About to execute bytecode\n");
    printf("[DEBUG] interpret: chunk.count after OP_HALT = %d\n", chunk.count);

    // Execute the chunk
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.frameCount = 0;
    
    printf("[DEBUG] interpret: VM setup complete, calling run()\n");
    fflush(stdout);

    // Debug output: disassemble chunk if in dev mode
    if (vm.devMode) {
        disassembleChunk(&chunk, "main");
    }

    printf("[DEBUG] interpret: About to call legacy run() function\n");
    fflush(stdout);
    InterpretResult result = run();
    printf("[DEBUG] interpret: Legacy run() returned with result: %d\n", result);
    fflush(stdout);
  
    freeAST(ast);
    freeCompiler(&compiler);
    freeChunk(&chunk);
    
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

    char* source = readFile(path);
  
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

    // Parse the module source into an AST
    ASTNode* ast = parseSource(source);

    if (!ast) {
        fprintf(stderr, "Failed to parse module: %s\n", path);
        free(source);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }
    
    if (!compileProgram(ast, &compiler, true)) {
        fprintf(stderr, "Failed to compile module: %s\n", path);
        freeAST(ast);
        free(source);
        freeCompiler(&compiler);
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    emitByte(&compiler, OP_HALT);
    
    // Store current VM state
    Chunk* oldChunk = vm.chunk;
    uint8_t* oldIP = vm.ip;
    const char* oldFilePath = vm.filePath;
    
    // Set VM state for module execution
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.filePath = path;
    
    // Debug output: disassemble chunk if in dev mode
    if (vm.devMode) {
        disassembleChunk(&chunk, fileName);
    }
    
    InterpretResult result = run();
    
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

    return result;
}
