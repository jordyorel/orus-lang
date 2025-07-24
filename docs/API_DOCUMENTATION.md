# Orus Programming Language - Complete API Documentation

**Comprehensive API reference for embedding and extending the Orus programming language.**

## Table of Contents

1. [Core API](#core-api)
2. [Virtual Machine API](#virtual-machine-api)
3. [Compiler API](#compiler-api)
4. [Type System API](#type-system-api)
5. [Error Handling API](#error-handling-api)
6. [Memory Management API](#memory-management-api)
7. [Configuration API](#configuration-api)
8. [Debugging and Profiling API](#debugging-and-profiling-api)
9. [Extension API](#extension-api)
10. [C Integration Examples](#c-integration-examples)

---

## Core API

### Main Entry Point (`include/orus.h`)

**Primary interface for integrating Orus into applications.**

```c
#include "orus.h"

// System Lifecycle
bool orus_init(void);
void orus_cleanup(void);
OrusVersion orus_get_version(void);

// Execution Interface
OrusResult orus_execute(const char* source);
OrusResult orus_execute_file(const char* filename);
OrusResult orus_execute_bytecode(const uint8_t* bytecode, size_t length);

// Interactive Interface
void orus_repl(void);
bool orus_repl_step(const char* input, char** output);

// Configuration
void orus_set_config(const OrusConfig* config);
OrusConfig* orus_get_config(void);
```

### Core Data Types

```c
// Result type for all operations
typedef enum {
    ORUS_OK = 0,
    ORUS_ERROR_SYNTAX,
    ORUS_ERROR_TYPE,
    ORUS_ERROR_RUNTIME,
    ORUS_ERROR_MEMORY,
    ORUS_ERROR_IO,
    ORUS_ERROR_INTERNAL
} OrusResult;

// Version information
typedef struct {
    int major;
    int minor;
    int patch;
    const char* git_hash;
    const char* build_date;
} OrusVersion;

// Global configuration
typedef struct {
    VMConfiguration vm_config;
    CompilerConfiguration compiler_config;
    DebugConfiguration debug_config;
    bool enable_profiling;
    bool enable_tracing;
    size_t max_memory;
} OrusConfig;
```

### Basic Usage Example

```c
#include "orus.h"

int main() {
    // Initialize Orus runtime
    if (!orus_init()) {
        fprintf(stderr, "Failed to initialize Orus\n");
        return 1;
    }
    
    // Execute Orus code
    const char* code = "x = 42; print(x * 2)";
    OrusResult result = orus_execute(code);
    
    if (result != ORUS_OK) {
        fprintf(stderr, "Execution failed: %d\n", result);
    }
    
    // Cleanup
    orus_cleanup();
    return result == ORUS_OK ? 0 : 1;
}
```

---

## Virtual Machine API

### VM Management (`include/vm/vm.h`)

**Complete interface for VM lifecycle and execution control.**

```c
// VM Creation and Destruction
VM* vm_create(const VMConfiguration* config);
void vm_destroy(VM* vm);
bool vm_reset(VM* vm);

// Execution Control
VMResult vm_execute(VM* vm, Chunk* bytecode);
VMResult vm_execute_function(VM* vm, Function* func, Value* args, int argc);
VMResult vm_step(VM* vm);  // Single instruction execution
void vm_pause(VM* vm);
void vm_resume(VM* vm);
void vm_stop(VM* vm);

// State Inspection
VMState vm_get_state(VM* vm);
Value vm_get_register(VM* vm, uint8_t reg);
void vm_set_register(VM* vm, uint8_t reg, Value value);
CallFrame* vm_get_current_frame(VM* vm);
size_t vm_get_stack_depth(VM* vm);
```

### VM Configuration

```c
typedef struct {
    // Execution settings
    size_t max_stack_depth;        // Maximum call stack depth
    size_t max_execution_time;     // Max execution time (ms)
    size_t max_memory_usage;       // Memory limit (bytes)
    
    // Performance settings
    bool enable_computed_goto;     // Use computed goto dispatch
    bool enable_optimization;      // Enable runtime optimizations
    bool enable_jit;               // Enable JIT compilation (future)
    
    // Debugging settings
    bool enable_tracing;           // Instruction tracing
    bool enable_profiling;         // Performance profiling
    bool enable_gc_stats;          // GC statistics
    
    // Memory settings
    size_t initial_heap_size;      // Initial heap size
    size_t gc_threshold;           // GC trigger threshold
    float gc_growth_factor;        // Heap growth factor
} VMConfiguration;
```

### Register Interface

```c
// Register file access
typedef struct {
    Value registers[REGISTER_COUNT];  // 256 registers
    bool dirty[REGISTER_COUNT];       // Dirty flags
    Type types[REGISTER_COUNT];       // Type tracking
} RegisterFile;

// Register operations
Value vm_load_register(VM* vm, uint8_t reg);
void vm_store_register(VM* vm, uint8_t reg, Value value);
void vm_clear_register(VM* vm, uint8_t reg);
bool vm_is_register_valid(VM* vm, uint8_t reg);

// Register allocation hints
uint8_t vm_suggest_temp_register(VM* vm);
uint8_t vm_suggest_local_register(VM* vm);
void vm_mark_register_live(VM* vm, uint8_t reg);
void vm_mark_register_dead(VM* vm, uint8_t reg);
```

### Value System

```c
// Value representation
typedef struct {
    ValueType type;
    union {
        int32_t i32;
        int64_t i64;
        float f32;
        double f64;
        bool boolean;
        Obj* obj;          // String, Array, Function, etc.
    } as;
} Value;

// Value operations
Value value_make_i32(int32_t val);
Value value_make_f64(double val);
Value value_make_bool(bool val);
Value value_make_nil(void);
Value value_make_string(const char* str);

bool value_is_number(Value value);
bool value_is_string(Value value);
bool value_is_nil(Value value);
bool value_equals(Value a, Value b);

// Type conversions
Value value_cast_to_string(Value value);
Value value_cast_to_number(Value value);
Value value_cast_to_bool(Value value);
```

---

## Compiler API

### Compilation Interface (`include/compiler/compiler.h`)

**High-level compilation interface with backend selection.**

```c
// Main compilation functions
CompileResult compile_source(const char* source, CompilerOptions* options);
CompileResult compile_file(const char* filename, CompilerOptions* options);
CompileResult compile_ast(ASTNode* ast, CompilerOptions* options);

// Backend selection
void compiler_set_backend(CompilerBackend backend);
CompilerBackend compiler_get_backend(void);
bool compiler_supports_feature(CompilerFeature feature);

// Compilation options
typedef struct {
    CompilerBackend backend;       // BACKEND_FAST, BACKEND_OPTIMIZED, BACKEND_HYBRID
    bool enable_optimization;      // Enable optimizations
    bool generate_debug_info;      // Include debug information
    bool strict_type_checking;     // Strict type validation
    const char* output_file;       // Output bytecode file
    CompilerWarningLevel warnings; // Warning level
} CompilerOptions;

// Results and diagnostics
typedef struct {
    OrusResult status;
    Chunk* bytecode;               // Generated bytecode
    CompilerDiagnostics* diagnostics; // Errors and warnings
    CompilerMetrics* metrics;      // Compilation statistics
} CompileResult;
```

### AST Interface (`include/compiler/ast.h`)

**Abstract Syntax Tree manipulation and inspection.**

```c
// AST node creation
ASTNode* ast_create_literal(Value value, SrcLocation loc);
ASTNode* ast_create_identifier(const char* name, SrcLocation loc);
ASTNode* ast_create_binary(ASTNode* left, const char* op, ASTNode* right, SrcLocation loc);
ASTNode* ast_create_unary(const char* op, ASTNode* operand, SrcLocation loc);
ASTNode* ast_create_assignment(const char* name, ASTNode* value, SrcLocation loc);
ASTNode* ast_create_if(ASTNode* condition, ASTNode* then_branch, ASTNode* else_branch, SrcLocation loc);
ASTNode* ast_create_while(ASTNode* condition, ASTNode* body, SrcLocation loc);
ASTNode* ast_create_for_range(const char* var, ASTNode* start, ASTNode* end, ASTNode* body, SrcLocation loc);

// AST traversal
void ast_walk(ASTNode* node, ASTVisitor* visitor);
ASTNode* ast_find_node(ASTNode* root, NodePredicate predicate);
void ast_transform(ASTNode* node, ASTTransformer* transformer);

// AST analysis
bool ast_validate(ASTNode* node, ValidationContext* ctx);
TypeResult ast_infer_types(ASTNode* node, TypeContext* ctx);
ComplexityMetrics ast_analyze_complexity(ASTNode* node);
```

### Parser Interface (`include/compiler/parser.h`)

**Direct parser access for advanced use cases.**

```c
// Parser creation and lifecycle
Parser* parser_create(const char* source, const char* filename);
void parser_destroy(Parser* parser);
void parser_reset(Parser* parser, const char* source);

// Parsing operations
ParseResult parser_parse_program(Parser* parser);
ParseResult parser_parse_expression(Parser* parser);
ParseResult parser_parse_statement(Parser* parser);

// Error recovery
bool parser_recover_from_error(Parser* parser);
void parser_synchronize(Parser* parser);
ParseErrorList* parser_get_errors(Parser* parser);

// Parser configuration
void parser_set_strict_mode(Parser* parser, bool strict);
void parser_enable_extension(Parser* parser, ParserExtension ext);
```

---

## Type System API

### Type Management (`include/type/type.h`)

**Comprehensive type system interface with inference and validation.**

```c
// Type creation and manipulation
Type* type_create_primitive(PrimitiveType prim);
Type* type_create_array(Type* element_type);
Type* type_create_function(Type** param_types, int param_count, Type* return_type);
Type* type_create_struct(const char* name, StructField* fields, int field_count);
Type* type_create_generic(const char* name, TypeConstraint* constraints, int constraint_count);

// Type queries
bool type_is_numeric(Type* type);
bool type_is_integer(Type* type);
bool type_is_floating(Type* type);
bool type_is_signed(Type* type);
bool type_is_assignable(Type* target, Type* source);
bool type_can_cast(Type* target, Type* source);
int type_get_size(Type* type);
int type_get_alignment(Type* type);

// Type inference
TypeResult infer_expression_type(ASTNode* expr, TypeContext* ctx);
TypeResult infer_variable_type(const char* name, TypeContext* ctx);
TypeResult unify_types(Type* type1, Type* type2, TypeContext* ctx);
```

### Cast Validation

```c
// Cast operations
typedef enum {
    CAST_SAFE,       // No data loss possible
    CAST_UNSAFE,     // Potential data loss, explicit cast required
    CAST_FORBIDDEN   // Invalid cast, compilation error
} CastSafety;

CastSafety validate_cast(Type* from, Type* to);
Type* get_common_type(Type* type1, Type* type2);
bool can_implicit_cast(Type* from, Type* to);

// Cast matrix for all primitive types
extern const CastSafety CAST_MATRIX[PRIMITIVE_TYPE_COUNT][PRIMITIVE_TYPE_COUNT];
```

### Type Context and Environments

```c
// Type context for inference
typedef struct {
    TypeEnvironment* env;          // Variable type bindings
    ConstraintSet* constraints;    // Type constraints
    SubstitutionMap* substitutions; // Type variable mappings
    ErrorCollector* errors;        // Type errors
    TypeArena* arena;             // Memory management
} TypeContext;

// Type environment operations
TypeEnvironment* type_env_create(void);
void type_env_destroy(TypeEnvironment* env);
void type_env_bind(TypeEnvironment* env, const char* name, Type* type);
Type* type_env_lookup(TypeEnvironment* env, const char* name);
void type_env_push_scope(TypeEnvironment* env);
void type_env_pop_scope(TypeEnvironment* env);
```

---

## Error Handling API

### Error Management (`include/errors/error_interface.h`)

**Feature-based error system with user-friendly messages.**

```c
// Error reporting functions
void report_compile_error(ErrorCode code, SrcLocation location, const char* message);
void report_runtime_error(VM* vm, ErrorCode code, const char* message);
void report_type_error(TypeErrorKind kind, SrcLocation location, Type* expected, Type* found);
void report_syntax_error(SrcLocation location, const char* expected, const char* found);

// Error collection and formatting
ErrorCollector* error_collector_create(void);
void error_collector_destroy(ErrorCollector* collector);
void error_collector_add(ErrorCollector* collector, Error* error);
char* error_collector_format_all(ErrorCollector* collector);
int error_collector_count(ErrorCollector* collector);
bool error_collector_has_errors(ErrorCollector* collector);
```

### Feature-Specific Error APIs

```c
// Type errors (src/errors/features/type_errors.c)
void report_type_mismatch(SrcLocation location, Type* expected, Type* found);
void report_invalid_cast(SrcLocation location, Type* target, Type* source);
void report_mixed_arithmetic(SrcLocation location, Type* left, Type* right);
void report_undefined_type(SrcLocation location, const char* type_name);

// Variable errors (src/errors/features/variable_errors.c)
void report_undefined_variable(SrcLocation location, const char* name);
void report_immutable_assignment(SrcLocation location, const char* name);
void report_variable_redefinition(SrcLocation location, const char* name);
void report_scope_violation(SrcLocation location, const char* name);

// Syntax errors (src/errors/features/syntax_errors.c)
void report_unexpected_token(SrcLocation location, TokenType expected, TokenType found);
void report_missing_semicolon(SrcLocation location);
void report_unmatched_brace(SrcLocation location, char expected);
void report_invalid_expression(SrcLocation location, const char* details);
```

### Error Recovery and Diagnostics

```c
// Error recovery strategies
bool attempt_error_recovery(Parser* parser);
void synchronize_to_statement(Parser* parser);
void synchronize_to_expression(Parser* parser);
ParseErrorContext* create_error_context(Parser* parser);

// Diagnostic formatting
typedef struct {
    SrcLocation location;
    ErrorSeverity severity;
    const char* message;
    const char* help_text;
    SourceContext* source_context;
} Diagnostic;

char* format_diagnostic(const Diagnostic* diag);
void print_diagnostic(const Diagnostic* diag, FILE* output);
void emit_diagnostics(ErrorCollector* collector, FILE* output);
```

---

## Memory Management API

### Garbage Collection (`include/vm/memory.h`)

**Memory management with garbage collection and arena allocation.**

```c
// Memory subsystem initialization
bool memory_init(MemoryConfig* config);
void memory_cleanup(void);
MemoryStats* memory_get_stats(void);

// Object allocation
Obj* allocate_object(VM* vm, ObjType type, size_t size);
ObjString* allocate_string(VM* vm, const char* chars, int length);
ObjArray* allocate_array(VM* vm, int capacity);
ObjFunction* allocate_function(VM* vm);

// Garbage collection
void collect_garbage(VM* vm);
void gc_mark_object(Obj* object);
void gc_mark_value(Value value);
void gc_mark_roots(VM* vm);
void gc_sweep(VM* vm);

// Memory configuration
typedef struct {
    size_t initial_heap_size;      // Initial heap size (bytes)
    size_t max_heap_size;          // Maximum heap size (bytes)
    float gc_heap_grow_factor;     // Heap growth factor
    size_t gc_threshold;           // GC trigger threshold
    bool enable_generational_gc;   // Generational GC (future)
    bool enable_concurrent_gc;     // Concurrent GC (future)
} MemoryConfig;
```

### Arena Allocation

```c
// Arena management for predictable lifetime objects
Arena* arena_create(size_t size);
void arena_destroy(Arena* arena);
void* arena_alloc(Arena* arena, size_t size);
void arena_reset(Arena* arena);
size_t arena_used(Arena* arena);
size_t arena_remaining(Arena* arena);

// Type-specific arenas
TypeArena* type_arena_create(void);
ASTArena* ast_arena_create(void);
StringArena* string_arena_create(void);
```

### Object Pooling

```c
// Object pools for frequently allocated objects
ObjectPool* object_pool_create(ObjType type, size_t initial_capacity);
void object_pool_destroy(ObjectPool* pool);
Obj* object_pool_acquire(ObjectPool* pool);
void object_pool_release(ObjectPool* pool, Obj* object);
size_t object_pool_available(ObjectPool* pool);
```

---

## Configuration API

### System Configuration (`include/config/config.h`)

**Comprehensive configuration system with multiple sources.**

```c
// Configuration management
OrusConfig* config_create_default(void);
OrusConfig* config_load_from_file(const char* filename);
OrusConfig* config_parse_args(int argc, char** argv);
bool config_validate(const OrusConfig* config);
void config_destroy(OrusConfig* config);

// Configuration merging
OrusConfig* config_merge(const OrusConfig* base, const OrusConfig* override);
void config_apply_environment_variables(OrusConfig* config);
void config_apply_command_line(OrusConfig* config, int argc, char** argv);

// Individual setting access
bool config_get_bool(const OrusConfig* config, const char* key);
int config_get_int(const OrusConfig* config, const char* key);
double config_get_double(const OrusConfig* config, const char* key);
const char* config_get_string(const OrusConfig* config, const char* key);
```

### Configuration Schema

```c
// Complete configuration structure
typedef struct {
    // VM Configuration
    struct {
        size_t max_stack_depth;
        size_t max_memory;
        bool enable_optimization;
        bool enable_jit;
        DispatchStrategy dispatch_strategy;
    } vm;
    
    // Compiler Configuration  
    struct {
        CompilerBackend default_backend;
        bool enable_optimizations;
        bool strict_type_checking;
        WarningLevel warning_level;
        bool generate_debug_info;
    } compiler;
    
    // Memory Configuration
    struct {
        size_t initial_heap_size;
        size_t gc_threshold;
        float gc_growth_factor;
        bool enable_arena_allocation;
        bool enable_object_pooling;
    } memory;
    
    // Debug Configuration
    struct {
        bool enable_tracing;
        bool enable_profiling;
        bool enable_gc_stats;
        TraceLevel trace_level;
        const char* log_file;
    } debug;
} OrusConfig;
```

---

## Debugging and Profiling API

### Debugging Interface (`include/debug/debug.h`)

**Comprehensive debugging and introspection capabilities.**

```c
// Debugger control
Debugger* debugger_create(VM* vm);
void debugger_destroy(Debugger* debugger);
void debugger_attach(Debugger* debugger, VM* vm);
void debugger_detach(Debugger* debugger);

// Breakpoints
void debugger_set_breakpoint(Debugger* debugger, const char* file, int line);
void debugger_remove_breakpoint(Debugger* debugger, int breakpoint_id);
void debugger_list_breakpoints(Debugger* debugger);
bool debugger_is_at_breakpoint(Debugger* debugger);

// Execution control
void debugger_step_over(Debugger* debugger);
void debugger_step_into(Debugger* debugger);
void debugger_step_out(Debugger* debugger);
void debugger_continue(Debugger* debugger);

// State inspection
StackTrace* debugger_get_stack_trace(Debugger* debugger);
VariableList* debugger_get_local_variables(Debugger* debugger);
VariableList* debugger_get_global_variables(Debugger* debugger);
Value debugger_evaluate_expression(Debugger* debugger, const char* expr);
```

### Profiling Interface (`include/profiling/profiler.h`)

**Performance profiling and optimization guidance.**

```c
// Profiler lifecycle
Profiler* profiler_create(ProfileConfig* config);
void profiler_destroy(Profiler* profiler);
void profiler_start(Profiler* profiler);
void profiler_stop(Profiler* profiler);
void profiler_reset(Profiler* profiler);

// Data collection
ProfileData* profiler_get_data(Profiler* profiler);
void profiler_mark_function_entry(Profiler* profiler, const char* name);
void profiler_mark_function_exit(Profiler* profiler, const char* name);
void profiler_record_allocation(Profiler* profiler, size_t size);
void profiler_record_gc_event(Profiler* profiler, GCEventType type, uint64_t duration);

// Reporting
ProfileReport* profiler_generate_report(Profiler* profiler);
void profiler_export_csv(Profiler* profiler, const char* filename);
void profiler_export_json(Profiler* profiler, const char* filename);
void profiler_print_summary(Profiler* profiler, FILE* output);
```

### Tracing Interface

```c
// Execution tracing
void trace_enable(VM* vm, TraceLevel level);
void trace_disable(VM* vm);
void trace_instruction(VM* vm, uint8_t opcode, uint8_t* operands);
void trace_function_call(VM* vm, const char* function_name);
void trace_register_access(VM* vm, uint8_t reg, bool is_write);

// Trace output
void trace_set_output_file(const char* filename);
void trace_set_output_callback(TraceCallback callback);
TraceBuffer* trace_get_buffer(VM* vm);
void trace_flush(VM* vm);
```

---

## Extension API

### Native Function Interface (`include/extensions/native.h`)

**Interface for extending Orus with native C functions.**

```c
// Native function registration
typedef Value (*NativeFunction)(VM* vm, int argc, Value* args);

void register_native_function(const char* name, NativeFunction func);
void register_native_module(const char* module_name, NativeFunctionTable* functions);
bool unregister_native_function(const char* name);

// Function table structure
typedef struct {
    const char* name;
    NativeFunction function;
    int min_args;
    int max_args;
    const char* signature;
} NativeFunctionEntry;

typedef struct {
    const char* module_name;
    NativeFunctionEntry* functions;
    int function_count;
    void (*module_init)(void);
    void (*module_cleanup)(void);
} NativeFunctionTable;
```

### Module System Interface

```c
// Module loading and management
Module* module_load(const char* module_name);
Module* module_load_from_file(const char* filename);
void module_unload(Module* module);
bool module_is_loaded(const char* module_name);

// Symbol export/import
void module_export_function(Module* module, const char* name, Function* func);
void module_export_variable(Module* module, const char* name, Value value);
Function* module_import_function(Module* module, const char* name);
Value module_import_variable(Module* module, const char* name);

// Module initialization
typedef struct {
    const char* name;
    const char* version;
    ModuleInitFunction init;
    ModuleCleanupFunction cleanup;
    DependencyList dependencies;
} ModuleInfo;
```

### FFI (Foreign Function Interface)

```c
// C library integration
FFILibrary* ffi_load_library(const char* library_path);
void ffi_unload_library(FFILibrary* lib);
FFIFunction* ffi_get_function(FFILibrary* lib, const char* symbol_name);
Value ffi_call_function(FFIFunction* func, Value* args, int argc);

// Type bridging
void ffi_register_type_mapping(const char* c_type, Type* orus_type);
Value ffi_c_to_orus_value(void* c_value, const char* c_type);
void* ffi_orus_to_c_value(Value orus_value, const char* c_type);
```

---

## C Integration Examples

### Embedding Orus in C Applications

```c
#include "orus.h"
#include <stdio.h>

// Simple embedding example
int main() {
    // Initialize Orus
    if (!orus_init()) {
        return 1;
    }
    
    // Configure for embedded use
    OrusConfig config = {
        .vm = {
            .max_memory = 1024 * 1024,  // 1MB limit
            .enable_optimization = true,
        },
        .debug = {
            .enable_profiling = false,
            .enable_tracing = false,
        }
    };
    orus_set_config(&config);
    
    // Execute Orus code
    const char* script = 
        "function fibonacci(n) {"
        "    if n <= 1 return n;"
        "    return fibonacci(n-1) + fibonacci(n-2);"
        "}"
        "result = fibonacci(10);"
        "print('Fibonacci(10) =', result);";
        
    OrusResult result = orus_execute(script);
    
    orus_cleanup();
    return result == ORUS_OK ? 0 : 1;
}
```

### Creating Native Extensions

```c
#include "orus.h"
#include <math.h>

// Native math functions
Value native_sqrt(VM* vm, int argc, Value* args) {
    if (argc != 1 || !value_is_number(args[0])) {
        // Error handling
        return value_make_nil();
    }
    
    double input = value_as_double(args[0]);
    double result = sqrt(input);
    return value_make_f64(result);
}

Value native_pow(VM* vm, int argc, Value* args) {
    if (argc != 2 || !value_is_number(args[0]) || !value_is_number(args[1])) {
        return value_make_nil();
    }
    
    double base = value_as_double(args[0]);
    double exponent = value_as_double(args[1]);
    double result = pow(base, exponent);
    return value_make_f64(result);
}

// Math module definition
NativeFunctionEntry math_functions[] = {
    {"sqrt", native_sqrt, 1, 1, "sqrt(number) -> number"},
    {"pow", native_pow, 2, 2, "pow(base, exponent) -> number"},
    {NULL, NULL, 0, 0, NULL}  // Sentinel
};

NativeFunctionTable math_module = {
    .module_name = "math",
    .functions = math_functions,
    .function_count = 2,
    .module_init = NULL,
    .module_cleanup = NULL
};

// Module registration
void init_math_module() {
    register_native_module("math", &math_module);
}
```

### Advanced VM Integration

```c
#include "orus.h"

// Custom VM with controlled execution
void controlled_execution_example() {
    // Create VM with custom configuration
    VMConfiguration vm_config = {
        .max_stack_depth = 1000,
        .max_execution_time = 5000,  // 5 second limit
        .enable_computed_goto = true,
        .enable_profiling = true,
    };
    
    VM* vm = vm_create(&vm_config);
    
    // Compile code to bytecode
    const char* source = "for i in 1..1000000 { sum = sum + i }";
    CompilerOptions compile_opts = {
        .backend = BACKEND_OPTIMIZED,
        .enable_optimization = true,
    };
    
    CompileResult compile_result = compile_source(source, &compile_opts);
    if (compile_result.status != ORUS_OK) {
        printf("Compilation failed\n");
        vm_destroy(vm);
        return;
    }
    
    // Execute with monitoring
    VMResult exec_result = vm_execute(vm, compile_result.bytecode);
    
    // Get performance data
    ProfileData* profile = vm_get_profile_data(vm);
    printf("Instructions executed: %lu\n", profile->instructions_executed);
    printf("GC collections: %lu\n", profile->gc_collections);
    
    // Cleanup
    vm_destroy(vm);
}
```

### Error Handling Integration

```c
#include "orus.h"

// Custom error handler
void custom_error_handler(const Error* error, void* user_data) {
    FILE* log_file = (FILE*)user_data;
    
    fprintf(log_file, "[%s:%d:%d] %s: %s\n",
        error->location.file,
        error->location.line,
        error->location.column,
        error_type_to_string(error->type),
        error->message
    );
    
    if (error->help_text) {
        fprintf(log_file, "Help: %s\n", error->help_text);
    }
}

void error_handling_example() {
    // Set up custom error handling
    FILE* error_log = fopen("orus_errors.log", "w");
    
    ErrorCollector* collector = error_collector_create();
    error_collector_set_handler(collector, custom_error_handler, error_log);
    
    // Execute code with error collection
    const char* buggy_code = "x = 42; y = 'hello'; z = x + y;";  // Type error
    
    OrusResult result = orus_execute(buggy_code);
    if (result != ORUS_OK) {
        printf("Execution failed with %d errors\n", 
               error_collector_count(collector));
        
        // Format all errors
        char* error_summary = error_collector_format_all(collector);
        printf("Error summary:\n%s\n", error_summary);
        free(error_summary);
    }
    
    error_collector_destroy(collector);
    fclose(error_log);
}
```

This comprehensive API documentation provides complete coverage of the Orus programming language implementation, enabling developers to embed, extend, and integrate Orus into their applications effectively.