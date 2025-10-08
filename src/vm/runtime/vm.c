// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/vm.c
// Author: Jordy Orel KONDA
// Copyright (c) 2022 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Initializes and drives the public VM runtime interface exposed to clients.


// register_vm.c - Register-based VM implementation
#define _POSIX_C_SOURCE 200809L
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// Auto-detect computed goto support
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
#include "runtime/core_fs_handles.h"
#include "runtime/builtins.h"
#include "tools/debug.h"
#include "internal/error_reporting.h"
#include "vm/vm_string_ops.h"
#include "config/config.h"
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#endif

#ifndef _WIN32
extern char* realpath(const char* path, char* resolved_path);
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

extern VM vm;

#if USE_COMPUTED_GOTO
void* vm_dispatch_table[OP_HALT + 1] = {0};
#endif

static bool vm_error_report_pending = false;

static const char* REPL_MODULE_NAME = "__repl__";

void vm_set_error_report_pending(bool pending) {
    vm_error_report_pending = pending;
}

bool vm_get_error_report_pending(void) {
    return vm_error_report_pending;
}

static int vm_bind_core_intrinsic(const char* symbol, const IntrinsicSignatureInfo* signature) {
    if (!symbol || !signature) {
        return -1;
    }

    size_t symbol_len = strlen(symbol);

    for (int i = 0; i < vm.nativeFunctionCount; ++i) {
        NativeFunction* existing = &vm.nativeFunctions[i];
        if (existing->name && existing->name->length == (int)symbol_len &&
            strncmp(existing->name->chars, symbol, symbol_len) == 0) {
            return i;
        }
    }

    if (vm.nativeFunctionCount >= MAX_NATIVES) {
        return -1;
    }

    NativeFn fn = vm_lookup_core_intrinsic(symbol);
    if (!fn) {
        return -1;
    }

    NativeFunction* slot = &vm.nativeFunctions[vm.nativeFunctionCount];
    memset(slot, 0, sizeof(*slot));
    slot->function = fn;
    slot->arity = signature->paramCount;
    slot->returnType = getPrimitiveType(signature->returnType);
    slot->name = allocateString(symbol, (int)symbol_len);
    if (!slot->name) {
        slot->function = NULL;
        slot->arity = 0;
        slot->returnType = NULL;
        return -1;
    }

    int bound_index = vm.nativeFunctionCount;
    vm.nativeFunctionCount++;
    return bound_index;
}

static void vm_patch_intrinsic_stub(int function_index, int native_index) {
    if (function_index < 0 || function_index >= vm.functionCount) {
        return;
    }
    if (native_index < 0 || native_index >= MAX_NATIVES) {
        return;
    }

    Function* fn = &vm.functions[function_index];
    if (!fn->chunk || !fn->chunk->code) {
        return;
    }

    int start = fn->start;
    if (start < 0 || start + 1 >= fn->chunk->count) {
        return;
    }
    if (fn->chunk->code[start] != OP_CALL_NATIVE_R) {
        return;
    }
    fn->chunk->code[start + 1] = (uint8_t)native_index;
}

// Forward declarations
static InterpretResult run(void);
void runtimeError(ErrorType type, SrcLocation location,
                   const char* format, ...);
typedef struct ModuleImportInfo {
    char* name;
    int line;
    int column;
} ModuleImportInfo;

static bool collect_module_imports(ASTNode* ast, ModuleImportInfo** out_imports, int* out_count);
static void free_module_imports(ModuleImportInfo* imports, int count);
static bool load_module_list(const char* current_path, ModuleImportInfo* imports, int module_count);
static bool has_orus_suffix(const char* text, size_t length);
static char* infer_module_name_from_path(const char* path);
static char* build_module_path(const char* base_path, const char* module_name);

typedef struct ModuleCacheEntry {
    char* key;
    char* resolved_path;
} ModuleCacheEntry;

typedef enum ModuleRootKind {
    MODULE_ROOT_DIRECT,
    MODULE_ROOT_CALLER,
    MODULE_ROOT_STD,
    MODULE_ROOT_ENV,
} ModuleRootKind;

typedef struct ModuleSearchRoot {
    char* path;
    ModuleRootKind kind;
} ModuleSearchRoot;

static ModuleCacheEntry* module_cache_entries = NULL;
static size_t module_cache_count = 0;
static size_t module_cache_capacity = 0;

static char* cached_executable_dir = NULL;


