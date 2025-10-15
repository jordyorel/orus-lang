#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "vm/vm.h"
#include "vm/vm_string_ops.h"
#include "vm/vm_tagged_union.h"
#include "runtime/memory.h"

#define ASSERT_TRUE(cond, message)                                                         \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            fprintf(stderr, "Assertion failed: %s (%s:%d)\n", message, __FILE__, __LINE__); \
            return false;                                                                  \
        }                                                                                  \
    } while (0)

static bool test_vm_result_ok_builds_enum(void) {
    initVM();

    Value inner = I32_VAL(123);
    Value out = BOOL_VAL(false);

    bool ok = vm_result_ok(inner, &out);
    ASSERT_TRUE(ok, "Result.Ok should succeed");
    ASSERT_TRUE(IS_ENUM(out), "Result.Ok should produce enum value");

    ObjEnumInstance* instance = AS_ENUM(out);
    ASSERT_TRUE(instance != NULL, "Result.Ok instance should be non-null");
    ASSERT_TRUE(instance->typeName != NULL, "Result.Ok should set type name");
    ASSERT_TRUE(strcmp(instance->typeName->chars, "Result") == 0,
                "Result.Ok should intern the 'Result' type name");
    ASSERT_TRUE(instance->variantName != NULL, "Result.Ok should set variant name");
    ASSERT_TRUE(strcmp(instance->variantName->chars, "Ok") == 0,
                "Result.Ok should intern the 'Ok' variant name");
    ASSERT_TRUE(instance->variantIndex == 0, "Result.Ok should use variant index 0");
    ASSERT_TRUE(instance->payload != NULL, "Result.Ok should allocate payload array");
    ASSERT_TRUE(instance->payload->length == 1, "Result.Ok payload length should be 1");

    Value stored = instance->payload->elements[0];
    ASSERT_TRUE(IS_I32(stored), "Result.Ok payload should preserve value type");
    ASSERT_TRUE(AS_I32(stored) == 123, "Result.Ok payload should preserve value contents");

    freeVM();
    return true;
}

static bool test_vm_result_err_builds_enum(void) {
    initVM();

    ObjString* message = intern_string("boom", 4);
    ASSERT_TRUE(message != NULL, "String interning should succeed");
    Value error = STRING_VAL(message);
    Value out = BOOL_VAL(false);

    bool ok = vm_result_err(error, &out);
    ASSERT_TRUE(ok, "Result.Err should succeed");
    ASSERT_TRUE(IS_ENUM(out), "Result.Err should produce enum value");

    ObjEnumInstance* instance = AS_ENUM(out);
    ASSERT_TRUE(instance != NULL, "Result.Err instance should be non-null");
    ASSERT_TRUE(instance->typeName != NULL, "Result.Err should set type name");
    ASSERT_TRUE(strcmp(instance->typeName->chars, "Result") == 0,
                "Result.Err should intern the 'Result' type name");
    ASSERT_TRUE(instance->variantName != NULL, "Result.Err should set variant name");
    ASSERT_TRUE(strcmp(instance->variantName->chars, "Err") == 0,
                "Result.Err should intern the 'Err' variant name");
    ASSERT_TRUE(instance->variantIndex == 1, "Result.Err should use variant index 1");
    ASSERT_TRUE(instance->payload != NULL, "Result.Err should allocate payload array");
    ASSERT_TRUE(instance->payload->length == 1, "Result.Err payload length should be 1");

    Value stored = instance->payload->elements[0];
    ASSERT_TRUE(IS_STRING(stored), "Result.Err payload should preserve error type");
    ASSERT_TRUE(AS_STRING(stored) == message, "Result.Err payload should reference provided error");

    freeVM();
    return true;
}

