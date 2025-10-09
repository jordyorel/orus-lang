#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vm/module_manager.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool test_alias_connects_legacy_std_name(void) {
    ModuleManager* manager = create_module_manager();
    ASSERT_TRUE(manager != NULL, "module manager should allocate");

    RegisterModule* canonical = load_module(manager, "intrinsics/math");
    ASSERT_TRUE(canonical != NULL, "canonical module should load");

    uint16_t reg = allocate_module_register(manager, "intrinsics/math");
    ASSERT_TRUE(reg != 0, "canonical module should allocate register");

    ASSERT_TRUE(register_module_export(canonical, "sin", MODULE_EXPORT_KIND_FUNCTION, (int)reg, NULL, "__c_sin"),
                "canonical module should register export");

    ASSERT_TRUE(module_manager_alias_module(manager, "intrinsics/math", "std/math"),
                "alias should register legacy std name");

    ModuleExportKind out_kind = MODULE_EXPORT_KIND_FUNCTION;
    uint16_t out_register = 0;
    Type* out_type = NULL;
    ASSERT_TRUE(module_manager_resolve_export(manager, "std/math", "sin", &out_kind, &out_register, &out_type),
                "legacy std name should resolve export");

    ASSERT_TRUE(out_kind == MODULE_EXPORT_KIND_FUNCTION, "alias should preserve export kind");
    ASSERT_TRUE(out_register == reg, "alias should expose same register id");
    ASSERT_TRUE(out_type == NULL, "test does not attach type metadata");

    RegisterModule* legacy = find_module(manager, "std/math");
    ASSERT_TRUE(legacy == canonical, "alias should point at canonical module");

    free_module_manager(manager);
    return true;
}

static bool test_alias_rejects_unknown_canonical(void) {
    ModuleManager* manager = create_module_manager();
    ASSERT_TRUE(manager != NULL, "module manager should allocate for failure test");

    bool aliased = module_manager_alias_module(manager, "intrinsics/bytes", "std/bytes");
    ASSERT_TRUE(!aliased, "alias should fail when canonical module is missing");

    free_module_manager(manager);
    return true;
}

static bool test_alias_rejects_duplicate_registration(void) {
    ModuleManager* manager = create_module_manager();
    ASSERT_TRUE(manager != NULL, "module manager should allocate for duplicate test");

    RegisterModule* module = load_module(manager, "intrinsics/bytes");
    ASSERT_TRUE(module != NULL, "canonical bytes module should load");

    ASSERT_TRUE(module_manager_alias_module(manager, "intrinsics/bytes", "std/bytes"),
                "first alias registration should succeed");

    ASSERT_TRUE(!module_manager_alias_module(manager, "intrinsics/bytes", "std/bytes"),
                "aliasing same name twice should fail");

    free_module_manager(manager);
    return true;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"alias connects legacy std/math", test_alias_connects_legacy_std_name},
        {"alias rejects missing canonical module", test_alias_rejects_unknown_canonical},
        {"alias rejects duplicate legacy registration", test_alias_rejects_duplicate_registration},
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

    printf("%d/%d module manager legacy alias tests passed\n", passed, total);
    return 0;
}
