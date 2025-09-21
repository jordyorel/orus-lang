// register_vm.c - Register-based VM implementation
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

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
#include "vm/module_manager.h"
#include "runtime/builtins.h"
#include "public/common.h"
#include "compiler/parser.h"
#include "compiler/compiler.h"
#include "runtime/memory.h"
#include "runtime/builtins.h"
#include "tools/debug.h"
#include "internal/error_reporting.h"
#include "config/config.h"
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
static bool collect_module_imports(ASTNode* ast, char*** out_names, int* out_count);
static void free_module_imports(char** names, int count);
static bool load_module_list(const char* current_path, char** module_names, int module_count);

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
        case VAL_ENUM: {
            ObjEnumInstance* inst = AS_ENUM(value);
            const char* typeName = (inst && inst->typeName) ? inst->typeName->chars : "<enum>";
            const char* variantName = (inst && inst->variantName) ? inst->variantName->chars : "<variant>";
            printf("%s.%s", typeName, variantName);
            if (inst && inst->payload && inst->payload->length > 0) {
                printf("(");
                for (int i = 0; i < inst->payload->length; i++) {
                    if (i > 0) printf(", ");
                    printValue(inst->payload->elements[i]);
                }
                printf(")");
            }
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
        case VAL_ARRAY_ITERATOR: {
            ObjArrayIterator* it = AS_ARRAY_ITERATOR(value);
            int index = it ? it->index : 0;
            int remaining = 0;
            if (it && it->array) {
                remaining = it->array->length - index;
                if (remaining < 0) remaining = 0;
            }
            printf("array_iter(index=%d, remaining=%d)", index, remaining);
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
        case VAL_ENUM: {
            ObjEnumInstance* left = AS_ENUM(a);
            ObjEnumInstance* right = AS_ENUM(b);
            if (!left || !right) return false;
            if (left->typeName != right->typeName) return false;
            if (left->variantIndex != right->variantIndex) return false;

            int left_len = left->payload ? left->payload->length : 0;
            int right_len = right->payload ? right->payload->length : 0;
            if (left_len != right_len) return false;

            for (int i = 0; i < left_len; i++) {
                if (!valuesEqual(left->payload->elements[i], right->payload->elements[i])) {
                    return false;
                }
            }
            return true;
        }
        case VAL_RANGE_ITERATOR:
            return AS_RANGE_ITERATOR(a) == AS_RANGE_ITERATOR(b);
        case VAL_ARRAY_ITERATOR:
            return AS_ARRAY_ITERATOR(a) == AS_ARRAY_ITERATOR(b);
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
    // printf("[TYPE_SYSTEM_TRACE] initTypeSystem() starting\n");
    // fflush(stdout);
    init_extended_type_system();
    // printf("[TYPE_SYSTEM_TRACE] init_extended_type_system() completed\n");
    // fflush(stdout);
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
    // fprintf(stderr, "[RUNTIME_ERROR_TRACE] runtimeError called: type=%d, message='%s'\n", type, buffer);
    // fprintf(stderr, "[RUNTIME_ERROR_TRACE] Location: file=%s, line=%d, column=%d\n", 
    //         location.file ? location.file : "NULL", location.line, location.column);
    // fflush(stderr);

    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
        location.line = vm.currentLine;
        location.column = vm.currentColumn;
    }

    if (vm.chunk && vm.ip) {
        size_t offset = (size_t)(vm.ip - vm.chunk->code);
        if (offset > 0) {
            offset--;
        }
        if (offset < (size_t)vm.chunk->count) {
            if ((location.line <= 0 || location.line == -1) && vm.chunk->lines) {
                location.line = vm.chunk->lines[offset];
            }
            if ((location.column <= 0 || location.column == -1) && vm.chunk->columns) {
                location.column = vm.chunk->columns[offset];
            }
        }
    }

    // Use enhanced error reporting
    ErrorCode code = map_error_type_to_code(type);
    report_runtime_error(code, location, "%s", buffer);

    ObjError* err = allocateError(type, buffer, location);
    vm.lastError = ERROR_VAL(err);
}

void vm_unwind_to_stack_depth(int targetDepth) {
    while (vm.frameCount > targetDepth) {
        CallFrame* frame = &vm.frames[--vm.frameCount];

        closeUpvalues(&vm.registers[frame->parameterBaseRegister]);

        if (frame->savedRegisterCount == 31) {
            for (int i = 0; i < 15; i++) {
                vm_set_register_safe(65 + i, frame->savedRegisters[i]);
            }
            for (int i = 0; i < 16; i++) {
                vm_set_register_safe(240 + i, frame->savedRegisters[15 + i]);
            }
        } else {
            for (int i = 0; i < frame->savedRegisterCount; i++) {
                vm_set_register_safe(frame->savedRegisterStart + i, frame->savedRegisters[i]);
            }
        }

        vm.chunk = frame->previousChunk;
        vm.ip = frame->returnAddress;
    }
}

