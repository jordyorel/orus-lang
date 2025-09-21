// config.c - Orus Language Configuration System Implementation
#include "config/config.h"
#include "public/version.h"
#include "debug/debug_config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// Add strcasecmp for cross-platform compatibility
#ifndef strcasecmp
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif
#endif

// Global configuration instance
static OrusConfig* global_config = NULL;

// Helper function to check if TTY supports colors
static bool is_tty_color_supported(void) {
    const char* term = getenv("TERM");
    return term && (strstr(term, "color") || strstr(term, "xterm") || strstr(term, "screen"));
}

// Configuration creation and initialization
OrusConfig* config_create(void) {
    OrusConfig* config = (OrusConfig*)malloc(sizeof(OrusConfig));
    if (!config) return NULL;
    
    config_reset_to_defaults(config);
    return config;
}

void config_destroy(OrusConfig* config) {
    if (!config) return;
    
    // Free allocated strings (if any were dynamically allocated)
    // Note: Most strings are const char* pointing to static strings or env vars
    // Only free if we know they were malloc'd
    
    free(config);
}

void config_reset_to_defaults(OrusConfig* config) {
    if (!config) return;
    
    // Runtime behavior
    config->trace_execution = false;
#ifdef VM_TRACE_TYPED_FALLBACKS
    config->trace_typed_fallbacks = true;
#else
    config->trace_typed_fallbacks = false;
#endif
    config->debug_mode = false;
    config->verbose = false;
    config->quiet = false;
    config->repl_mode = false;
    
    // VM Configuration
    config->max_recursion_depth = DEFAULT_MAX_RECURSION_DEPTH;
    config->register_count = DEFAULT_REGISTER_COUNT;
    config->stack_size = DEFAULT_STACK_SIZE;
    config->heap_size = DEFAULT_HEAP_SIZE;
    
    // GC Configuration
    config->gc_enabled = true;
    config->gc_threshold = DEFAULT_GC_THRESHOLD;
    config->gc_frequency = DEFAULT_GC_FREQUENCY;
    config->gc_strategy = DEFAULT_GC_STRATEGY;
    
    // Parser Configuration
    config->parser_max_depth = DEFAULT_PARSER_MAX_DEPTH;
    config->parser_buffer_size = DEFAULT_PARSER_BUFFER_SIZE;
    config->parser_strict_mode = false;
    
    // Error Reporting
    config->error_format = DEFAULT_ERROR_FORMAT;
    config->error_colors = is_tty_color_supported();
    config->error_context = true;
    config->error_max_suggestions = DEFAULT_ERROR_MAX_SUGGESTIONS;
    
    // Development Tools
    config->show_ast = false;
    config->show_typed_ast = false;
    config->show_bytecode = false;
    config->show_tokens = false;
    config->show_optimization_stats = false;
    config->benchmark_mode = false;
    
    // Debug System Configuration
    config->debug_categories = NULL;      // No debug categories by default
    config->debug_colors = true;          // Enable colors by default (if TTY supports)
    config->debug_timestamps = false;     // No timestamps by default
    config->debug_file_location = false;  // No file location by default
    config->debug_verbosity = 1;          // Normal verbosity by default
    
    // File paths
    config->input_file = NULL;
    config->output_file = NULL;
    config->config_file = NULL;
    config->log_file = NULL;
    
    // Optimization levels
    config->optimization_level = DEFAULT_OPTIMIZATION_LEVEL;
    config->inline_functions = true;
    config->dead_code_elimination = true;
    
    // Memory management
    config->memory_profiling = false;
    config->memory_debugging = false;
    config->arena_size = DEFAULT_ARENA_SIZE;
    
    // VM Profiling
    config->vm_profiling_enabled = false;
    config->profile_instructions = false;
    config->profile_hot_paths = false;
    config->profile_registers = false;
    config->profile_memory_access = false;
    config->profile_branches = false;
    config->profile_output = NULL;
}

