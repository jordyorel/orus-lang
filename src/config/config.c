// config.c - Orus Language Configuration System Implementation
#include "config/config.h"
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
    config->show_bytecode = false;
    config->show_tokens = false;
    config->show_optimization_stats = false;
    config->benchmark_mode = false;
    
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
        } else if (strcmp(arg, "--show-bytecode") == 0) {
            config->show_bytecode = true;
        } else if (strcmp(arg, "--show-tokens") == 0) {
            config->show_tokens = true;
        } else if (strcmp(arg, "--show-opt-stats") == 0) {
            config->show_optimization_stats = true;
        } else if (strcmp(arg, "--benchmark") == 0) {
            config->benchmark_mode = true;
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
    
    if (config->optimization_level < 0 || config->optimization_level > 2) {
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
    printf("  -d, --debug             Enable debug mode\n");
    printf("  --verbose               Enable verbose output\n");
    printf("  -q, --quiet             Suppress non-essential output\n");
    printf("\nDevelopment Tools:\n");
    printf("  --show-ast              Show AST dump\n");
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
}

void config_print_version(void) {
    printf("Orus Language Interpreter v1.0.0\n");
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
        if (line[0] == '[' && line[strlen(line)-1] == ']') {
            strncpy(section, line + 1, sizeof(section) - 1);
            section[strlen(section) - 1] = '\0'; // Remove closing ]
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