// Value operations
void print_raw_f64(double value) {
    if (isnan(value)) {
        fputs("nan", stdout);
        return;
    }
    if (isinf(value)) {
        fputs(value < 0 ? "-inf" : "inf", stdout);
        return;
    }
    if (value == 0.0) {
        fputs("0", stdout);
        return;
    }

    char buffer[512];
    const double abs_value = fabs(value);
    const char* format = "%.17f";
    if (abs_value > 0.0 && abs_value < 1e-4) {
        format = "%.17g";
    }

    snprintf(buffer, sizeof(buffer), format, value);

    char* exponent = strchr(buffer, 'e');
    if (!exponent) {
        exponent = strchr(buffer, 'E');
    }

    char exponent_part[64] = {0};
    if (exponent) {
        strncpy(exponent_part, exponent, sizeof(exponent_part) - 1);
        *exponent = '\0';
    }

    char* decimal = strchr(buffer, '.');
    if (decimal != NULL) {
        char* end = buffer + strlen(buffer) - 1;
        while (end > decimal && *end == '0') {
            *end-- = '\0';
        }
        if (*end == '.') {
            *end = '\0';
        }
    }

    if (exponent_part[0] != '\0') {
        strncat(buffer, exponent_part, sizeof(buffer) - strlen(buffer) - 1);
    }

    fputs(buffer, stdout);
}

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
            print_raw_f64(AS_F64(value));
            break;
        case VAL_STRING: {
            const char* chars = string_get_chars(AS_STRING(value));
            printf("%s", chars ? chars : "");
            break;
        }
        case VAL_BYTES: {
            ObjByteBuffer* buffer = AS_BYTES(value);
            size_t length = buffer ? buffer->length : 0;
            printf("<bytes len=%zu>", length);
            break;
        }
        case VAL_ARRAY: {
            ObjArray* array = AS_ARRAY(value);
            for (int i = 0; i < array->length; i++) {
                if (i > 0) printf(", ");
                printValue(array->elements[i]);
            }
            break;
        }
        case VAL_ENUM: {
            ObjEnumInstance* inst = AS_ENUM(value);
            const char* typeName = inst && inst->typeName ? string_get_chars(inst->typeName) : NULL;
            if (!typeName) typeName = "<enum>";
            const char* variantName = inst && inst->variantName ? string_get_chars(inst->variantName) : NULL;
            if (!variantName) variantName = "<variant>";
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
        case VAL_ERROR: {
            ObjError* err_obj = AS_ERROR(value);
            const char* msg = err_obj && err_obj->message ? string_get_chars(err_obj->message) : NULL;
            printf("Error: %s", msg ? msg : "");
            break;
        }
        case VAL_RANGE_ITERATOR: {
            ObjRangeIterator* it = AS_RANGE_ITERATOR(value);
            long long step = it ? (long long)it->step : 1;
            if (!it || step == 1) {
                printf("range(%lld..%lld)",
                       it ? (long long)it->current : 0,
                       it ? (long long)it->end : 0);
            } else {
                printf("range(%lld..%lld step=%lld)",
                       (long long)it->current,
                       (long long)it->end,
                       step);
            }
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
        case VAL_FILE: {
            ObjFile* file = AS_FILE(value);
            if (!file) {
                printf("file(<null>)");
                break;
            }
            const char* path = file->path ? string_get_chars(file->path) : NULL;
            const char* state = file->isClosed ? "closed" : (file->ownsHandle ? "owned" : "borrowed");
            printf("file(");
            bool printed_field = false;
            if (path && *path) {
                printf("path=\"%s\"", path);
                printed_field = true;
            }
            if (printed_field) {
                printf(", ");
            }
            printf("handle=%p, %s)", (void*)file->handle, state);
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
        case VAL_STRING: {
            ObjString* left = AS_STRING(a);
            ObjString* right = AS_STRING(b);
            if (left == right) return true;
            if (!left || !right) return false;
            if (left->length != right->length) return false;
            const char* left_chars = string_get_chars(left);
            const char* right_chars = string_get_chars(right);
            if (!left_chars || !right_chars) {
                return left_chars == right_chars;
            }
            return memcmp(left_chars, right_chars, (size_t)left->length) == 0;
        }
        case VAL_ARRAY:
            return AS_ARRAY(a) == AS_ARRAY(b);
        case VAL_BYTES: {
            ObjByteBuffer* left = AS_BYTES(a);
            ObjByteBuffer* right = AS_BYTES(b);
            if (left == right) return true;
            if (!left || !right) return false;
            if (left->length != right->length) return false;
            if (left->length == 0) return true;
            if (!left->data || !right->data) {
                return left->data == right->data;
            }
            return memcmp(left->data, right->data, left->length) == 0;
        }
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
        case VAL_FILE:
            return AS_FILE(a) == AS_FILE(b);
        case VAL_ERROR:
            return AS_ERROR(a) == AS_ERROR(b);
        default:
            return false;
    }
}

// Forward declarations for type system functions
extern void init_extended_type_system(void);
extern Type* get_primitive_type_cached(TypeKind kind);

void initTypeSystem(void) {
    init_extended_type_system();
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

    bool has_catch_handler = false;
    for (int i = vm.tryFrameCount - 1; i >= 0; --i) {
        if (vm.tryFrames[i].catchRegister != TRY_CATCH_REGISTER_NONE) {
            has_catch_handler = true;
            break;
        }
    }

    ObjError* err = allocateError(type, buffer, location);
    if (!err) {
        ErrorCode code = map_error_details_to_code(type, buffer);
        report_runtime_error(code, location, "%s", buffer);
        vm.lastError = BOOL_VAL(false);
        vm_set_error_report_pending(false);
        return;
    }

    vm.lastError = ERROR_VAL(err);

    if (has_catch_handler) {
        vm_set_error_report_pending(true);
    } else {
        ErrorCode code = map_error_details_to_code(type, buffer);
        report_runtime_error(code, location, "%s", buffer);
        vm_set_error_report_pending(false);
    }
}

void vm_report_unhandled_error(void) {
    if (!vm_get_error_report_pending()) {
        return;
    }

    if (!IS_ERROR(vm.lastError)) {
        vm_set_error_report_pending(false);
        return;
    }

    ObjError* err = AS_ERROR(vm.lastError);
    if (!err) {
        vm_set_error_report_pending(false);
        return;
    }

    const char* message = err->message ? string_get_chars(err->message) : NULL;
    if (!message) {
        message = "";
    }
    SrcLocation loc = {
        .file = err->location.file,
        .line = err->location.line,
        .column = err->location.column,
    };
    ErrorCode code = map_error_details_to_code(err->type, message);
    report_runtime_error(code, loc, "%s", message);
    vm_set_error_report_pending(false);
}

void vm_unwind_to_stack_depth(int targetDepth) {
    while (vm.frameCount > targetDepth) {
        CallFrame* frame = &vm.frames[--vm.frameCount];

        CallFrame* window = vm.register_file.current_frame;
        Value* param_base_ptr = NULL;
        if (window) {
            vm_get_register_safe(frame->parameterBaseRegister);
            param_base_ptr = get_register(&vm.register_file, frame->parameterBaseRegister);
        }
        if (!param_base_ptr) {
            param_base_ptr = &vm.registers[frame->parameterBaseRegister];
        }
        closeUpvalues(param_base_ptr);

        deallocate_frame(&vm.register_file);

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
    // Source text is now set in main.c with proper error handling
    // set_source_text(source, strlen(source)) is called before interpret()
    // fflush(stdout);
    // Create a chunk for the compiled bytecode
    Chunk chunk;
    initChunk(&chunk);

    Compiler compiler;
    initCompiler(&compiler, &chunk, "<repl>", source);

    char* module_name = infer_module_name_from_path(vm.filePath);
    ASTNode* ast = NULL;
    bool is_repl_session = vm.filePath && strcmp(vm.filePath, "<repl>") == 0;
    if (is_repl_session && !module_name) {
        module_name = strdup(REPL_MODULE_NAME);
        if (!module_name) {
            goto cleanup;
        }
    }

    RegisterModule* repl_module = NULL;

    // Parse the source into an AST
    ast = parseSourceWithModuleName(source, module_name);
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
    ModuleImportInfo* import_infos = NULL;
    int import_count = 0;
    if (!collect_module_imports(ast, &import_infos, &import_count)) {
        goto cleanup;
    }

    if (import_count > 0) {
        if (ast) {
            freeAST(ast);
            ast = NULL;
        }
        if (!load_module_list(current_path, import_infos, import_count)) {
            free_module_imports(import_infos, import_count);
            goto cleanup;
        }
        free_module_imports(import_infos, import_count);
        import_infos = NULL;

        ast = parseSourceWithModuleName(source, module_name);
        if (!ast) {
            goto cleanup;
        }
    } else {
        free_module_imports(import_infos, import_count);
        import_infos = NULL;
    }

    // Compile the AST to bytecode using the unified multi-pass pipeline
    bool compilation_result = compileProgram(ast, &compiler, is_repl_session);

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

    if (is_repl_session && vm.register_file.module_manager && module_name) {
        repl_module = load_module(vm.register_file.module_manager, module_name);
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

    if (is_repl_session && result == INTERPRET_OK && compiler.isModule &&
        vm.register_file.module_manager) {
        ModuleManager* manager = vm.register_file.module_manager;
        if (!repl_module && module_name) {
            repl_module = load_module(manager, module_name);
        }
        if (repl_module) {
            for (int i = 0; i < compiler.exportCount; i++) {
                ModuleExportEntry* entry = &compiler.exports[i];
                if (!entry->name) {
                    continue;
                }

                Type* exported_type = entry->type;
                bool should_register = !entry->is_internal_intrinsic;
                bool registered = true;
                if (should_register) {
                    registered = register_module_export(repl_module, entry->name, entry->kind,
                                                        entry->register_index, exported_type,
                                                        entry->intrinsic_symbol);
                } else if (exported_type) {
                    module_free_export_type(exported_type);
                    exported_type = NULL;
                }
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

            if (compiler.importCount > 0) {
                for (int i = 0; i < compiler.importCount; i++) {
                    ModuleImportEntry* entry = &compiler.imports[i];
                    if (!entry->module_name || !entry->symbol_name) {
                        continue;
                    }
                    RegisterModule* source_module = find_module(manager, entry->module_name);
                    if (!source_module) {
                        continue;
                    }
                    import_variable(repl_module, entry->symbol_name, source_module);
                }
            }
        }
    }
  
cleanup:
    if (ast) {
        freeAST(ast);
    }
    free(module_name);
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
        if (vm.loadedModules[i] && strcmp(string_get_chars(vm.loadedModules[i]), path) == 0) {
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
        if (vm.loadingModules[i] && strcmp(string_get_chars(vm.loadingModules[i]), path) == 0) {
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
        if (vm.loadingModules[i] && strcmp(string_get_chars(vm.loadingModules[i]), path) == 0) {
            vm.loadingModuleCount--;
            vm.loadingModules[i] = vm.loadingModules[vm.loadingModuleCount];
            vm.loadingModules[vm.loadingModuleCount] = NULL;
            return;
        }
    }
}

static bool has_orus_suffix(const char* text, size_t length) {
    const char* suffix = ".orus";
    size_t suffix_len = 5;  // strlen(".orus")
    if (!text || length < suffix_len) {
        return false;
    }

    const char* start = text + length - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        if (tolower((unsigned char)start[i]) != suffix[i]) {
            return false;
        }
    }
    return true;
}

static char* infer_module_name_from_path(const char* path) {
    if (!path) {
        return NULL;
    }

    const char* start = path;

    // Skip leading drive letters like C:/
    if (isalpha((unsigned char)start[0]) && start[1] == ':' &&
        (start[2] == '\\' || start[2] == '/')) {
        start += 3;
    }

    // Skip leading ./ or .\ segments
    while (start[0] == '.' && (start[1] == '/' || start[1] == '\\')) {
        start += 2;
    }

    // Skip remaining leading separators
    while (*start == '/' || *start == '\\') {
        start++;
    }

    if (*start == '\0') {
        return NULL;
    }

    size_t length = strlen(start);
    if (!has_orus_suffix(start, length)) {
        return NULL;
    }

    size_t base_length = length - 5;  // Remove ".orus"

    if (base_length == 0) {
        return NULL;
    }

    char* result = (char*)malloc(base_length + 1);
    if (!result) {
        return NULL;
    }

    size_t out_index = 0;
    for (size_t i = 0; i < base_length; ++i) {
        char ch = start[i];
        if (ch == '/' || ch == '\\') {
            if (out_index == 0 || result[out_index - 1] == '.') {
                continue;  // Avoid leading or duplicate separators
            }
            result[out_index++] = '.';
        } else {
            result[out_index++] = ch;
        }
    }

    while (out_index > 0 && result[out_index - 1] == '.') {
        out_index--;
    }

    if (out_index == 0) {
        free(result);
        return NULL;
    }

    result[out_index] = '\0';
    return result;
}

static bool is_absolute_path(const char* path) {
    if (!path || !*path) {
        return false;
    }

#ifdef _WIN32
    if ((isalpha((unsigned char)path[0]) && path[1] == ':' &&
         (path[2] == '\\' || path[2] == '/')) ||
        (path[0] == '\\' && path[1] == '\\')) {
        return true;
    }
#else
    if (path[0] == '/') {
        return true;
    }
#endif

    return false;
}

static char* make_absolute_path(const char* path) {
    if (!path) {
        return NULL;
    }

#ifdef _WIN32
    char buffer[MAX_PATH];
    if (_fullpath(buffer, path, MAX_PATH)) {
        return strdup(buffer);
    }

    if (is_absolute_path(path)) {
        return strdup(path);
    }

    if (_getcwd(buffer, MAX_PATH)) {
        size_t base_len = strlen(buffer);
        size_t path_len = strlen(path);
        size_t needs_sep = (base_len > 0 && buffer[base_len - 1] != '/' && buffer[base_len - 1] != '\\') ? 1 : 0;
        size_t total_len = base_len + needs_sep + path_len + 1;
        char* combined = (char*)malloc(total_len);
        if (!combined) {
            return NULL;
        }
        memcpy(combined, buffer, base_len);
        size_t offset = base_len;
        if (needs_sep) {
            combined[offset++] = '/';
        }
        memcpy(combined + offset, path, path_len + 1);
        return combined;
    }

    return strdup(path);
#else
    char* resolved = realpath(path, NULL);
    if (resolved) {
        return resolved;
    }

    if (is_absolute_path(path)) {
        return strdup(path);
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) {
        return strdup(path);
    }

    size_t base_len = strlen(cwd);
    size_t path_len = strlen(path);
    size_t needs_sep = (base_len > 0 && cwd[base_len - 1] != '/') ? 1 : 0;
    size_t total_len = base_len + needs_sep + path_len + 1;
    char* combined = (char*)malloc(total_len);
    if (!combined) {
        return NULL;
    }

    memcpy(combined, cwd, base_len);
    size_t offset = base_len;
    if (needs_sep) {
        combined[offset++] = '/';
    }
    memcpy(combined + offset, path, path_len + 1);
    return combined;
#endif
}

static char* copy_dirname(const char* path) {
    if (!path || !*path) {
        return NULL;
    }

    char* absolute = make_absolute_path(path);
    if (!absolute) {
        return NULL;
    }

    struct stat st;
    if (stat(absolute, &st) == 0 && S_ISDIR(st.st_mode)) {
        size_t len = strlen(absolute);
        while (len > 1 && (absolute[len - 1] == '/' || absolute[len - 1] == '\\')) {
            absolute[len - 1] = '\0';
            len--;
        }
        return absolute;
    }

    size_t len = strlen(absolute);
    while (len > 0 && (absolute[len - 1] == '/' || absolute[len - 1] == '\\')) {
        absolute[len - 1] = '\0';
        len--;
    }

    char* last_sep = NULL;
    for (char* cursor = absolute; *cursor; ++cursor) {
        if (*cursor == '/' || *cursor == '\\') {
            last_sep = cursor;
        }
    }

    if (!last_sep) {
        free(absolute);
        return make_absolute_path(".");
    }

#ifdef _WIN32
    if (last_sep == absolute && absolute[1] == '\0') {
        return absolute;
    }
    if (last_sep > absolute && last_sep[-1] == ':') {
        last_sep[1] = '\0';
        return absolute;
    }
#else
    if (last_sep == absolute) {
        last_sep[1] = '\0';
        return absolute;
    }
#endif

    *last_sep = '\0';
    return absolute;
}

static char* join_paths(const char* base, const char* relative) {
    if (!relative) {
        return NULL;
    }

    if (!base || !*base) {
        return strdup(relative);
    }

    if (is_absolute_path(relative)) {
        return strdup(relative);
    }

    size_t base_len = strlen(base);
    size_t relative_len = strlen(relative);
    bool base_has_sep = base_len > 0 && (base[base_len - 1] == '/' || base[base_len - 1] == '\\');
    size_t relative_start = (relative_len > 0 && (relative[0] == '/' || relative[0] == '\\')) ? 1 : 0;
    if (relative_len <= relative_start) {
        return strdup(base);
    }

    size_t total_len = base_len + (base_has_sep || relative_start ? 0 : 1) + (relative_len - relative_start) + 1;
    char* result = (char*)malloc(total_len);
    if (!result) {
        return NULL;
    }

    memcpy(result, base, base_len);
    size_t offset = base_len;
    if (!base_has_sep && !relative_start) {
        result[offset++] = '/';
    }
    memcpy(result + offset, relative + relative_start, relative_len - relative_start + 1);
    return result;
}

static bool path_exists(const char* path) {
    if (!path) {
        return false;
    }

    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool directory_exists(const char* path) {
    if (!path) {
        return false;
    }

    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool path_has_suffix_component(const char* path, const char* component) {
    if (!path || !component) {
        return false;
    }

    size_t path_len = strlen(path);
    size_t component_len = strlen(component);
    if (component_len == 0 || path_len < component_len) {
        return false;
    }

    const char* start = path + path_len - component_len;
#ifdef _WIN32
    for (size_t i = 0; i < component_len; ++i) {
        if (tolower((unsigned char)start[i]) != tolower((unsigned char)component[i])) {
            return false;
        }
    }
#else
    if (strncmp(start, component, component_len) != 0) {
        return false;
    }
#endif

    if (path_len == component_len) {
        return true;
    }

    char separator = start[-1];
    return separator == '/' || separator == '\\';
}

static bool append_search_root(ModuleSearchRoot** roots, size_t* count, size_t* capacity,
                               const char* path, ModuleRootKind kind) {
    if (!path || !*path) {
        return true;
    }

    for (size_t i = 0; i < *count; ++i) {
        if ((*roots)[i].path && strcmp((*roots)[i].path, path) == 0) {
            return true;
        }
    }

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        ModuleSearchRoot* resized = (ModuleSearchRoot*)realloc(*roots, sizeof(ModuleSearchRoot) * new_capacity);
        if (!resized) {
            return false;
        }
        *roots = resized;
        *capacity = new_capacity;
    }

    char* copy = strdup(path);
    if (!copy) {
        return false;
    }

    (*roots)[*count].path = copy;
    (*roots)[*count].kind = kind;
    (*count)++;
    return true;
}

static void free_search_roots(ModuleSearchRoot* roots, size_t count) {
    if (!roots) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        free(roots[i].path);
        roots[i].path = NULL;
    }
    free(roots);
}

static char* trim_whitespace_in_place(char* text) {
    if (!text) {
        return NULL;
    }

    while (*text && isspace((unsigned char)*text)) {
        text++;
    }

    size_t len = strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }

    return text;
}

static bool append_string(char*** list, size_t* count, size_t* capacity, const char* value) {
    if (!value) {
        return true;
    }

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 4 : (*capacity * 2);
        char** resized = (char**)realloc(*list, sizeof(char*) * new_capacity);
        if (!resized) {
            return false;
        }
        *list = resized;
        *capacity = new_capacity;
    }

    (*list)[*count] = strdup(value);
    if (!(*list)[*count]) {
        return false;
    }
    (*count)++;
    return true;
}

static void free_string_list(char** list, size_t count) {
    if (!list) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        free(list[i]);
    }
    free(list);
}

static char** collect_oruspath_entries(size_t* out_count) {
    if (out_count) {
        *out_count = 0;
    }

    const char* env = getenv("ORUSPATH");
    if (!env || !*env) {
        return NULL;
    }

    char separator = ':';
#ifdef _WIN32
    separator = ';';
#endif

    char* copy = strdup(env);
    if (!copy) {
        return NULL;
    }

    char** entries = NULL;
    size_t count = 0;
    size_t capacity = 0;

    char* cursor = copy;
    while (cursor && *cursor) {
        char* next = strchr(cursor, separator);
        if (next) {
            *next = '\0';
            next++;
        }

        char* trimmed = trim_whitespace_in_place(cursor);
        if (trimmed && *trimmed) {
            if (!append_string(&entries, &count, &capacity, trimmed)) {
                free_string_list(entries, count);
                free(copy);
                return NULL;
            }
        }

        cursor = next;
    }

    free(copy);

    if (out_count) {
        *out_count = count;
    }
    return entries;
}

static char* get_executable_directory(void) {
    if (cached_executable_dir) {
        return cached_executable_dir;
    }

#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        buffer[len] = '\0';
        cached_executable_dir = copy_dirname(buffer);
        if (cached_executable_dir) {
            return cached_executable_dir;
        }
    }
#elif defined(__APPLE__)
    uint32_t size = 0;
    if (_NSGetExecutablePath(NULL, &size) == -1 && size > 0) {
        uint32_t buffer_capacity = size;
        char* path_buffer = (char*)reallocate(NULL, 0, (size_t)buffer_capacity);
        if (path_buffer) {
            uint32_t requested = buffer_capacity;
            if (_NSGetExecutablePath(path_buffer, &requested) == 0) {
                char resolved[PATH_MAX];
                char* canonical = realpath(path_buffer, resolved);
                const char* target = canonical ? canonical : path_buffer;
                cached_executable_dir = copy_dirname(target);
            }
            if (cached_executable_dir) {
                reallocate(path_buffer, (size_t)buffer_capacity, 0);
                return cached_executable_dir;
            }
            reallocate(path_buffer, (size_t)buffer_capacity, 0);
        }
    }

    char path_buffer[PATH_MAX];
    size = (uint32_t)sizeof(path_buffer);
    int exec_path_result = _NSGetExecutablePath(path_buffer, &size);
    if (exec_path_result == 0) {
        char resolved[PATH_MAX];
        char* canonical = realpath(path_buffer, resolved);
        const char* target = canonical ? canonical : path_buffer;
        cached_executable_dir = copy_dirname(target);
        if (cached_executable_dir) {
            return cached_executable_dir;
        }
    } else if (exec_path_result == -1 && size > 0) {
        uint32_t heap_capacity = size;
        char* heap_buffer = (char*)reallocate(NULL, 0, (size_t)heap_capacity);
        if (heap_buffer) {
            uint32_t requested = heap_capacity;
            if (_NSGetExecutablePath(heap_buffer, &requested) == 0) {
                char resolved[PATH_MAX];
                char* canonical = realpath(heap_buffer, resolved);
                const char* target = canonical ? canonical : heap_buffer;
                cached_executable_dir = copy_dirname(target);
            }
            if (cached_executable_dir) {
                reallocate(heap_buffer, (size_t)heap_capacity, 0);
                return cached_executable_dir;
            }
            reallocate(heap_buffer, (size_t)heap_capacity, 0);
        }
    }
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        cached_executable_dir = copy_dirname(buffer);
        if (cached_executable_dir) {
            return cached_executable_dir;
        }
    }
