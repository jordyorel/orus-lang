// Orus Language Main Interpreter
// This is the main entry point for the Orus programming language

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vm.h"
#include "common.h"
#include "error_reporting.h"
#include "errors/error_interface.h"
#include "errors/features/type_errors.h"
#include "compiler.h"
#include "repl.h"
#include "version.h"


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

static void showUsage(const char* program) {
    printf("Usage: %s [options] [file]\n", program);
    printf("Options:\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -v, --version  Show version information\n");
    printf("  -t, --trace    Enable execution tracing\n");
    printf("  -d, --debug    Enable debug mode\n");
    printf("\n");
    printf("If no file is provided, starts interactive REPL mode.\n");
}

static void showVersion() {
    printf("Orus Language Interpreter v%s\n", ORUS_VERSION_STRING);
    printf("Built with register-based virtual machine\n");
    
    // ✅ Phase 4: Add diagnostic output for dispatch mode
    #ifdef USE_COMPUTED_GOTO
        printf("Dispatch Mode: Computed Goto (fast)\n");
    #else
        printf("Dispatch Mode: Switch-based (portable)\n");
    #endif
}

int main(int argc, const char* argv[]) {
    // Initialize error reporting system
    init_error_reporting();
    
    // Initialize feature-based error system
    init_feature_errors();
    init_type_errors();
    
    bool traceExecution = false;
    bool debugMode = false;
    const char* fileName = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            showUsage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            showVersion();
            return 0;
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--trace") == 0) {
            traceExecution = true;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            debugMode = true;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            showUsage(argv[0]);
            return EXIT_USAGE_ERROR;
        } else {
            if (fileName != NULL) {
                fprintf(stderr, "Too many arguments.\n");
                showUsage(argv[0]);
                return EXIT_USAGE_ERROR;
            }
            fileName = argv[i];
        }
    }
    
    // Initialize VM
    initVM();
    
    // Set VM options
    if (traceExecution) {
        vm.trace = true;
    }
    
    if (debugMode) {
        vm.devMode = true;
    }
    
    // Run file or REPL
    if (fileName == NULL) {
        repl();
    } else {
        runFile(fileName);
    }
    
    freeVM();
    return 0;
}
