#ifndef DEBUG_CONFIG_H
#define DEBUG_CONFIG_H

#include <stdbool.h>
#include <stdio.h>

// Debug categories - each component has its own debug flag
typedef enum {
    DEBUG_NONE = 0,
    DEBUG_CODEGEN = 1 << 0,        // Code generation debug output
    DEBUG_CONSTANTFOLD = 1 << 1,   // Constant folding optimization
    DEBUG_TYPE_INFERENCE = 1 << 2, // Type system and inference
    DEBUG_PARSER = 1 << 3,         // Parser debug output
    DEBUG_LEXER = 1 << 4,          // Lexer/tokenizer debug
    DEBUG_VM = 1 << 5,             // Virtual machine execution
    DEBUG_VM_DISPATCH = 1 << 6,    // VM instruction dispatch
    DEBUG_REGISTER_ALLOC = 1 << 7, // Register allocation
    DEBUG_OPTIMIZER = 1 << 8,      // General optimization passes
    DEBUG_PEEPHOLE = 1 << 9,       // Peephole optimizations
    DEBUG_SYMBOL_TABLE = 1 << 10,  // Symbol table operations
    DEBUG_MEMORY = 1 << 11,        // Memory management
    DEBUG_GC = 1 << 12,            // Garbage collection
    DEBUG_RUNTIME = 1 << 13,       // Runtime system
    DEBUG_PROFILING = 1 << 14,     // Performance profiling
    DEBUG_ERROR = 1 << 15,         // Error handling
    DEBUG_CONFIG = 1 << 16,        // Configuration system
    DEBUG_MAIN = 1 << 17,          // Main program flow
    DEBUG_REPL = 1 << 18,          // REPL interface
    DEBUG_LOGGING = 1 << 19,       // Internal logging system
    DEBUG_TYPED_AST = 1 << 20,     // Typed AST operations
    
    // Convenience flags
    DEBUG_ALL = 0xFFFFFFFF,        // Enable all debug output
    DEBUG_COMPILER = DEBUG_CODEGEN | DEBUG_CONSTANTFOLD | DEBUG_TYPE_INFERENCE | 
                     DEBUG_PARSER | DEBUG_LEXER | DEBUG_REGISTER_ALLOC | 
                     DEBUG_OPTIMIZER | DEBUG_PEEPHOLE | DEBUG_SYMBOL_TABLE | 
                     DEBUG_TYPED_AST,
    DEBUG_VM_ALL = DEBUG_VM | DEBUG_VM_DISPATCH | DEBUG_MEMORY | DEBUG_GC | 
                   DEBUG_RUNTIME | DEBUG_PROFILING
} DebugCategory;

// Debug configuration structure
typedef struct {
    unsigned int enabled_categories;  // Bitfield of enabled debug categories
    bool use_colors;                 // Enable colored output
    bool show_timestamps;            // Show timestamps in debug output
    bool show_thread_id;             // Show thread ID (for future multi-threading)
    bool show_file_location;         // Show file:line in debug output
    FILE* output_stream;             // Output stream (stdout, stderr, or file)
    int verbosity_level;             // 0=minimal, 1=normal, 2=verbose, 3=very verbose
} DebugConfig;

// Global debug configuration
extern DebugConfig g_debug_config;

// Debug system initialization and control
void debug_init(void);
void debug_shutdown(void);
void debug_enable_category(DebugCategory category);
void debug_disable_category(DebugCategory category);
void debug_set_categories(unsigned int categories);
bool debug_is_enabled(DebugCategory category);
void debug_set_colors(bool enable);
void debug_set_timestamps(bool enable);
void debug_set_file_location(bool enable);
void debug_set_output_stream(FILE* stream);
void debug_set_verbosity(int level);

// Convenience functions for common combinations
void debug_enable_compiler(void);
void debug_enable_vm(void);
void debug_enable_all(void);
void debug_disable_all(void);

// Main debug printing function
void debug_printf(DebugCategory category, const char* format, ...);
void debug_printf_verbose(DebugCategory category, int required_verbosity, const char* format, ...);

// Conditional debug macros - only compile if DEBUG is defined
#ifdef DEBUG
    #define DEBUG_PRINT(category, ...) debug_printf(category, __VA_ARGS__)
    #define DEBUG_PRINT_V(category, level, ...) debug_printf_verbose(category, level, __VA_ARGS__)
    #define DEBUG_ENABLED(category) debug_is_enabled(category)