#endif

    cached_executable_dir = make_absolute_path(".");
    return cached_executable_dir;
}

static const char* module_cache_lookup(const char* key) {
    if (!key) {
        return NULL;
    }

    for (size_t i = 0; i < module_cache_count; ++i) {
        if (module_cache_entries[i].key && strcmp(module_cache_entries[i].key, key) == 0) {
            return module_cache_entries[i].resolved_path;
        }
    }
    return NULL;
}

static bool module_cache_store(const char* key, const char* path) {
    if (!key || !path) {
        return false;
    }

    if (module_cache_lookup(key)) {
        return true;
    }

    if (module_cache_count == module_cache_capacity) {
        size_t new_capacity = module_cache_capacity == 0 ? 8 : (module_cache_capacity * 2);
        ModuleCacheEntry* resized = (ModuleCacheEntry*)realloc(module_cache_entries,
                                                              sizeof(ModuleCacheEntry) * new_capacity);
        if (!resized) {
            return false;
        }
        module_cache_entries = resized;
        module_cache_capacity = new_capacity;
    }

    char* key_copy = strdup(key);
    char* path_copy = strdup(path);
    if (!key_copy || !path_copy) {
        free(key_copy);
        free(path_copy);
        return false;
    }

    module_cache_entries[module_cache_count].key = key_copy;
    module_cache_entries[module_cache_count].resolved_path = path_copy;
    module_cache_count++;
    return true;
}