// Environment variable loading
bool config_load_from_env(OrusConfig* config) {
    if (!config) return false;
    
    const char* env_val;
    
    // Load boolean flags
    if ((env_val = getenv(ORUS_TRACE))) {
        config->trace_execution = (strcmp(env_val, "1") == 0 ||
                                  strcasecmp(env_val, "true") == 0);
    }

    if ((env_val = getenv(ORUS_TRACE_TYPED_FALLBACKS))) {
        config->trace_typed_fallbacks = (strcmp(env_val, "1") == 0 ||
                                         strcasecmp(env_val, "true") == 0);
    }

    if ((env_val = getenv(ORUS_DEBUG))) {
        config->debug_mode = (strcmp(env_val, "1") == 0 || 
                             strcasecmp(env_val, "true") == 0);
    }
    
    if ((env_val = getenv(ORUS_VERBOSE))) {
        config->verbose = (strcmp(env_val, "1") == 0 || 
                          strcasecmp(env_val, "true") == 0);
    }
    
    if ((env_val = getenv(ORUS_QUIET))) {
        config->quiet = (strcmp(env_val, "1") == 0 || 
                        strcasecmp(env_val, "true") == 0);
    }
    
    // Load numeric values
    if ((env_val = getenv(ORUS_MAX_RECURSION))) {
        int val = atoi(env_val);
        if (val > 0) config->max_recursion_depth = val;
    }
    
    if ((env_val = getenv(ORUS_REGISTER_COUNT))) {
        int val = atoi(env_val);
        if (val > 0 && val <= 256) config->register_count = val;
    }
    
    if ((env_val = getenv(ORUS_STACK_SIZE))) {
        int val = atoi(env_val);
        if (val > 0) config->stack_size = val;
    }
    
    if ((env_val = getenv(ORUS_HEAP_SIZE))) {
        int val = atoi(env_val);
        if (val > 0) config->heap_size = val;
    }
    
    if ((env_val = getenv(ORUS_GC_ENABLED))) {
        config->gc_enabled = (strcmp(env_val, "1") == 0 || 
                             strcasecmp(env_val, "true") == 0);
    }
    
    if ((env_val = getenv(ORUS_GC_THRESHOLD))) {
        int val = atoi(env_val);
        if (val > 0) config->gc_threshold = val;
    }
    
    if ((env_val = getenv(ORUS_GC_STRATEGY))) {
        if (strcmp(env_val, "mark-sweep") == 0 || strcmp(env_val, "generational") == 0) {
            config->gc_strategy = env_val;
        }
    }
    
    if ((env_val = getenv(ORUS_ERROR_FORMAT))) {
        if (strcmp(env_val, "friendly") == 0 || strcmp(env_val, "json") == 0 || 
            strcmp(env_val, "minimal") == 0) {
            config->error_format = env_val;
        }
    }
    
    if ((env_val = getenv(ORUS_ERROR_COLORS))) {
        config->error_colors = (strcmp(env_val, "1") == 0 || 
                               strcasecmp(env_val, "true") == 0);
    }
    
    if ((env_val = getenv(ORUS_OPTIMIZATION_LEVEL))) {
        int val = atoi(env_val);
        if (val >= 0 && val <= 2) config->optimization_level = val;
    }
    
    if ((env_val = getenv(ORUS_LOG_FILE))) {
        config->log_file = env_val;
    }
    
    return true;
}

// Helper function to parse size strings (e.g., "1MB", "512K")
static uint32_t parse_size_string(const char* str) {
    if (!str) return 0;
    
    char* endptr;
    uint32_t value = strtoul(str, &endptr, 10);
    
    if (endptr != str) {
        // Check for size suffixes
        if (*endptr == 'K' || *endptr == 'k') {
            value *= 1024;
        } else if (*endptr == 'M' || *endptr == 'm') {
            value *= 1024 * 1024;
        } else if (*endptr == 'G' || *endptr == 'g') {
            value *= 1024 * 1024 * 1024;
        }
    }
    
    return value;
}

