#ifndef MODULES_H
#define MODULES_H

#include "chunk.h"
#include "ast.h"
#include "value.h"
#include "vm.h"
#include <stdbool.h>
#include <time.h>

typedef struct {
    char* name;
    Value value;
    uint8_t index; // Global variable index
} Export;

typedef struct {
    char* module_name; // full path
    char* name;        // base module name
    Chunk* bytecode;
    Export exports[UINT8_COUNT];
    uint8_t export_count;
    bool executed;
    char* disk_path;   // path on disk if loaded from file
    long mtime;        // modification time
    bool from_embedded; // true if loaded from embedded table
} Module;

Export* get_export(Module* module, const char* name);

char* load_module_source(const char* resolved_path);
char* load_module_with_fallback(const char* path, char** disk_path, long* mtime,
                                bool* from_embedded);
ASTNode* parse_module_source(const char* source_code, const char* module_name);
Chunk* compile_module_ast(ASTNode* ast, const char* module_name);
bool register_module(Module* module);
Module* get_module(const char* name);
InterpretResult compile_module_only(const char* path);

extern bool traceImports;

// Holds the last module loading error message if any
extern const char* moduleError;

// Ensure a module is loaded and executed. Returns INTERPRET_OK on success.
InterpretResult interpret_module(const char* path);

#endif