static char* make_cache_key(const char* prefix, const char* normalized) {
    if (!normalized) {
        return NULL;
    }

    if (!prefix || !*prefix) {
        return strdup(normalized);
    }

    size_t prefix_len = strlen(prefix);
    size_t normalized_len = strlen(normalized);
    size_t total_len = prefix_len + 1 + normalized_len + 1;
    char* buffer = (char*)malloc(total_len);
    if (!buffer) {
        return NULL;
    }

    memcpy(buffer, prefix, prefix_len);
    buffer[prefix_len] = '|';
    memcpy(buffer + prefix_len + 1, normalized, normalized_len + 1);
    return buffer;
}

static void report_module_resolution_failure(const char* module_name, const char* normalized,
                                             char** roots, size_t root_count) {
    const char* header = "  - %s\n";
    size_t buffer_len = 0;
    for (size_t i = 0; i < root_count; ++i) {
        buffer_len += strlen(roots[i]) + strlen(header);
    }

    if (root_count == 0) {
        buffer_len += strlen("  - <none>\n");
    }

    char* summary = (char*)malloc(buffer_len + 1);
    if (!summary) {
        summary = strdup("  - <memory error>\n");
    }

    if (summary) {
        summary[0] = '\0';
        if (root_count == 0) {
            strcpy(summary, "  - <none>\n");
        } else {
            for (size_t i = 0; i < root_count; ++i) {
                strcat(summary, "  - ");
                strcat(summary, roots[i]);
                strcat(summary, "\n");
            }
        }
    }

    runtimeError(ERROR_IMPORT, CURRENT_LOCATION(),
                 "Unable to resolve module '%s'. Normalized path '%s'.\nSearch roots tried:\n%s"
                 "Hint: Set the ORUSPATH environment variable to add additional module directories.",
                 module_name ? module_name : "<unknown>",
                 normalized ? normalized : "<unknown>",
                 summary ? summary : "  - <none>\n");

    free(summary);
}