// Command-line argument parsing
bool config_parse_args(OrusConfig* config, int argc, const char* argv[]) {
    if (!config || argc < 1) return false;
    
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        
        // Help and version (existing)
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            config_print_help(argv[0]);
            return false; // Signal to exit
        }
        
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            config_print_version();
            return false; // Signal to exit
        }
        
        // Runtime flags
        if (strcmp(arg, "-t") == 0 || strcmp(arg, "--trace") == 0) {
            config->trace_execution = true;
        } else if (strcmp(arg, "--trace-typed-fallbacks") == 0) {
            config->trace_typed_fallbacks = true;
        } else if (strcmp(arg, "--no-trace-typed-fallbacks") == 0) {
            config->trace_typed_fallbacks = false;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0) {
            config->debug_mode = true;
        } else if (strcmp(arg, "--verbose") == 0) {
            config->verbose = true;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            config->quiet = true;
        }
        
        // Development tools
        else if (strcmp(arg, "--show-ast") == 0) {
            config->show_ast = true;
        } else if (strcmp(arg, "--show-typed-ast") == 0) {
            config->show_typed_ast = true;
        } else if (strcmp(arg, "--show-bytecode") == 0) {
            config->show_bytecode = true;
        } else if (strcmp(arg, "--show-tokens") == 0) {
            config->show_tokens = true;
        } else if (strcmp(arg, "--show-opt-stats") == 0) {
            config->show_optimization_stats = true;
        } else if (strcmp(arg, "--benchmark") == 0) {
            config->benchmark_mode = true;
        }
        
        // VM Profiling options
        else if (strcmp(arg, "--profile") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_instructions = true;
            config->profile_hot_paths = true;
        } else if (strcmp(arg, "--profile-instructions") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_instructions = true;
        } else if (strcmp(arg, "--profile-hot-paths") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_hot_paths = true;
        } else if (strcmp(arg, "--profile-registers") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_registers = true;
        } else if (strcmp(arg, "--profile-memory") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_memory_access = true;
        } else if (strcmp(arg, "--profile-branches") == 0) {
            config->vm_profiling_enabled = true;
            config->profile_branches = true;
        } else if (strncmp(arg, "--profile-output=", 17) == 0) {
            config->profile_output = arg + 17;
        }
        
        // VM configuration with values
        else if (strncmp(arg, "--max-recursion=", 16) == 0) {
            int val = atoi(arg + 16);
            if (val > 0) config->max_recursion_depth = val;
        } else if (strncmp(arg, "--registers=", 12) == 0) {
            int val = atoi(arg + 12);
            if (val > 0 && val <= 256) config->register_count = val;
        } else if (strncmp(arg, "--stack-size=", 13) == 0) {
            config->stack_size = parse_size_string(arg + 13);
        } else if (strncmp(arg, "--heap-size=", 12) == 0) {
            config->heap_size = parse_size_string(arg + 12);
        }
        
        // GC configuration
        else if (strcmp(arg, "--gc-disable") == 0) {
            config->gc_enabled = false;
        } else if (strncmp(arg, "--gc-threshold=", 15) == 0) {
            config->gc_threshold = parse_size_string(arg + 15);
        } else if (strncmp(arg, "--gc-strategy=", 14) == 0) {
            const char* strategy = arg + 14;
            if (strcmp(strategy, "mark-sweep") == 0 || strcmp(strategy, "generational") == 0) {
                config->gc_strategy = strategy;
            }
        }
        
        // Error reporting
        else if (strncmp(arg, "--error-format=", 15) == 0) {
            const char* format = arg + 15;
            if (strcmp(format, "friendly") == 0 || strcmp(format, "json") == 0 || 
                strcmp(format, "minimal") == 0) {
                config->error_format = format;
            }
        } else if (strcmp(arg, "--no-colors") == 0) {
            config->error_colors = false;
        } else if (strcmp(arg, "--no-context") == 0) {
            config->error_context = false;
        }
        
        // Optimization
        else if (strncmp(arg, "-O", 2) == 0) {
            int level = atoi(arg + 2);
            if (level >= 0 && level <= 2) config->optimization_level = level;
        } else if (strcmp(arg, "--no-inline") == 0) {
            config->inline_functions = false;
        } else if (strcmp(arg, "--no-dce") == 0) {
            config->dead_code_elimination = false;
        }
        
        // Memory debugging
        else if (strcmp(arg, "--memory-profile") == 0) {
            config->memory_profiling = true;
        } else if (strcmp(arg, "--memory-debug") == 0) {
            config->memory_debugging = true;
        }
        
        // Configuration file
        else if (strncmp(arg, "--config=", 9) == 0) {
            config->config_file = arg + 9;
        }
        
        // Output options
        else if (strncmp(arg, "--output=", 9) == 0) {
            config->output_file = arg + 9;
        } else if (strncmp(arg, "--log=", 6) == 0) {
            config->log_file = arg + 6;
        }
        
        // Parser options
        else if (strcmp(arg, "--strict") == 0) {
            config->parser_strict_mode = true;
        } else if (strncmp(arg, "--parser-depth=", 15) == 0) {
            int val = atoi(arg + 15);
            if (val > 0) config->parser_max_depth = val;
        }
        
        // Debug System options
        else if (strncmp(arg, "--debug-categories=", 19) == 0) {
            config->debug_categories = arg + 19;
        } else if (strcmp(arg, "--debug-colors") == 0) {
            config->debug_colors = true;
        } else if (strcmp(arg, "--no-debug-colors") == 0) {
            config->debug_colors = false;
        } else if (strcmp(arg, "--debug-timestamps") == 0) {
            config->debug_timestamps = true;
        } else if (strcmp(arg, "--debug-file-location") == 0) {
            config->debug_file_location = true;
        } else if (strncmp(arg, "--debug-verbosity=", 18) == 0) {
            int val = atoi(arg + 18);
            if (val >= 0 && val <= 3) config->debug_verbosity = val;
        } else if (strcmp(arg, "--debug-help") == 0) {
            config_print_debug_help();
            return false; // Signal to exit
        }
        
        // Unknown option starting with -
        else if (arg[0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", arg);
            return false;
        }
        
        // Input file
        else {
            if (config->input_file != NULL) {
                fprintf(stderr, "Too many arguments.\n");
                return false;
            }
            config->input_file = arg;
        }
    }
    
    // Set REPL mode if no input file
    if (config->input_file == NULL) {
        config->repl_mode = true;
    }
    
    return true;
}

