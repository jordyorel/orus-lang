// Orus Language Main Interpreter
// This is the main entry point for the Orus programming language

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vm/vm.h"
#include "public/common.h"
#include "internal/error_reporting.h"
#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "errors/features/variable_errors.h"
#include "compiler/compiler.h"
#include "compiler/loop_optimization.h"
#include "tools/repl.h"
#include "public/version.h"
#include "config/config.h"


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

static void runFile(const char* path) {
    char* source = readFile(path);
    if (source == NULL) {
        // readFile already prints an error message when it fails
        exit(65);
    }
    
    // Set the file path for better error reporting
    vm.filePath = path;
    
    // Initialize error reporting with arena allocation
    ErrorReportResult error_init = init_error_reporting();
    if (error_init != ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "Failed to initialize error reporting\n");
        free(source);
        exit(70);
    }
    
    // Set source text for error reporting with bounds checking
    size_t source_len = strlen(source);
    ErrorReportResult source_result = set_source_text(source, source_len);
    if (source_result != ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "Failed to set source text for error reporting\n");
        cleanup_error_reporting();
        free(source);
        exit(70);
    }
    
    InterpretResult result = interpret(source);
    
    // Clean up error reporting before freeing source
    cleanup_error_reporting();
    free(source);
    vm.filePath = NULL;
    
    if (result == INTERPRET_COMPILE_ERROR) {
        fprintf(stderr, "Compilation failed for \"%s\".\n", path);
        if (vm.devMode) {
            fprintf(stderr, "Debug: Check if the syntax is supported and tokens are properly recognized.\n");
            fprintf(stderr, "Debug: Try running with simpler syntax to isolate the issue.\n");
        }
        exit(65);
    }
    if (result == INTERPRET_RUNTIME_ERROR) {
        // Enhanced error reporting is now handled in runtimeError() function
        exit(70);
    }
}

// Note: showUsage and showVersion functions are now handled by the configuration system
// See config_print_help() and config_print_version() in src/config/config.c

int main(int argc, const char* argv[]) {
    // Initialize error reporting system
    init_error_reporting();
    
    // Initialize feature-based error system
    init_feature_errors();
    init_type_errors();
    init_variable_errors();
    
    // Create and initialize configuration
    OrusConfig* config = config_create();
    if (!config) {
        fprintf(stderr, "Error: Failed to create configuration\n");
        return EXIT_USAGE_ERROR;
    }
    
    // Load configuration from environment variables
    config_load_from_env(config);
    
    // Load configuration file if specified in environment
    const char* config_file = getenv(ORUS_CONFIG_FILE);
    if (config_file) {
        config_load_from_file(config, config_file);
    }
    
    // Parse command line arguments (highest precedence)
    if (!config_parse_args(config, argc, argv)) {
        // config_parse_args returns false for help/version or errors
        config_destroy(config);
        return 0; // Help/version shown, normal exit
    }
    
    // Validate configuration
    if (!config_validate(config)) {
        config_print_errors(config);
        config_destroy(config);
        return EXIT_USAGE_ERROR;
    }
    
    // Set global configuration for access by other modules
    config_set_global(config);
    
    // Load configuration file if specified via command line
    if (config->config_file && config->config_file != config_file) {
        config_load_from_file(config, config->config_file);
        // Re-validate after loading config file
        if (!config_validate(config)) {
            config_print_errors(config);
            config_destroy(config);
            return EXIT_USAGE_ERROR;
        }
    }
    
    // Initialize VM with configuration
    initVM();
    
    // Apply configuration to VM
    vm.trace = config->trace_execution;
    vm.devMode = config->debug_mode;
    
    // Apply VM configuration settings
    // Note: In a real implementation, we'd need to modify the VM to accept these parameters
    // For now, we'll just set the trace and debug flags
    
    if (config->verbose && !config->quiet) {
        printf("Orus Language Interpreter starting...\n");
        if (config->show_ast || config->show_bytecode || config->show_tokens || config->show_optimization_stats) {
            printf("Development tools enabled: ");
            if (config->show_ast) printf("AST ");
            if (config->show_bytecode) printf("Bytecode ");
            if (config->show_tokens) printf("Tokens ");
            if (config->show_optimization_stats) printf("OptStats ");
            printf("\n");
        }
    }
    
    // Handle special development modes
    if (config->benchmark_mode && !config->quiet) {
        printf("Benchmark mode enabled\n");
    }
    
    // Run file or REPL based on configuration
    if (config->repl_mode) {
        if (!config->quiet) {
            printf("Starting REPL mode...\n");
        }
        repl();
    } else {
        runFile(config->input_file);
    }
    
    // Show optimization statistics if requested
    if (config->show_optimization_stats && !config->quiet) {
        printGlobalOptimizationStats();
    }
    
    // Cleanup
    freeVM();
    config_destroy(config);
    return 0;
}