static char* normalize_module_name(const char* module_name) {
    if (!module_name || !*module_name) {
        return NULL;
    }

    size_t name_len = strlen(module_name);
    bool has_extension = has_orus_suffix(module_name, name_len);
    size_t suffix_len = 5;  // strlen(".orus")
    size_t base_len = has_extension ? name_len - suffix_len : name_len;

    bool has_separator = false;
    for (size_t i = 0; i < base_len; ++i) {
        char ch = module_name[i];
        if (ch == '.' || ch == '/' || ch == '\\') {
            has_separator = true;
            break;
        }
    }

    const char* prefix = NULL;
    size_t prefix_len = 0;
    if (!has_separator) {
        prefix = "std/";
        prefix_len = 4;
    }

    size_t total_len = prefix_len + base_len + (has_extension ? suffix_len : suffix_len) + 1;
    char* normalized = (char*)malloc(total_len);
    if (!normalized) {
        return NULL;
    }

    size_t offset = 0;
    if (prefix) {
        memcpy(normalized, prefix, prefix_len);
        offset = prefix_len;
    }

    for (size_t i = 0; i < base_len; ++i) {
        char ch = module_name[i];
        if (ch == '.' || ch == '/' || ch == '\\') {
            if (offset > 0 && normalized[offset - 1] == '/') {
                continue;
            }
            normalized[offset++] = '/';
        } else {
            normalized[offset++] = ch;
        }
    }

    if (!has_extension) {
        memcpy(normalized + offset, ".orus", suffix_len + 1);
    } else {
        memcpy(normalized + offset, module_name + base_len, suffix_len + 1);
    }

    return normalized;
}

