// vm_internal.h - Shared internal helpers for the VM
#ifndef VM_INTERNAL_H
#define VM_INTERNAL_H

#include "vm/vm.h"
#include "vm/vm_constants.h"
#include "public/common.h"
#include "vm/vm_validation.h"

// runtimeError is implemented in vm.c and used by validation helpers.
// Declare it here so any internal VM module including this header has
// access to the prototype without needing the dispatch headers.
void runtimeError(ErrorType type, SrcLocation location, const char* format, ...);

#define CURRENT_LOCATION() ((SrcLocation){vm.filePath, vm.currentLine, vm.currentColumn})

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