// Debug operations
// Main execution engine
static InterpretResult run(void) {
    return vm_run_dispatch();
}
// Main interpretation functions
InterpretResult interpret(const char* source) {
    InterpretResult result = INTERPRET_COMPILE_ERROR;
    if (vm.config.trace_typed_fallbacks) {
        vm_reset_loop_trace();
    }
    // Source text is now set in main.c with proper error handling
    // set_source_text(source, strlen(source)) is called before interpret()
    // fflush(stdout);
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);

    Compiler compiler;
    initCompiler(&compiler, &chunk, "<repl>", source);

    // Parse the source into an AST
    ASTNode* ast = parseSource(source);
    // fflush(stdout);

    if (!ast) {
        goto cleanup;
    }

    // fflush(stdout);

    // Check if typed AST visualization is enabled
    // fflush(stdout);
    
    const OrusConfig* config = config_get_global();
    // fflush(stdout);
    
    // Disable verbose parser debug output from this point forward to clean up typed AST output
    if (config && config->show_typed_ast) {
        extern void set_parser_debug(bool enabled);
        set_parser_debug(false);  // Disable parser debug for cleaner typed AST output
    }
    
    const char* current_path = vm.filePath ? vm.filePath : ".";
    char** import_names = NULL;
    int import_count = 0;
    if (!collect_module_imports(ast, &import_names, &import_count)) {
        goto cleanup;
    }

    if (import_count > 0) {
        freeAST(ast);
        ast = NULL;
        if (!load_module_list(current_path, import_names, import_count)) {
            free_module_imports(import_names, import_count);
            goto cleanup;
        }
        free_module_imports(import_names, import_count);
        import_names = NULL;

        ast = parseSource(source);
        if (!ast) {
            goto cleanup;
        }
    } else {
        free_module_imports(import_names, import_count);
        import_names = NULL;
    }

    // Compile the AST to bytecode using the unified multi-pass pipeline
    bool compilation_result = compileProgram(ast, &compiler, false);

    if (!compilation_result) {
        printf("[ERROR] interpret: Compilation failed\n");
        goto cleanup;
    }

    if (config && config->show_bytecode) {
        printf("\n=== BYTECODE DUMP ===\n");
        printf("Instructions: %d\n", chunk.count);
        printf("Constants: %d\n", chunk.constants.count);
        for (int c = 0; c < chunk.constants.count; c++) {
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
                        uint16_t constantIndex = (chunk.code[i + 2] << 8) | chunk.code[i + 3];
                        printf(" reg=%d, constantIndex=%d", chunk.code[i + 1], constantIndex);
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
                               chunk.code[i + 1], chunk.code[i + 2], chunk.code[i + 3]);
                        i += 3;
                    }
                    break;
                case OP_PRINT_R:
                    printf(" (OP_PRINT_R)");
                    if (i + 1 < chunk.count) {
                        printf(" reg=%d", chunk.code[i + 1]);
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
    }

    // Execute the chunk
    vm.chunk = &chunk;
    vm.ip = chunk.code;
    vm.frameCount = 0;
    
    fflush(stdout);

    // Debug output: disassemble chunk if in dev mode
    if (vm.devMode) {
        disassembleChunk(&chunk, "main");
    }
    
    fflush(stdout);
    result = run();
    fflush(stdout);
  
cleanup:
    if (ast) {
        freeAST(ast);
    }
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

static bool isModuleLoading(const char* path) {
    for (int i = 0; i < vm.loadingModuleCount; i++) {
        if (vm.loadingModules[i] && strcmp(vm.loadingModules[i]->chars, path) == 0) {
            return true;
        }
    }
    return false;
}

static void pushLoadingModule(const char* path) {
    if (!path) {
        return;
    }
    if (vm.loadingModuleCount < UINT8_COUNT) {
        vm.loadingModules[vm.loadingModuleCount] = allocateString(path, strlen(path));
        vm.loadingModuleCount++;
    }
}

static void popLoadingModule(const char* path) {
    if (!path || vm.loadingModuleCount == 0) {
        return;
    }
    for (int i = 0; i < vm.loadingModuleCount; i++) {
        if (vm.loadingModules[i] && strcmp(vm.loadingModules[i]->chars, path) == 0) {
            vm.loadingModuleCount--;
            vm.loadingModules[i] = vm.loadingModules[vm.loadingModuleCount];
            vm.loadingModules[vm.loadingModuleCount] = NULL;
            return;
        }
    }
}