// Configuration validation
bool config_validate(const OrusConfig* config) {
    if (!config) return false;
    
    // Validate ranges
    if (config->max_recursion_depth < 10 || config->max_recursion_depth > 100000) {
        return false;
    }
    
    if (config->register_count < 1 || config->register_count > 256) {
        return false;
    }
    
    if (config->stack_size < 1024 || config->stack_size > 1024*1024*1024) {
        return false;
    }
    
    if (config->heap_size < 1024 || config->heap_size > 1024*1024*1024) {
        return false;
    }
    
    if (config->optimization_level > 2) {
        return false;
    }
    
    // Validate conflicting options
    if (config->verbose && config->quiet) {
        return false;
    }
    
    return true;
}

void config_print_errors(const OrusConfig* config) {
    if (!config) {
        fprintf(stderr, "Error: Null configuration\n");
        return;
    }
    
    if (config->max_recursion_depth < 10 || config->max_recursion_depth > 100000) {
        fprintf(stderr, "Error: Max recursion depth must be between 10 and 100000\n");
    }
    
    if (config->register_count < 1 || config->register_count > 256) {
        fprintf(stderr, "Error: Register count must be between 1 and 256\n");
    }
    
    if (config->verbose && config->quiet) {
        fprintf(stderr, "Error: Cannot use both --verbose and --quiet options\n");
    }
}

// Global configuration management
const OrusConfig* config_get_global(void) {
    return global_config;
}

void config_set_global(OrusConfig* config) {
    global_config = config;
}

