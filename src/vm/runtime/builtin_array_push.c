//  Orus Language Project

#include "runtime/builtins.h"
#include "runtime/memory.h"

bool builtin_array_push(Value array_value, Value element) {
    if (!IS_ARRAY(array_value)) {
        return false;
    }

    ObjArray* array = AS_ARRAY(array_value);
    return arrayPush(array, element);
}