static bool test_vm_make_tagged_union_allows_empty_payload(void) {
    initVM();

    TaggedUnionSpec spec = {
        .type_name = "Ping",
        .variant_name = NULL,
        .variant_index = 7,
        .payload = NULL,
        .payload_count = 0,
    };

    Value out = BOOL_VAL(true);
    bool ok = vm_make_tagged_union(&spec, &out);
    ASSERT_TRUE(ok, "Tagged union creation without payload should succeed");
    ASSERT_TRUE(IS_ENUM(out), "Tagged union without payload should be enum value");

    ObjEnumInstance* instance = AS_ENUM(out);
    ASSERT_TRUE(instance != NULL, "Tagged union instance should be non-null");
    ASSERT_TRUE(instance->typeName != NULL, "Tagged union should intern type name");
    ASSERT_TRUE(strcmp(instance->typeName->chars, "Ping") == 0,
                "Tagged union should preserve provided type name");
    ASSERT_TRUE(instance->variantName == NULL, "Tagged union should allow missing variant name");
    ASSERT_TRUE(instance->variantIndex == 7, "Tagged union should preserve variant index");
    ASSERT_TRUE(instance->payload == NULL, "Tagged union without payload should not allocate array");

    freeVM();
    return true;
}

static bool test_vm_make_tagged_union_survives_gc_pressure(void) {
    initVM();

    size_t previous_threshold = gcThreshold;
    gcThreshold = 1; // Force a GC safepoint on the next allocation-heavy operation.

    Value inner = I32_VAL(42);
    Value out = BOOL_VAL(false);
    bool ok = vm_result_ok(inner, &out);

    gcThreshold = previous_threshold;

    ASSERT_TRUE(ok, "Result.Ok should succeed even under GC pressure");
    ASSERT_TRUE(IS_ENUM(out), "Result.Ok under GC pressure should produce enum value");

    ObjEnumInstance* instance = AS_ENUM(out);
    ASSERT_TRUE(instance != NULL, "Result.Ok under GC pressure should return a valid instance");
    ASSERT_TRUE(instance->payload != NULL, "Result.Ok under GC pressure should keep payload alive");
    ASSERT_TRUE(instance->payload->length == 1,
                "Result.Ok under GC pressure should preserve payload length");
    ASSERT_TRUE(IS_I32(instance->payload->elements[0]) &&
                    AS_I32(instance->payload->elements[0]) == 42,
                "Result.Ok under GC pressure should preserve payload contents");

    freeVM();
    return true;
}

static bool test_vm_make_tagged_union_requires_payload_pointer(void) {
    initVM();

    TaggedUnionSpec spec = {
        .type_name = "Result",
        .variant_name = "Ok",
        .variant_index = 0,
        .payload = NULL,
        .payload_count = 1,
    };

    Value sentinel = BOOL_VAL(true);
    bool ok = vm_make_tagged_union(&spec, &sentinel);
    ASSERT_TRUE(!ok, "Tagged union should fail when payload pointer is missing");
    ASSERT_TRUE(IS_BOOL(sentinel) && AS_BOOL(sentinel),
                "Tagged union failure should leave output value untouched");

    freeVM();
    return true;
}

static bool test_vm_make_tagged_union_requires_type_name(void) {
    initVM();

    TaggedUnionSpec spec = {
        .type_name = NULL,
        .variant_name = "Nothing",
        .variant_index = 3,
        .payload = NULL,
        .payload_count = 0,
    };

    Value sentinel = BOOL_VAL(false);
    bool ok = vm_make_tagged_union(&spec, &sentinel);
    ASSERT_TRUE(!ok, "Tagged union should fail without a type name");
    ASSERT_TRUE(IS_BOOL(sentinel) && !AS_BOOL(sentinel),
                "Tagged union failure should keep sentinel value");

    freeVM();
    return true;
}

int main(void) {
    bool (*tests[])(void) = {
        test_vm_result_ok_builds_enum,
        test_vm_result_err_builds_enum,
        test_vm_make_tagged_union_allows_empty_payload,
        test_vm_make_tagged_union_survives_gc_pressure,
        test_vm_make_tagged_union_requires_payload_pointer,
        test_vm_make_tagged_union_requires_type_name,
    };

    const char* names[] = {
        "Result.Ok wraps payload",
        "Result.Err wraps payload",
        "Tagged union supports empty payload",
        "Tagged union handles GC pressure",
        "Tagged union validates payload pointer",
        "Tagged union validates type name",
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        if (tests[i]()) {
            printf("[PASS] %s\n", names[i]);
            passed++;
        } else {
            printf("[FAIL] %s\n", names[i]);
            return 1;
        }
    }

    printf("%d/%d tagged union tests passed\n", passed, total);
    return 0;
}