static char* build_module_path(const char* base_path, const char* module_name) {
    if (!module_name) {
        return NULL;
    }

    char* normalized = normalize_module_name(module_name);
    if (!normalized) {
        return NULL;
    }

    char* caller_dir = copy_dirname(base_path);
    char* caller_key = make_cache_key(caller_dir, normalized);
    char* global_key = make_cache_key(NULL, normalized);

    const char* cached_path = caller_key ? module_cache_lookup(caller_key) : NULL;
    if (!cached_path) {
        cached_path = module_cache_lookup(global_key);
    }

    if (cached_path) {
        char* result = strdup(cached_path);
        free(normalized);
        free(caller_dir);
        free(caller_key);
        free(global_key);
        return result;
    }

    char** root_descriptions = NULL;
    size_t root_description_count = 0;
    size_t root_description_capacity = 0;

    ModuleSearchRoot* roots = NULL;
    size_t root_count = 0;
    size_t root_capacity = 0;

    if (caller_dir) {
        append_search_root(&roots, &root_count, &root_capacity, caller_dir, MODULE_ROOT_CALLER);
    }

    char* exe_dir = get_executable_directory();
    if (exe_dir) {
        append_search_root(&roots, &root_count, &root_capacity, exe_dir, MODULE_ROOT_STD);

        bool exe_dir_has_std = false;
        char* std_probe = join_paths(exe_dir, "std");
        if (std_probe) {
            if (directory_exists(std_probe)) {
                exe_dir_has_std = true;
            }
            free(std_probe);
        }

        if (!exe_dir_has_std && path_has_suffix_component(exe_dir, "bin")) {
            char* install_root_candidate = join_paths(exe_dir, "..");
            char* install_root = NULL;
            if (install_root_candidate) {
                install_root = make_absolute_path(install_root_candidate);
                free(install_root_candidate);
            }

            if (install_root) {
                char* install_std = join_paths(install_root, "std");
                if (install_std) {
                    if (directory_exists(install_std)) {
                        append_search_root(&roots, &root_count, &root_capacity, install_root, MODULE_ROOT_STD);
                        exe_dir_has_std = true;
                    }
                    free(install_std);
                }
                free(install_root);
            }
        }

        if (!exe_dir_has_std) {
            fprintf(stderr,
                    "Warning: Standard library directory '%s' missing expected 'std' subdirectory."
                    " Falling back to bundled search roots.\n",
                    exe_dir);
        }
    }

#ifdef __APPLE__
    const char* mac_std_roots[] = {
        "/Library/Orus",
        "/Library/Orus/latest",
    };
    for (size_t i = 0; i < sizeof(mac_std_roots) / sizeof(mac_std_roots[0]); ++i) {
        const char* root_path = mac_std_roots[i];
        append_search_root(&roots, &root_count, &root_capacity, root_path, MODULE_ROOT_STD);
        char* std_probe = join_paths(root_path, "std");
        if (std_probe) {
            if (!directory_exists(std_probe)) {
                fprintf(stderr,
                        "Warning: macOS stdlib fallback '%s' missing expected 'std' directory."
                        " Resolver will continue searching.\n",
                        root_path);
            }
            free(std_probe);
        }
    }
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__posix__)) && !defined(__APPLE__) && !defined(_WIN32)
    const char* posix_std_roots[] = {
        "/usr/local/lib/orus",
        "/usr/lib/orus",
    };
    for (size_t i = 0; i < sizeof(posix_std_roots) / sizeof(posix_std_roots[0]); ++i) {
        const char* root_path = posix_std_roots[i];
        append_search_root(&roots, &root_count, &root_capacity, root_path, MODULE_ROOT_STD);
        char* std_probe = join_paths(root_path, "std");
        if (std_probe) {
            if (!directory_exists(std_probe)) {
                fprintf(stderr,
                        "Warning: POSIX stdlib fallback '%s' missing expected 'std' directory."
                        " Resolver will continue searching.\n",
                        root_path);
            }
            free(std_probe);
        }
    }