#else
    #define DEBUG_PRINT(category, ...) ((void)0)
    #define DEBUG_PRINT_V(category, level, ...) ((void)0)
    #define DEBUG_ENABLED(category) false
#endif

// Convenience macros for each category
#define DEBUG_CODEGEN_PRINT(...) DEBUG_PRINT(DEBUG_CODEGEN, __VA_ARGS__)
#define DEBUG_CONSTANTFOLD_PRINT(...) DEBUG_PRINT(DEBUG_CONSTANTFOLD, __VA_ARGS__)
#define DEBUG_TYPE_INFERENCE_PRINT(...) DEBUG_PRINT(DEBUG_TYPE_INFERENCE, __VA_ARGS__)
#define DEBUG_PARSER_PRINT(...) DEBUG_PRINT(DEBUG_PARSER, __VA_ARGS__)
#define DEBUG_LEXER_PRINT(...) DEBUG_PRINT(DEBUG_LEXER, __VA_ARGS__)
#define DEBUG_VM_PRINT(...) DEBUG_PRINT(DEBUG_VM, __VA_ARGS__)
#define DEBUG_VM_DISPATCH_PRINT(...) DEBUG_PRINT(DEBUG_VM_DISPATCH, __VA_ARGS__)
#define DEBUG_REGISTER_ALLOC_PRINT(...) DEBUG_PRINT(DEBUG_REGISTER_ALLOC, __VA_ARGS__)
#define DEBUG_OPTIMIZER_PRINT(...) DEBUG_PRINT(DEBUG_OPTIMIZER, __VA_ARGS__)
#define DEBUG_PEEPHOLE_PRINT(...) DEBUG_PRINT(DEBUG_PEEPHOLE, __VA_ARGS__)
#define DEBUG_SYMBOL_TABLE_PRINT(...) DEBUG_PRINT(DEBUG_SYMBOL_TABLE, __VA_ARGS__)
#define DEBUG_MEMORY_PRINT(...) DEBUG_PRINT(DEBUG_MEMORY, __VA_ARGS__)
#define DEBUG_GC_PRINT(...) DEBUG_PRINT(DEBUG_GC, __VA_ARGS__)
#define DEBUG_RUNTIME_PRINT(...) DEBUG_PRINT(DEBUG_RUNTIME, __VA_ARGS__)
#define DEBUG_PROFILING_PRINT(...) DEBUG_PRINT(DEBUG_PROFILING, __VA_ARGS__)
#define DEBUG_ERROR_PRINT(...) DEBUG_PRINT(DEBUG_ERROR, __VA_ARGS__)
#define DEBUG_CONFIG_PRINT(...) DEBUG_PRINT(DEBUG_CONFIG, __VA_ARGS__)
#define DEBUG_MAIN_PRINT(...) DEBUG_PRINT(DEBUG_MAIN, __VA_ARGS__)
#define DEBUG_REPL_PRINT(...) DEBUG_PRINT(DEBUG_REPL, __VA_ARGS__)
#define DEBUG_LOGGING_PRINT(...) DEBUG_PRINT(DEBUG_LOGGING, __VA_ARGS__)
#define DEBUG_TYPED_AST_PRINT(...) DEBUG_PRINT(DEBUG_TYPED_AST, __VA_ARGS__)

// ANSI color codes for debug output
#define DEBUG_COLOR_RESET     "\033[0m"
#define DEBUG_COLOR_BOLD      "\033[1m"
#define DEBUG_COLOR_RED       "\033[31m"
#define DEBUG_COLOR_GREEN     "\033[32m"
#define DEBUG_COLOR_YELLOW    "\033[33m"
#define DEBUG_COLOR_BLUE      "\033[34m"
#define DEBUG_COLOR_MAGENTA   "\033[35m"
#define DEBUG_COLOR_CYAN      "\033[36m"
#define DEBUG_COLOR_WHITE     "\033[37m"

// Category name to string mapping
const char* debug_category_name(DebugCategory category);

// Parse debug categories from string (e.g., "codegen,vm,parser")
unsigned int debug_parse_categories(const char* categories_str);

#endif // DEBUG_CONFIG_H