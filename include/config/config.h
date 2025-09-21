#ifndef ORUS_CONFIG_H
#define ORUS_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

// Forward declarations
struct DebugConfig;

/**
 * Orus Language Configuration System
 * 
 * This system provides centralized configuration management for the Orus interpreter.
 * Configuration can be set via:
 * 1. Command-line arguments (highest precedence)
 * 2. Environment variables (medium precedence)
 * 3. Configuration file (lowest precedence)
 * 4. Default values (fallback)
 */

// Configuration structure containing all configurable parameters
typedef struct {
    // Runtime behavior
    bool trace_execution;          // Enable execution tracing (-t, --trace)
    bool trace_typed_fallbacks;    // Emit typed loop fallback telemetry
    bool debug_mode;              // Enable debug mode (-d, --debug)
    bool verbose;                 // Enable verbose output (-v, --verbose)
    bool quiet;                   // Suppress non-essential output (-q, --quiet)
    bool repl_mode;               // Interactive REPL mode
    
    // VM Configuration
    uint32_t max_recursion_depth;  // Maximum recursion depth (default: 1000)
    uint32_t register_count;       // Number of VM registers (default: 256)
    uint32_t stack_size;           // Stack size in bytes (default: 1MB)
    uint32_t heap_size;            // Heap size in bytes (default: 8MB)
    
    // GC Configuration
    bool gc_enabled;               // Enable garbage collection (default: true)
    uint32_t gc_threshold;         // GC trigger threshold in bytes (default: 1MB)
    uint32_t gc_frequency;         // GC frequency multiplier (default: 1)
    const char* gc_strategy;       // GC strategy: "mark-sweep", "generational" (default: "mark-sweep")
    
    // Parser Configuration
    uint32_t parser_max_depth;     // Maximum parser recursion depth (default: 1000)
    uint32_t parser_buffer_size;   // Parser buffer size (default: 64KB)
    bool parser_strict_mode;       // Strict parsing mode (default: false)
    
    // Error Reporting
    const char* error_format;      // Error format: "friendly", "json", "minimal" (default: "friendly")
    bool error_colors;             // Enable colored error output (default: true if TTY)
    bool error_context;            // Show error context lines (default: true)
    uint32_t error_max_suggestions; // Maximum error suggestions (default: 3)
    
    // Development Tools
    bool show_ast;                 // Show AST dump (--show-ast)
    bool show_typed_ast;           // Show typed AST with type annotations (--show-typed-ast)
    bool show_bytecode;            // Show bytecode dump (--show-bytecode)
    bool show_tokens;              // Show token stream (--show-tokens)
    bool show_optimization_stats;  // Show optimization statistics (--show-opt-stats)
    bool benchmark_mode;           // Enable benchmarking (--benchmark)
    
    // Debug System Configuration
    const char* debug_categories;  // Debug categories to enable (--debug-categories)
    bool debug_colors;             // Enable colored debug output (--debug-colors, --no-debug-colors)
    bool debug_timestamps;         // Show timestamps in debug output (--debug-timestamps)
    bool debug_file_location;      // Show file:line in debug output (--debug-file-location)
    int debug_verbosity;           // Debug verbosity level (--debug-verbosity)
    
    // File paths
    const char* input_file;        // Input file path
    const char* output_file;       // Output file path (if applicable)
    const char* config_file;       // Configuration file path
    const char* log_file;          // Log file path
    
    // Optimization levels
    uint32_t optimization_level;   // 0=none, 1=basic, 2=aggressive (default: 1)
    bool inline_functions;         // Enable function inlining (default: true)
    bool dead_code_elimination;    // Enable dead code elimination (default: true)
    
    // Memory management
    bool memory_profiling;         // Enable memory profiling (default: false)
    bool memory_debugging;         // Enable memory debugging (default: false)
    uint32_t arena_size;           // Arena allocation size (default: 64KB)
    
    // VM Profiling
    bool vm_profiling_enabled;     // Enable VM profiling (--profile)
    bool profile_instructions;     // Profile instruction execution (--profile-instructions)
    bool profile_hot_paths;        // Profile hot paths and loops (--profile-hot-paths)
    bool profile_registers;        // Profile register usage (--profile-registers)
    bool profile_memory_access;    // Profile memory access patterns (--profile-memory)
    bool profile_branches;         // Profile branch prediction (--profile-branches)
    const char* profile_output;    // Profiling output file (--profile-output)
    
} OrusConfig;