#endif

#ifdef _WIN32
    const char* windows_std_roots[] = {
        "C:/Program Files/Orus",
        "C:/Program Files (x86)/Orus",
    };
    for (size_t i = 0; i < sizeof(windows_std_roots) / sizeof(windows_std_roots[0]); ++i) {
        const char* root_path = windows_std_roots[i];
        append_search_root(&roots, &root_count, &root_capacity, root_path, MODULE_ROOT_STD);
        char* std_probe = join_paths(root_path, "std");
        if (std_probe) {
            if (!directory_exists(std_probe)) {
                fprintf(stderr,
                        "Warning: Windows stdlib fallback '%s' missing expected 'std' directory."
                        " Resolver will continue searching.\n",
                        root_path);
            }
            free(std_probe);
        }
    }
#endif

    size_t env_count = 0;
    char** env_entries = collect_oruspath_entries(&env_count);
    for (size_t i = 0; i < env_count; ++i) {
        append_search_root(&roots, &root_count, &root_capacity, env_entries[i], MODULE_ROOT_ENV);
    }

    char* resolved = NULL;
    ModuleRootKind resolved_kind = MODULE_ROOT_DIRECT;

    if (path_exists(normalized)) {
        resolved = make_absolute_path(normalized);
        resolved_kind = MODULE_ROOT_DIRECT;
    }

    for (size_t i = 0; !resolved && i < root_count; ++i) {
        char* candidate = join_paths(roots[i].path, normalized);
        if (candidate && path_exists(candidate)) {
            resolved = make_absolute_path(candidate);
            resolved_kind = roots[i].kind;
        }
        free(candidate);
    }

    if (resolved) {
        if (resolved_kind == MODULE_ROOT_CALLER && caller_key) {
            module_cache_store(caller_key, resolved);
        } else if (global_key) {
            module_cache_store(global_key, resolved);
        }

        free_string_list(env_entries, env_count);
        free_search_roots(roots, root_count);
        free(normalized);
        free(caller_dir);
        free(caller_key);
        free(global_key);
        free_string_list(root_descriptions, root_description_count);
        return resolved;
    }

    if (is_absolute_path(normalized)) {
        size_t needed = strlen(normalized) + strlen(" (explicit path)") + 1;
        char* entry = (char*)malloc(needed);
        if (entry) {
            snprintf(entry, needed, "%s (explicit path)", normalized);
            append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                          entry);
            free(entry);
        } else {
            append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                          normalized);
        }
    } else {
#ifdef _WIN32
        char cwd_buffer[MAX_PATH];
        if (_getcwd(cwd_buffer, MAX_PATH)) {
            size_t needed = strlen(cwd_buffer) + 1 + strlen(normalized) + strlen(" (current working directory)") + 1;
            char* cwd_entry = (char*)malloc(needed);
            if (cwd_entry) {
                snprintf(cwd_entry, needed, "%s/%s (current working directory)", cwd_buffer, normalized);
                append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                              cwd_entry);
                free(cwd_entry);
            }
        } else {
            append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                          normalized);
        }
#else
        char cwd_buffer[PATH_MAX];
        if (getcwd(cwd_buffer, sizeof(cwd_buffer))) {
            size_t needed = strlen(cwd_buffer) + 1 + strlen(normalized) + strlen(" (current working directory)") + 1;
            char* cwd_entry = (char*)malloc(needed);
            if (cwd_entry) {
                snprintf(cwd_entry, needed, "%s/%s (current working directory)", cwd_buffer, normalized);
                append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                              cwd_entry);
                free(cwd_entry);
            }
        } else {
            append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                          normalized);
        }
#endif
    }

    for (size_t i = 0; i < root_count; ++i) {
        const char* label = "module search root";
        switch (roots[i].kind) {
            case MODULE_ROOT_CALLER:
                label = "importer directory";
                break;
            case MODULE_ROOT_STD: {
                const char* std_label = "stdlib directory";
                bool is_install_root = false;
                if (cached_executable_dir && path_has_suffix_component(cached_executable_dir, "bin")) {
                    char* install_root_candidate = join_paths(cached_executable_dir, "..");
                    char* install_root = NULL;
                    if (install_root_candidate) {
                        install_root = make_absolute_path(install_root_candidate);
                        free(install_root_candidate);
                    }
                    if (install_root) {
                        if (strcmp(roots[i].path, install_root) == 0) {
                            std_label = "installed stdlib root";
                            is_install_root = true;
                        }
                        free(install_root);
                    }
                }
#ifdef __APPLE__
                if (!is_install_root && (strcmp(roots[i].path, "/Library/Orus") == 0 ||
                                         strcmp(roots[i].path, "/Library/Orus/latest") == 0)) {
                    std_label = "macOS stdlib fallback";
                }
#endif
#if (defined(__linux__) || defined(__unix__) || defined(__posix__)) && !defined(__APPLE__) && !defined(_WIN32)
                if (!is_install_root && (strcmp(roots[i].path, "/usr/local/lib/orus") == 0 ||
                                         strcmp(roots[i].path, "/usr/lib/orus") == 0)) {
                    std_label = "system stdlib fallback";
                }
#endif
#ifdef _WIN32
                if (!is_install_root && (strcmp(roots[i].path, "C:/Program Files/Orus") == 0 ||
                                         strcmp(roots[i].path, "C:/Program Files (x86)/Orus") == 0)) {
                    std_label = "Windows stdlib fallback";
                }
#endif
                label = std_label;
                break;
            }
            case MODULE_ROOT_ENV:
                label = "ORUSPATH entry";
                break;
            default:
                break;
        }

        size_t needed = strlen(roots[i].path) + strlen(label) + 4;
        char* entry = (char*)malloc(needed);
        if (entry) {
            snprintf(entry, needed, "%s (%s)", roots[i].path, label);
            append_string(&root_descriptions, &root_description_count, &root_description_capacity, entry);
            free(entry);
        } else {
            append_string(&root_descriptions, &root_description_count, &root_description_capacity,
                          roots[i].path);
        }
    }

    report_module_resolution_failure(module_name, normalized, root_descriptions, root_description_count);

    free_string_list(env_entries, env_count);
    free_search_roots(roots, root_count);
    free_string_list(root_descriptions, root_description_count);
    free(normalized);
    free(caller_dir);
    free(caller_key);
    free(global_key);
    return NULL;
}

