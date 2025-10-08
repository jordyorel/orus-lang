#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm/vm.h"
#include "vm/module_manager.h"
#include "vm/vm_string_ops.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool native_table_contains(const char* symbol, int* out_index) {
    if (!symbol || !out_index) {
        return false;
    }

    size_t target_len = strlen(symbol);
    for (int i = 0; i < vm.nativeFunctionCount; ++i) {
        NativeFunction* entry = &vm.nativeFunctions[i];
        const char* name_chars = entry->name ? string_get_chars(entry->name) : NULL;
        if (!name_chars) {
            continue;
        }
        if (strncmp(name_chars, symbol, target_len) == 0 && entry->name->length == (int)target_len) {
            *out_index = i;
            return true;
        }
    }
    return false;
}

static bool module_records_intrinsic(const char* module_name, const char* export_name, const char* intrinsic) {
    RegisterModule* module = find_module(vm.register_file.module_manager, module_name);
    if (!module || !module->exports.export_count) {
        return false;
    }

    for (uint16_t i = 0; i < module->exports.export_count; ++i) {
        const char* name = module->exports.exported_names ? module->exports.exported_names[i] : NULL;
        if (!name || strcmp(name, export_name) != 0) {
            continue;
        }
        const char* recorded = module->exports.exported_intrinsics ? module->exports.exported_intrinsics[i] : NULL;
        return recorded && strcmp(recorded, intrinsic) == 0;
    }

    return false;
}

static bool test_module_binds_core_intrinsic_to_native(void) {
    const char* module_path = "tests/unit/fixtures/core_intrinsic_module.orus";
    const char* module_name = "core_intrinsic_module";
    const char* intrinsic_symbol = "__c_sin";

    initVM();

    InterpretResult result = interpret_module(module_path, module_name);
    if (result != INTERPRET_OK) {
        fprintf(stderr, "interpret_module failed with result %d\n", result);
        freeVM();
        return false;
    }

    int native_index = -1;
    ASSERT_TRUE(native_table_contains(intrinsic_symbol, &native_index),
                "intrinsic should be present in native function table");

    bool patched = false;
    for (int i = 0; i < vm.functionCount; ++i) {
        Function* fn = &vm.functions[i];
        if (!fn->chunk || !fn->chunk->code || fn->chunk->count < 5) {
            continue;
        }
        if (fn->chunk->code[0] == OP_CALL_NATIVE_R && fn->chunk->code[1] == (uint8_t)native_index) {
            patched = true;
            break;
        }
    }
    ASSERT_TRUE(patched, "compiled module function should call the bound native index");

    ASSERT_TRUE(module_records_intrinsic(module_name, "sin", intrinsic_symbol),
                "module manager should remember intrinsic symbol for export");

    freeVM();
    return true;
}

static bool test_module_binds_fs_intrinsic_to_native(void) {
    const char* module_path = "tests/unit/fixtures/core_fs_intrinsic_module.orus";
    const char* module_name = "core_fs_intrinsic_module";
    const char* intrinsic_symbol = "__fs_open";

    initVM();

    InterpretResult result = interpret_module(module_path, module_name);
    if (result != INTERPRET_OK) {
        fprintf(stderr, "interpret_module failed with result %d\n", result);
        freeVM();
        return false;
    }

    int native_index = -1;
    ASSERT_TRUE(native_table_contains(intrinsic_symbol, &native_index),
                "fs intrinsic should be present in native function table");

    bool patched = false;
    for (int i = 0; i < vm.functionCount; ++i) {
        Function* fn = &vm.functions[i];
        if (!fn->chunk || !fn->chunk->code || fn->chunk->count < 5) {
            continue;
        }
        if (fn->chunk->code[0] == OP_CALL_NATIVE_R && fn->chunk->code[1] == (uint8_t)native_index) {
            patched = true;
            break;
        }
    }
    ASSERT_TRUE(patched, "compiled module function should call the bound fs native index");

    ASSERT_TRUE(module_records_intrinsic(module_name, "open_file", intrinsic_symbol),
                "module manager should remember fs intrinsic symbol for export");

    freeVM();
    return true;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"module binds core intrinsic to native table", test_module_binds_core_intrinsic_to_native},
        {"module binds fs intrinsic to native table", test_module_binds_fs_intrinsic_to_native},
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

    printf("%d/%d core intrinsic runtime tests passed\n", passed, total);
    return 0;
}