// Utility functions
void config_print_help(const char* program_name) {
    printf("Usage: %s [options] [file]\n", program_name);
    printf("\nOptions:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  -v, --version           Show version information\n");
    printf("  -t, --trace             Enable execution tracing\n");
    printf("  --trace-typed-fallbacks Enable loop typed fallback telemetry\n");
    printf("      --no-trace-typed-fallbacks Disable loop typed fallback telemetry\n");
    printf("  -d, --debug             Enable debug mode\n");
    printf("  --verbose               Enable verbose output\n");
    printf("  -q, --quiet             Suppress non-essential output\n");
    printf("\nDevelopment Tools:\n");
    printf("  --show-ast              Show AST dump\n");
    printf("  --show-typed-ast        Show typed AST with type annotations\n");
    printf("  --show-bytecode         Show bytecode dump\n");
    printf("  --show-tokens           Show token stream\n");
    printf("  --show-opt-stats        Show optimization statistics\n");
    printf("  --benchmark             Enable benchmarking mode\n");
    printf("\nVM Configuration:\n");
    printf("  --max-recursion=N       Set maximum recursion depth (default: %d)\n", DEFAULT_MAX_RECURSION_DEPTH);
    printf("  --registers=N           Set number of VM registers (default: %d)\n", DEFAULT_REGISTER_COUNT);
    printf("  --stack-size=SIZE       Set stack size (default: 1MB)\n");
    printf("  --heap-size=SIZE        Set heap size (default: 8MB)\n");
    printf("\nGarbage Collection:\n");
    printf("  --gc-disable            Disable garbage collection\n");
    printf("  --gc-threshold=SIZE     Set GC trigger threshold (default: 1MB)\n");
    printf("  --gc-strategy=STRATEGY  Set GC strategy: mark-sweep, generational\n");
    printf("\nError Reporting:\n");
    printf("  --error-format=FORMAT   Set error format: friendly, json, minimal\n");
    printf("  --no-colors             Disable colored error output\n");
    printf("  --no-context            Disable error context lines\n");
    printf("\nVM Profiling:\n");
    printf("  --profile               Enable basic profiling (instructions + hot paths)\n");
    printf("  --profile-instructions  Profile instruction execution counts\n");
    printf("  --profile-hot-paths     Profile hot paths and loop detection\n");
    printf("  --profile-registers     Profile register allocation patterns\n");
    printf("  --profile-memory        Profile memory access patterns\n");
    printf("  --profile-branches      Profile branch prediction accuracy\n");
    printf("  --profile-output=FILE   Export profiling data to file\n");
    printf("\nOptimization:\n");
    printf("  -O0, -O1, -O2           Set optimization level (default: 1)\n");
    printf("  --no-inline             Disable function inlining\n");
    printf("  --no-dce                Disable dead code elimination\n");
    printf("\nMemory Debugging:\n");
    printf("  --memory-profile        Enable memory profiling\n");
    printf("  --memory-debug          Enable memory debugging\n");
    printf("\nParser Options:\n");
    printf("  --strict                Enable strict parsing mode\n");
    printf("  --parser-depth=N        Set parser recursion depth (default: %d)\n", DEFAULT_PARSER_MAX_DEPTH);
    printf("\nDebug System:\n");
    printf("  --debug-categories=LIST Debug categories (e.g., codegen,constantfold,parser)\n");
    printf("  --debug-colors          Enable colored debug output\n");
    printf("  --no-debug-colors       Disable colored debug output\n");
    printf("  --debug-timestamps      Show timestamps in debug output\n");
    printf("  --debug-file-location   Show file:line in debug output\n");
    printf("  --debug-verbosity=N     Set debug verbosity (0-3, default: 1)\n");
    printf("  --debug-help            Show detailed debug system help\n");
    printf("\nFile Options:\n");
    printf("  --config=FILE           Load configuration from file\n");
    printf("  --output=FILE           Set output file\n");
    printf("  --log=FILE              Set log file\n");
    printf("\nIf no file is provided, starts interactive REPL mode.\n");
    printf("\nEnvironment Variables:\n");
    printf("  ORUS_TRACE, ORUS_DEBUG, ORUS_VERBOSE, ORUS_QUIET\n");
    printf("  ORUS_MAX_RECURSION, ORUS_REGISTER_COUNT, ORUS_STACK_SIZE\n");
    printf("  ORUS_GC_ENABLED, ORUS_GC_THRESHOLD, ORUS_GC_STRATEGY\n");
    printf("  ORUS_ERROR_FORMAT, ORUS_ERROR_COLORS, ORUS_OPTIMIZATION_LEVEL\n");
    printf("  ORUS_DEBUG, ORUS_DEBUG_COLORS, ORUS_DEBUG_TIMESTAMPS\n");
}

