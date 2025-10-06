#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "runtime/memory.h"
#include "vm/register_file.h"
#include "vm/vm.h"

static bool test_gc_marks_spilled_registers(void) {
    initVM();

    const char* text = "spilled-root-value";
    ObjString* str = allocateString(text, (int)strlen(text));
    uint16_t spill_id = allocate_spilled_register(&vm.register_file, STRING_VAL(str));

    bool found_before = false;
    for (Obj* obj = vm.objects; obj != NULL; obj = obj->next) {
        if (obj == (Obj*)str) {
            found_before = true;
            break;
        }
    }

    collectGarbage();

    bool found_after = false;
    for (Obj* obj = vm.objects; obj != NULL; obj = obj->next) {
        if (obj == (Obj*)str) {
            found_after = true;
            break;
        }
    }

    Value* slot = get_register(&vm.register_file, spill_id);
    Value retrieved = slot ? *slot : BOOL_VAL(false);

    bool success = found_before && found_after && slot != NULL && IS_STRING(retrieved) && AS_STRING(retrieved) == str;

    if (!success) {
        fprintf(stderr,
                "GC spill root regression failed: before=%d after=%d slot=%p type=%d\n",
                found_before,
                found_after,
                (void*)slot,
                retrieved.type);
    }

    freeVM();
    return success;
}

int main(void) {
    struct {
        const char* name;
        bool (*fn)(void);
    } tests[] = {
        {"GC preserves spilled registers", test_gc_marks_spilled_registers},
    };

    int passed = 0;
    int total = (int)(sizeof(tests) / sizeof(tests[0]));

    for (int i = 0; i < total; i++) {
        bool ok = tests[i].fn();
        if (ok) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
        }
    }

    printf("%d/%d Spill GC tests passed\n", passed, total);
    return passed == total ? 0 : 1;
}