static char* build_module_path(const char* base_path, const char* module_name) {
    if (!module_name) {
        return NULL;
    }

    size_t module_len = strlen(module_name);
    const char* forward = strrchr(base_path, '/');
    const char* backward = strrchr(base_path, '\\');
    const char* sep = forward;
    if (backward && (!sep || backward > sep)) {
        sep = backward;
    }

    size_t dir_len = sep ? (size_t)(sep - base_path + 1) : 0;
    const char* suffix = ".orus";
    size_t suffix_len = strlen(suffix);
    bool has_extension = module_len >= suffix_len && strcmp(module_name + module_len - suffix_len, suffix) == 0;
    size_t total = dir_len + module_len + (has_extension ? 1 : suffix_len + 1);
    char* result = (char*)malloc(total);
    if (!result) {
        return NULL;
    }

    if (dir_len > 0) {
        memcpy(result, base_path, dir_len);
    }
    size_t base_len = has_extension ? module_len - suffix_len : module_len;
    for (size_t i = 0; i < base_len; i++) {
        char ch = module_name[i];
        if (ch == '.') {
            result[dir_len + i] = '/';
        } else {
            result[dir_len + i] = ch;
        }
    }

    if (has_extension) {
        memcpy(result + dir_len + base_len, module_name + base_len, suffix_len + 1);
    } else {
        memcpy(result + dir_len + base_len, suffix, suffix_len + 1);
    }
    return result;
}

static void free_module_imports(char** names, int count) {
    if (!names) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

static bool collect_module_imports(ASTNode* ast, char*** out_names, int* out_count) {
    if (!out_names || !out_count) {
        return false;
    }

    *out_names = NULL;
    *out_count = 0;

    if (!ast || ast->type != NODE_PROGRAM) {
        return true;
    }

    char** names = NULL;
    int count = 0;

    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* decl = ast->program.declarations[i];
        if (!decl || decl->type != NODE_IMPORT || !decl->import.moduleName) {
            continue;
        }

        char* copy = strdup(decl->import.moduleName);
        if (!copy) {
            free_module_imports(names, count);
            return false;
        }

        char** resized = (char**)realloc(names, sizeof(char*) * (size_t)(count + 1));
        if (!resized) {
            free(copy);
            free_module_imports(names, count);
            return false;
        }

        names = resized;
        names[count++] = copy;
    }

    *out_names = names;
    *out_count = count;
    return true;
}

static bool load_module_list(const char* current_path, char** module_names, int module_count) {
    if (!module_names || module_count == 0) {
        return true;
    }

    for (int i = 0; i < module_count; i++) {
        char* dep_path = build_module_path(current_path, module_names[i]);
        if (!dep_path) {
            return false;
        }

        InterpretResult result = interpret_module(dep_path);
        free(dep_path);
        if (result != INTERPRET_OK) {
            return false;
        }
    }

    return true;
}