void config_print_debug_help(void) {
    printf("Orus Debug System Help\n");
    printf("======================\n\n");
    
    printf("The Orus debug system provides centralized, configurable debug output\n");
    printf("across all compiler and VM components.\n\n");
    
    printf("Available Debug Categories:\n");
    printf("  codegen        - Code generation and bytecode emission\n");
    printf("  constantfold   - Constant folding optimization\n");
    printf("  type           - Type system and inference\n");
    printf("  parser         - Parser debug output\n");
    printf("  lexer          - Lexer/tokenizer debug\n");
    printf("  vm             - Virtual machine execution\n");
    printf("  dispatch       - VM instruction dispatch\n");
    printf("  regalloc       - Register allocation\n");
    printf("  optimizer      - General optimization passes\n");
    printf("  peephole       - Peephole optimizations\n");
    printf("  symbols        - Symbol table operations\n");
    printf("  memory         - Memory management\n");
    printf("  gc             - Garbage collection\n");
    printf("  runtime        - Runtime system\n");
    printf("  profiling      - Performance profiling\n");
    printf("  error          - Error handling\n");
    printf("  config         - Configuration system\n");
    printf("  main           - Main program flow\n");
    printf("  repl           - REPL interface\n");
    printf("  logging        - Internal logging\n");
    printf("  typed_ast      - Typed AST operations\n\n");
    
    printf("Special Category Combinations:\n");
    printf("  all            - Enable all debug categories\n");
    printf("  compiler       - Enable all compiler-related categories\n");
    printf("  vm             - Enable all VM-related categories\n\n");
    
    printf("Usage Examples:\n");
    printf("  Command Line:\n");
    printf("    --debug-categories=codegen,constantfold\n");
    printf("    --debug-categories=all --no-debug-colors\n");
    printf("    --debug-categories=compiler --debug-timestamps\n\n");
    
    printf("  Environment Variables:\n");
    printf("    ORUS_DEBUG=codegen,constantfold\n");
    printf("    ORUS_DEBUG=all ORUS_DEBUG_COLORS=0\n");
    printf("    ORUS_DEBUG=compiler ORUS_DEBUG_TIMESTAMPS=1\n\n");
    
    printf("Debug Verbosity Levels:\n");
    printf("  0 - Minimal output (errors only)\n");
    printf("  1 - Normal output (default)\n");
    printf("  2 - Verbose output\n");
    printf("  3 - Very verbose output\n\n");
    
    printf("Note: Command-line options override environment variables.\n");
}

void config_apply_debug_settings(const OrusConfig* config) {
    if (!config) return;
    
    // Apply debug categories from command line
    if (config->debug_categories) {
        unsigned int categories = debug_parse_categories(config->debug_categories);
        debug_set_categories(categories);
    }
    
    // Apply debug colors setting
    debug_set_colors(config->debug_colors);
    
    // Apply debug timestamps setting
    debug_set_timestamps(config->debug_timestamps);
    
    // Apply debug file location setting
    debug_set_file_location(config->debug_file_location);
    
    // Apply debug verbosity setting
    debug_set_verbosity(config->debug_verbosity);
}

void config_print_version(void) {
    printf("Orus Language Interpreter v%s\n", ORUS_VERSION_STRING);
    printf("Built with comprehensive configuration system\n");
    printf("Register-based virtual machine with garbage collection\n");
    
    #ifdef USE_COMPUTED_GOTO
        printf("Dispatch Mode: Computed Goto (optimized)\n");
    #else
        printf("Dispatch Mode: Switch-based (portable)\n");
    #endif
    
    printf("\nConfiguration Features:\n");
    printf("  - Multi-source configuration (CLI, env vars, files)\n");
    printf("  - Runtime VM tuning\n");
    printf("  - Development tool integration\n");
    printf("  - Memory debugging support\n");
}

