#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler/compiler.h"
#include "compiler/parser.h"
#include "compiler/typed_ast.h"
#include "compiler/error_reporter.h"
#include "debug/debug_config.h"
#include "type/type.h"
#include "vm/vm.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

typedef struct {
    CompilerContext* ctx;
    TypedASTNode* typed;
    ASTNode* ast;
} CompiledModule;

static bool compile_module_source(const char* source, const char* filename, CompiledModule* out) {
    memset(out, 0, sizeof(*out));

    ASTNode* ast = parseSource(source);
    if (!ast) {
        return false;
    }
    ast->location.file = filename;

    init_type_inference();
    TypeEnv* env = type_env_new(NULL);
    if (!env) {
        cleanup_type_inference();
        freeAST(ast);
        return false;
    }

    TypedASTNode* typed = generate_typed_ast(ast, env);
    if (!typed) {
        cleanup_type_inference();
        freeAST(ast);
        return false;
    }

    CompilerContext* ctx = init_compiler_context(typed);
    if (!ctx) {
        cleanup_type_inference();
        free_typed_ast_node(typed);
        freeAST(ast);
        return false;
    }

    ctx->is_module = true;

    if (!compile_to_bytecode(ctx)) {
        free_compiler_context(ctx);
        free_typed_ast_node(typed);
        freeAST(ast);
        cleanup_type_inference();
        return false;
    }

    out->ctx = ctx;
    out->typed = typed;
    out->ast = ast;
    return true;
}

static void destroy_compiled_module(CompiledModule* module) {
    if (!module) {
        return;
    }
    free_compiler_context(module->ctx);
    free_typed_ast_node(module->typed);
    freeAST(module->ast);
    cleanup_type_inference();
}

static bool test_core_intrinsic_emits_native_call(void) {
    const char* source =
        "@[core(\"__c_sin\")]\n"
        "pub fn sin(x: f64) -> f64\n";

    CompiledModule module;
    if (!compile_module_source(source, "core_intrinsic.orus", &module)) {
        fprintf(stderr, "Failed to compile core intrinsic module\n");
        return false;
    }

    ASSERT_TRUE(module.ctx->module_export_count == 1, "expected exactly one module export");
    ModuleExportEntry* export_entry = &module.ctx->module_exports[0];
    ASSERT_TRUE(export_entry->intrinsic_symbol != NULL, "export should record intrinsic symbol");
    ASSERT_TRUE(strcmp(export_entry->intrinsic_symbol, "__c_sin") == 0,
                "export stored incorrect intrinsic symbol");
    ASSERT_TRUE(export_entry->function_index >= 0, "export missing function index metadata");

    int function_index = export_entry->function_index;
    ASSERT_TRUE(function_index < module.ctx->function_count,
                "function index out of bounds for compiled module");

    BytecodeBuffer* chunk = module.ctx->function_chunks[function_index];
    ASSERT_TRUE(chunk != NULL, "compiled function chunk missing");
    ASSERT_TRUE(chunk->count >= 6, "intrinsic trampoline should contain call and return instructions");
    ASSERT_TRUE(chunk->instructions[0] == OP_CALL_NATIVE_R, "trampoline must call native opcode");
    ASSERT_TRUE(chunk->instructions[5] == OP_RETURN_R, "trampoline must return value register");

    destroy_compiled_module(&module);
    return true;
}

static bool test_fs_intrinsic_emits_native_trampoline(void) {
    const char* source =
        "@[core(\"__fs_read\")]\n"
        "pub fn fs_read(handle: any, count: i64) -> bytes\n";

    CompiledModule module;
    if (!compile_module_source(source, "fs_intrinsic.orus", &module)) {
        fprintf(stderr, "Failed to compile filesystem intrinsic module\n");
        return false;
    }

    ASSERT_TRUE(module.ctx->module_export_count == 1,
                "expected exactly one module export for fs intrinsic");
    ModuleExportEntry* export_entry = &module.ctx->module_exports[0];
    ASSERT_TRUE(export_entry->intrinsic_symbol != NULL,
                "export should record intrinsic symbol");
    ASSERT_TRUE(strcmp(export_entry->intrinsic_symbol, "__fs_read") == 0,
                "export stored incorrect fs intrinsic symbol");
    ASSERT_TRUE(export_entry->function_index >= 0,
                "export missing function index metadata");

    int function_index = export_entry->function_index;
    ASSERT_TRUE(function_index < module.ctx->function_count,
                "function index out of bounds for compiled module");

    BytecodeBuffer* chunk = module.ctx->function_chunks[function_index];
    ASSERT_TRUE(chunk != NULL, "compiled function chunk missing");
    ASSERT_TRUE(chunk->count >= 6,
                "intrinsic trampoline should contain call and return instructions");
    ASSERT_TRUE(chunk->instructions[0] == OP_CALL_NATIVE_R,
                "trampoline must call native opcode");
    ASSERT_TRUE(chunk->instructions[5] == OP_RETURN_R,
                "trampoline must return value register");

    destroy_compiled_module(&module);
    return true;
}

int main(void) {
    debug_init();

    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"core intrinsic codegen emits native trampoline", test_core_intrinsic_emits_native_call},
        {"fs intrinsic codegen emits native trampoline", test_fs_intrinsic_emits_native_trampoline},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; ++i) {
        if (tests[i].fn()) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            return 1;
        }
    }

    printf("%d/%d core intrinsic codegen tests passed\n", passed, total);
    return 0;
}
