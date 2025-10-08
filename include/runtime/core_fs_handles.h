// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/runtime/core_fs_handles.h
// Author: Orus Contributors
// Description: Helpers for wrapping native filesystem handles with GC-managed VM objects.

#ifndef ORUS_RUNTIME_CORE_FS_HANDLES_H
#define ORUS_RUNTIME_CORE_FS_HANDLES_H

#include <stdbool.h>
#include <stdio.h>

#include "vm/vm.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Wrap a native FILE handle using an existing path string reference.
 *
 * The VM takes ownership when {@code owns_handle} is true and will close the
 * handle when the object is finalised or explicitly closed via the helper
 * functions in this module.
 */
Value vm_file_wrap_handle(FILE* handle, ObjString* path, bool owns_handle);

/**
 * Wrap a native FILE handle using a raw C string describing the path.
 * A copy of the path will be stored in the resulting object when provided.
 */
Value vm_file_wrap_handle_cstr(FILE* handle, const char* path, bool owns_handle);

/**
 * Borrow the underlying ObjFile instance from a Value. Returns NULL for
 * non-file values.
 */
ObjFile* vm_file_from_value(Value value);

/**
 * Borrow the native FILE* from a Value when the handle remains open.
 * Returns NULL when the value is not a file or the handle has been closed.
 */
FILE* vm_file_borrow_handle(Value value);

/**
 * Close the FILE handle owned by the provided object when applicable.
 * Returns true when the object exists and is either already closed or was
 * closed successfully by this call.
 */
bool vm_file_close_object(ObjFile* file);

/**
 * Close the FILE handle contained in the Value when possible.
 */
bool vm_file_close_value(Value value);

/**
 * Query whether the file value still has a live handle.
 */
bool vm_file_value_is_open(Value value);

/**
 * Obtain the stored path for diagnostics. Returns NULL when unavailable.
 */
const char* vm_file_value_path(Value value);

#ifdef __cplusplus
}
#endif

#endif /* ORUS_RUNTIME_CORE_FS_HANDLES_H */