void config_print_current(const OrusConfig* config) {
    if (!config) {
        printf("No configuration loaded\n");
        return;
    }
    
    printf("Current Orus Configuration:\n");
    printf("============================\n");
    
    printf("Runtime Behavior:\n");
    printf("  Trace Execution: %s\n", config->trace_execution ? "enabled" : "disabled");
    printf("  Trace Typed Fallbacks: %s\n", config->trace_typed_fallbacks ? "enabled" : "disabled");
    printf("  Debug Mode: %s\n", config->debug_mode ? "enabled" : "disabled");
    printf("  Verbose: %s\n", config->verbose ? "enabled" : "disabled");
    printf("  Quiet: %s\n", config->quiet ? "enabled" : "disabled");
    printf("  REPL Mode: %s\n", config->repl_mode ? "enabled" : "disabled");
    
    printf("\nVM Configuration:\n");
    printf("  Max Recursion Depth: %u\n", config->max_recursion_depth);
    printf("  Register Count: %u\n", config->register_count);
    printf("  Stack Size: %u bytes (%.1f MB)\n", config->stack_size, config->stack_size / (1024.0 * 1024.0));
    printf("  Heap Size: %u bytes (%.1f MB)\n", config->heap_size, config->heap_size / (1024.0 * 1024.0));
    
    printf("\nGarbage Collection:\n");
    printf("  GC Enabled: %s\n", config->gc_enabled ? "yes" : "no");
    printf("  GC Threshold: %u bytes (%.1f MB)\n", config->gc_threshold, config->gc_threshold / (1024.0 * 1024.0));
    printf("  GC Strategy: %s\n", config->gc_strategy);
    
    printf("\nError Reporting:\n");
    printf("  Error Format: %s\n", config->error_format);
    printf("  Error Colors: %s\n", config->error_colors ? "enabled" : "disabled");
    printf("  Error Context: %s\n", config->error_context ? "enabled" : "disabled");
    
    printf("\nOptimization:\n");
    printf("  Optimization Level: %u\n", config->optimization_level);
    printf("  Function Inlining: %s\n", config->inline_functions ? "enabled" : "disabled");
    printf("  Dead Code Elimination: %s\n", config->dead_code_elimination ? "enabled" : "disabled");
    
    if (config->input_file) {
        printf("\nInput File: %s\n", config->input_file);
    }
    if (config->output_file) {
        printf("Output File: %s\n", config->output_file);
    }
    if (config->log_file) {
        printf("Log File: %s\n", config->log_file);
    }
}

void config_save_to_file(const OrusConfig* config, const char* filename) {
    if (!config || !filename) return;
    
    FILE* file = fopen(filename, "w");
    if (!file) {
        fprintf(stderr, "Error: Cannot open config file '%s' for writing\n", filename);
        return;
    }
    
    fprintf(file, "# Orus Language Configuration File\n");
    fprintf(file, "# Generated automatically - modify with care\n\n");
    
    fprintf(file, "[runtime]\n");
    fprintf(file, "trace_execution = %s\n", config->trace_execution ? "true" : "false");
    fprintf(file, "trace_typed_fallbacks = %s\n", config->trace_typed_fallbacks ? "true" : "false");
    fprintf(file, "debug_mode = %s\n", config->debug_mode ? "true" : "false");
    fprintf(file, "verbose = %s\n", config->verbose ? "true" : "false");
    fprintf(file, "quiet = %s\n", config->quiet ? "true" : "false");
    
    fprintf(file, "\n[vm]\n");
    fprintf(file, "max_recursion_depth = %u\n", config->max_recursion_depth);
    fprintf(file, "register_count = %u\n", config->register_count);
    fprintf(file, "stack_size = %u\n", config->stack_size);
    fprintf(file, "heap_size = %u\n", config->heap_size);
    
    fprintf(file, "\n[gc]\n");
    fprintf(file, "gc_enabled = %s\n", config->gc_enabled ? "true" : "false");
    fprintf(file, "gc_threshold = %u\n", config->gc_threshold);
    fprintf(file, "gc_strategy = %s\n", config->gc_strategy);
    
    fprintf(file, "\n[errors]\n");
    fprintf(file, "error_format = %s\n", config->error_format);
    fprintf(file, "error_colors = %s\n", config->error_colors ? "true" : "false");
    fprintf(file, "error_context = %s\n", config->error_context ? "true" : "false");
    
    fprintf(file, "\n[optimization]\n");
    fprintf(file, "optimization_level = %u\n", config->optimization_level);
    fprintf(file, "inline_functions = %s\n", config->inline_functions ? "true" : "false");
    fprintf(file, "dead_code_elimination = %s\n", config->dead_code_elimination ? "true" : "false");
    
    fclose(file);
    printf("Configuration saved to %s\n", filename);
}

