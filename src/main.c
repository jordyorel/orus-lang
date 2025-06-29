// Orus Language Main Interpreter
// This is the main entry point for the Orus programming language

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vm.h"
#include "common.h"
#include "compiler.h"

static void repl() {
    char line[1024];
    
    printf("Orus Language Interpreter v0.1.0\n");
    printf("Type 'exit' to quit.\n\n");
    
    for (;;) {
        printf("orus> ");
        
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        
        if (strcmp(line, "exit") == 0) {
            break;
        }
        
        if (strlen(line) == 0) {
            continue;
        }
        
        interpret(line);
    }
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }
    
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }
    
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    if (bytesRead < fileSize) {
        fprintf(stderr, "Could not read file \"%s\".\n", path);
        exit(74);
    }
    
    buffer[bytesRead] = '\0';
    
    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    InterpretResult result = interpret(source);
    free(source);
    
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
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
    printf("Orus Language Interpreter v0.1.0\n");
    printf("Built with register-based virtual machine\n");
}

int main(int argc, const char* argv[]) {
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
            return 64;
        } else {
            if (fileName != NULL) {
                fprintf(stderr, "Too many arguments.\n");
                showUsage(argv[0]);
                return 64;
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