InterpretResult interpret_module(const char* path) {
    InterpretResult result = INTERPRET_COMPILE_ERROR;
    bool pushed = false;
    bool chunk_initialized = false;
    bool compiler_initialized = false;
    Chunk chunk;
    Compiler compiler;
    ASTNode* ast = NULL;
    char* source = NULL;
    char* module_name = NULL;
    RegisterModule* module_entry = NULL;

    if (!path) {
        fprintf(stderr, "Module path cannot be null.\n");
        return result;
    }

    if (isModuleLoaded(path)) {
        return INTERPRET_OK;
    }

    if (isModuleLoading(path)) {
        fprintf(stderr, "Cyclic module dependency detected while processing use: %s\n", path);
        return result;
    }

    pushLoadingModule(path);
    pushed = true;

    source = readFile(path);

    if (!source) {
        goto cleanup;
    }

    // Create a chunk for the compiled bytecode
    initChunk(&chunk);
    chunk_initialized = true;
    
    // Extract module name from path (filename without extension)
    const char* fileName = strrchr(path, '/');
    if (!fileName) fileName = strrchr(path, '\\'); // Windows path separator
    if (!fileName) fileName = path;
    else fileName++; // Skip the separator

    module_name = NULL;
    if (fileName) {
        size_t name_len = strlen(fileName);
        module_name = (char*)malloc(name_len + 1);
        if (module_name) {
            memcpy(module_name, fileName, name_len + 1);
            char* dot = strrchr(module_name, '.');
            if (dot) {
                *dot = '\0';
            }
        }
    }

    // Create compiler for the module
    initCompiler(&compiler, &chunk, fileName, source);
    compiler_initialized = true;

    // Parse the module source into an AST
    ast = parseSource(source);

    if (!ast) {
        fprintf(stderr, "Failed to parse module: %s\n", path);
        goto cleanup;
    }

    char** module_imports = NULL;
    int module_import_count = 0;
    if (!collect_module_imports(ast, &module_imports, &module_import_count)) {
        fprintf(stderr, "Failed to gather module uses for: %s\n", path);
        goto cleanup;
    }

    if (module_import_count > 0) {
        freeAST(ast);
        ast = NULL;
        if (!load_module_list(path, module_imports, module_import_count)) {
            free_module_imports(module_imports, module_import_count);
            fprintf(stderr, "Failed to preload dependencies for module: %s\n", path);
            goto cleanup;
        }
        free_module_imports(module_imports, module_import_count);
        module_imports = NULL;

        ast = parseSource(source);
        if (!ast) {
            fprintf(stderr, "Failed to parse module: %s\n", path);
            goto cleanup;
        }
    } else {
        free_module_imports(module_imports, module_import_count);
        module_imports = NULL;
    }

    if (ast && ast->type == NODE_PROGRAM && ast->program.moduleName) {
        free(module_name);
        module_name = strdup(ast->program.moduleName);
        if (!module_name) {
            fprintf(stderr, "Failed to allocate module name for: %s\n", path);
            goto cleanup;
        }
    }

    if (!compileProgram(ast, &compiler, true)) {
        fprintf(stderr, "Failed to compile module: %s\n", path);
        goto cleanup;
    }

    if (vm.register_file.module_manager && module_name) {
        module_entry = load_module(vm.register_file.module_manager, module_name);
    }

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
    
    result = run();
    
    // Restore VM state
    vm.chunk = oldChunk;
    vm.ip = oldIP;
    vm.filePath = oldFilePath;
    
    // Add module to loaded modules list if successful
    if (result == INTERPRET_OK) {
        addLoadedModule(path);
        if (compiler.isModule && module_entry) {
            for (int i = 0; i < compiler.exportCount; i++) {
                ModuleExportEntry* entry = &compiler.exports[i];
                if (!entry->name) {
                    continue;
                }

                Type* exported_type = entry->type;
                bool registered = register_module_export(module_entry, entry->name, entry->kind,
                                                        entry->register_index, exported_type);
                if (!registered && exported_type) {
                    module_free_export_type(exported_type);
                    exported_type = NULL;
                }
                entry->type = NULL;

                if (entry->kind == MODULE_EXPORT_KIND_GLOBAL &&
                    entry->register_index >= 0 && entry->register_index < UINT8_COUNT) {
                    vm.publicGlobals[entry->register_index] = true;
                    if (entry->register_index >= vm.variableCount) {
                        vm.variableCount = entry->register_index + 1;
                    }
                    if (vm.globalTypes[entry->register_index] == NULL) {
                        vm.globalTypes[entry->register_index] = exported_type ? exported_type : getPrimitiveType(TYPE_ANY);
                    } else if (exported_type) {
                        vm.globalTypes[entry->register_index] = exported_type;
                    }
                }
            }

            if (compiler.importCount > 0 && vm.register_file.module_manager) {
                for (int i = 0; i < compiler.importCount; i++) {
                    ModuleImportEntry* entry = &compiler.imports[i];
                    if (!entry->module_name || !entry->symbol_name) {
                        continue;
                    }
                    RegisterModule* source_module = find_module(vm.register_file.module_manager, entry->module_name);
                    if (!source_module) {
                        continue;
                    }
                    import_variable(module_entry, entry->symbol_name, source_module);
                }
            }
        }
    } else {
        fprintf(stderr, "Runtime error in module: %s\n", path);
    }

cleanup:
    if (ast) {
        freeAST(ast);
    }
    free(source);
    if (compiler_initialized) {
        freeCompiler(&compiler);
    }
    if (chunk_initialized) {
        freeChunk(&chunk);
    }

    free(module_name);

    if (pushed) {
        popLoadingModule(path);
    }

    return result;
}

void vm_reset_loop_trace(void) {
    memset(&vm.profile.loop_trace, 0, sizeof(LoopTraceCounters));
}

void vm_dump_loop_trace(FILE* out) {
    if (!out || !vm.config.trace_typed_fallbacks) {
        return;
    }

    fprintf(out,
            "[loop-trace] typed_hit=%" PRIu64 " typed_miss=%" PRIu64
            " boxed_type_mismatch=%" PRIu64 " boxed_overflow_guard=%" PRIu64 "\n",
            (uint64_t)vm.profile.loop_trace.typed_hit,
            (uint64_t)vm.profile.loop_trace.typed_miss,
            (uint64_t)vm.profile.loop_trace.boxed_type_mismatch,
            (uint64_t)vm.profile.loop_trace.boxed_overflow_guard);
}
