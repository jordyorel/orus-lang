//  Orus Language Project

#include "runtime/builtins.h"
#include "runtime/memory.h"

bool builtin_array_pop(Value array_value, Value* out_value) {
    if (!IS_ARRAY(array_value)) {
        return false;
    }

    ObjArray* array = AS_ARRAY(array_value);
    return arrayPop(array, out_value);
}