// Configuration initialization and management
OrusConfig* config_create(void);
void config_destroy(OrusConfig* config);
void config_reset_to_defaults(OrusConfig* config);

// Configuration loading from various sources
bool config_load_from_file(OrusConfig* config, const char* filename);
bool config_load_from_env(OrusConfig* config);
bool config_parse_args(OrusConfig* config, int argc, const char* argv[]);

// Configuration validation
bool config_validate(const OrusConfig* config);
void config_print_errors(const OrusConfig* config);

// Configuration access
const OrusConfig* config_get_global(void);

// Debug system integration
void config_apply_debug_settings(const OrusConfig* config);
void config_set_global(OrusConfig* config);

// Configuration utilities
void config_print_help(const char* program_name);
void config_print_debug_help(void);
void config_print_version(void);
void config_print_current(const OrusConfig* config);
void config_save_to_file(const OrusConfig* config, const char* filename);

// Environment variable names
#define ORUS_CONFIG_FILE "ORUS_CONFIG"
#define ORUS_TRACE "ORUS_TRACE"
#define ORUS_TRACE_TYPED_FALLBACKS "ORUS_TRACE_TYPED_FALLBACKS"
#define ORUS_DEBUG "ORUS_DEBUG"
#define ORUS_VERBOSE "ORUS_VERBOSE"
#define ORUS_QUIET "ORUS_QUIET"
#define ORUS_MAX_RECURSION "ORUS_MAX_RECURSION"
#define ORUS_REGISTER_COUNT "ORUS_REGISTER_COUNT"
#define ORUS_STACK_SIZE "ORUS_STACK_SIZE"
#define ORUS_HEAP_SIZE "ORUS_HEAP_SIZE"
#define ORUS_GC_ENABLED "ORUS_GC_ENABLED"
#define ORUS_GC_THRESHOLD "ORUS_GC_THRESHOLD"
#define ORUS_GC_STRATEGY "ORUS_GC_STRATEGY"
#define ORUS_ERROR_FORMAT "ORUS_ERROR_FORMAT"
#define ORUS_ERROR_COLORS "ORUS_ERROR_COLORS"
#define ORUS_OPTIMIZATION_LEVEL "ORUS_OPTIMIZATION_LEVEL"
#define ORUS_LOG_FILE "ORUS_LOG_FILE"

// Default values
#define DEFAULT_MAX_RECURSION_DEPTH 1000
#define DEFAULT_REGISTER_COUNT 256
#define DEFAULT_STACK_SIZE (1024 * 1024)      // 1MB
#define DEFAULT_HEAP_SIZE (8 * 1024 * 1024)   // 8MB
#define DEFAULT_GC_THRESHOLD (1024 * 1024)    // 1MB
#define DEFAULT_GC_FREQUENCY 1
#define DEFAULT_GC_STRATEGY "mark-sweep"
#define DEFAULT_PARSER_MAX_DEPTH 1000
#define DEFAULT_PARSER_BUFFER_SIZE (64 * 1024) // 64KB
#define DEFAULT_ERROR_FORMAT "friendly"
#define DEFAULT_ERROR_MAX_SUGGESTIONS 3
#define DEFAULT_OPTIMIZATION_LEVEL 1
#define DEFAULT_ARENA_SIZE (64 * 1024)        // 64KB

#endif // ORUS_CONFIG_H