static void free_module_imports(ModuleImportInfo* imports, int count) {
    if (!imports) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(imports[i].name);
    }
    free(imports);
}

static bool collect_module_imports(ASTNode* ast, ModuleImportInfo** out_imports, int* out_count) {
    if (!out_imports || !out_count) {
        return false;
    }

    *out_imports = NULL;
    *out_count = 0;

    if (!ast || ast->type != NODE_PROGRAM) {
        return true;
    }

    ModuleImportInfo* imports = NULL;
    int count = 0;

    for (int i = 0; i < ast->program.count; i++) {
        ASTNode* decl = ast->program.declarations[i];
        if (!decl || decl->type != NODE_IMPORT || !decl->import.moduleName) {
            continue;
        }

        char* copy = strdup(decl->import.moduleName);
        if (!copy) {
            free_module_imports(imports, count);
            return false;
        }

        ModuleImportInfo* resized = (ModuleImportInfo*)realloc(imports, sizeof(ModuleImportInfo) * (size_t)(count + 1));
        if (!resized) {
            free(copy);
            free_module_imports(imports, count);
            return false;
        }

        imports = resized;
        imports[count].name = copy;
        imports[count].line = decl->location.line;
        imports[count].column = decl->location.column;
        count++;
    }

    *out_imports = imports;
    *out_count = count;
    return true;
}

static bool load_module_list(const char* current_path, ModuleImportInfo* imports, int module_count) {
    if (!imports || module_count == 0) {
        return true;
    }

    const char* previous_file = vm.filePath;
    int previous_line = vm.currentLine;
    int previous_column = vm.currentColumn;

    if (current_path) {
        vm.filePath = current_path;
    }

    for (int i = 0; i < module_count; i++) {
        ModuleImportInfo* info = &imports[i];
        vm.currentLine = info->line;
        vm.currentColumn = info->column;

        char* dep_path = build_module_path(current_path, info->name);
        if (!dep_path) {
            vm.filePath = previous_file;
            vm.currentLine = previous_line;
            vm.currentColumn = previous_column;
            return false;
        }

        InterpretResult result = interpret_module(dep_path, info->name);
        free(dep_path);
        if (result != INTERPRET_OK) {
            vm.filePath = previous_file;
            vm.currentLine = previous_line;
            vm.currentColumn = previous_column;
            return false;
        }
    }

    vm.filePath = previous_file;
    vm.currentLine = previous_line;
    vm.currentColumn = previous_column;
    return true;
}

InterpretResult interpret_module(const char* path, const char* module_name_hint) {
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

    // Extract file name for diagnostics
    const char* fileName = strrchr(path, '/');
    if (!fileName) fileName = strrchr(path, '\\'); // Windows path separator
    if (!fileName) fileName = path;
    else fileName++; // Skip the separator

    if (module_name_hint) {
        module_name = strdup(module_name_hint);
    }

    if (!module_name) {
        module_name = infer_module_name_from_path(path);
    }

    if (!module_name && fileName) {
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
    ast = parseSourceWithModuleName(source, module_name);

    if (!ast) {
        fprintf(stderr, "Failed to parse module: %s\n", path);
        goto cleanup;
    }

    ModuleImportInfo* module_imports = NULL;
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

        ast = parseSourceWithModuleName(source, module_name);
        if (!ast) {
            fprintf(stderr, "Failed to parse module: %s\n", path);
            goto cleanup;
        }
    } else {
        free_module_imports(module_imports, module_import_count);
        module_imports = NULL;
    }

    if (!compileProgram(ast, &compiler, true)) {
        fprintf(stderr, "Failed to compile module: %s\n", path);
        goto cleanup;
    }

    if (vm.register_file.module_manager && module_name) {
        module_entry = load_module(vm.register_file.module_manager, module_name);
    }

    if (compiler.isModule) {
        for (int i = 0; i < compiler.exportCount; ++i) {
            ModuleExportEntry* entry = &compiler.exports[i];
            if (!entry->name || !entry->intrinsic_symbol || entry->function_index < 0) {
                continue;
            }

            const IntrinsicSignatureInfo* signature = vm_get_intrinsic_signature(entry->intrinsic_symbol);
            if (!signature) {
                continue;
            }

            int native_index = vm_bind_core_intrinsic(entry->intrinsic_symbol, signature);
            if (native_index >= 0) {
                vm_patch_intrinsic_stub(entry->function_index, native_index);
            }
        }
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
                bool should_register = !entry->is_internal_intrinsic;
                bool registered = true;
                if (should_register) {
                    registered = register_module_export(module_entry, entry->name, entry->kind,
                                                        entry->register_index, exported_type,
                                                        entry->intrinsic_symbol);
                } else if (exported_type) {
                    module_free_export_type(exported_type);
                    exported_type = NULL;
                }
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

