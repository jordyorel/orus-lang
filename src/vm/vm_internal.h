// vm_internal.h - Shared internal helpers for the VM
#ifndef VM_INTERNAL_H
#define VM_INTERNAL_H

#include "vm.h"
#include "vm_constants.h"
#include "common.h"

#define VM_ERROR_RETURN(type, loc, msg, ...) \
    do { \
        runtimeError(type, loc, msg, ##__VA_ARGS__); \
        RETURN(INTERPRET_RUNTIME_ERROR); \
    } while (0)

#define VM_TYPE_CHECK(cond, msg) \
    do { \
        if (unlikely(!(cond))) { \
            VM_ERROR_RETURN(ERROR_TYPE, CURRENT_LOCATION(), msg); \
        } \
    } while (0)

#define VM_BOUNDS_CHECK(index, limit, name) \
    do { \
        if (unlikely((index) >= (limit))) { \
            VM_ERROR_RETURN(ERROR_RUNTIME, CURRENT_LOCATION(), \
                           "%s index %d out of bounds (limit: %d)", name, index, limit); \
        } \
    } while (0)

#endif // VM_INTERNAL_H
