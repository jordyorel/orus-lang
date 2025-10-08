// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/runtime/core_fs_handles.c
// Author: Orus Contributors
// Description: Runtime helpers for managing native filesystem handles wrapped by the VM.

#include "runtime/core_fs_handles.h"

#include <string.h>

#include "runtime/memory.h"
#include "vm/vm_string_ops.h"

Value vm_file_wrap_handle(FILE* handle, ObjString* path, bool owns_handle) {
    ObjFile* file = allocateFileHandle(handle, path, owns_handle);
    return FILE_VAL(file);
}

Value vm_file_wrap_handle_cstr(FILE* handle, const char* path, bool owns_handle) {
    ObjString* path_string = NULL;
    if (path) {
        size_t length = strlen(path);
        path_string = allocateString(path, (int)length);
    }
    return vm_file_wrap_handle(handle, path_string, owns_handle);
}

ObjFile* vm_file_from_value(Value value) {
    if (!IS_FILE(value)) {
        return NULL;
    }
    return AS_FILE(value);
}

FILE* vm_file_borrow_handle(Value value) {
    ObjFile* file = vm_file_from_value(value);
    if (!file || file->isClosed) {
        return NULL;
    }
    return file->handle;
}

bool vm_file_close_object(ObjFile* file) {
    if (!file) {
        return false;
    }

    if (file->isClosed) {
        return true;
    }

    int close_result = 0;
    if (file->handle && file->ownsHandle) {
        close_result = fclose(file->handle);
    }

    file->handle = NULL;
    file->isClosed = true;
    file->ownsHandle = false;
    return close_result == 0;
}

bool vm_file_close_value(Value value) {
    ObjFile* file = vm_file_from_value(value);
    if (!file) {
        return false;
    }
    return vm_file_close_object(file);
}

bool vm_file_value_is_open(Value value) {
    ObjFile* file = vm_file_from_value(value);
    if (!file) {
        return false;
    }
    return !file->isClosed && file->handle != NULL;
}

const char* vm_file_value_path(Value value) {
    ObjFile* file = vm_file_from_value(value);
    if (!file || !file->path) {
        return NULL;
    }
    return string_get_chars(file->path);
}