// Simple configuration file parser
bool config_load_from_file(OrusConfig* config, const char* filename) {
    if (!config || !filename) return false;
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        return false; // File doesn't exist or can't be opened
    }
    
    char line[256];
    char section[64] = "";
    
    while (fgets(line, sizeof(line), file)) {
        // Remove newline and trim whitespace
        char* p = line + strlen(line) - 1;
        while (p >= line && (*p == '\n' || *p == '\r' || *p == ' ' || *p == '\t')) {
            *p-- = '\0';
        }
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') continue;
        
        // Check for section headers
        if (line[0] == '[') {
            size_t len = strlen(line);
            if (len > 1 && line[len - 1] == ']') {
                size_t copy_len = len > 2 ? len - 2 : 0;
                if (copy_len >= sizeof(section)) {
                    copy_len = sizeof(section) - 1;
                }
                memcpy(section, line + 1, copy_len);
                section[copy_len] = '\0';
            } else {
                section[0] = '\0';
            }
            continue;
        }
        
        // Parse key = value pairs
        char* equals = strchr(line, '=');
        if (!equals) continue;
        
        *equals = '\0';
        char* key = line;
        char* value = equals + 1;
        
        // Trim whitespace from key and value
        while (*key == ' ' || *key == '\t') key++;
        while (*value == ' ' || *value == '\t') value++;
        
        // Process based on current section
        if (strcmp(section, "runtime") == 0) {
            if (strcmp(key, "trace_execution") == 0) {
                config->trace_execution = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "trace_typed_fallbacks") == 0) {
                config->trace_typed_fallbacks = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "debug_mode") == 0) {
                config->debug_mode = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "verbose") == 0) {
                config->verbose = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "quiet") == 0) {
                config->quiet = (strcmp(value, "true") == 0);
            }
        } else if (strcmp(section, "vm") == 0) {
            if (strcmp(key, "max_recursion_depth") == 0) {
                config->max_recursion_depth = atoi(value);
            } else if (strcmp(key, "register_count") == 0) {
                config->register_count = atoi(value);
            } else if (strcmp(key, "stack_size") == 0) {
                config->stack_size = atoi(value);
            } else if (strcmp(key, "heap_size") == 0) {
                config->heap_size = atoi(value);
            }
        } else if (strcmp(section, "gc") == 0) {
            if (strcmp(key, "gc_enabled") == 0) {
                config->gc_enabled = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "gc_threshold") == 0) {
                config->gc_threshold = atoi(value);
            }
            // Note: gc_strategy would need special handling for string storage
        } else if (strcmp(section, "optimization") == 0) {
            if (strcmp(key, "optimization_level") == 0) {
                config->optimization_level = atoi(value);
            } else if (strcmp(key, "inline_functions") == 0) {
                config->inline_functions = (strcmp(value, "true") == 0);
            } else if (strcmp(key, "dead_code_elimination") == 0) {
                config->dead_code_elimination = (strcmp(value, "true") == 0);
            }
        }
    }
    
    fclose(file);
    return true;
}
