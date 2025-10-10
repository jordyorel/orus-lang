// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_backend.c
// Description: DynASM-backed JIT backend bootstrap providing minimal native
//              entry compilation for the Orus VM tiering roadmap.

#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/jit_layout.h"
#include "vm/vm_tiering.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "dasm_proto.h"
#include "dasm_x86.h"
#define ORUS_JIT_HAS_DYNASM_X86 1
#else
#define ORUS_JIT_HAS_DYNASM_X86 0
#endif

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <pthread.h>
#endif
#endif

#ifndef DASM_CHECKS
#define DASM_CHECKS 1
#endif

#ifndef ORUS_JIT_MAX_SECTIONS
#define ORUS_JIT_MAX_SECTIONS 1
#endif

#if defined(__APPLE__) && defined(__aarch64__) && defined(MAP_JIT)
#define ORUS_JIT_USE_APPLE_JIT 1
#else
#define ORUS_JIT_USE_APPLE_JIT 0
#endif

struct OrusJitBackend {
    size_t page_size;
    bool available;
};

#if ORUS_JIT_HAS_DYNASM_X86
enum { ORUS_JIT_SECTION_CODE = 0 };
#endif

static size_t
orus_jit_detect_page_size(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
#else
    long value = sysconf(_SC_PAGESIZE);
    if (value <= 0) {
        return 4096u;
    }
    return (size_t)value;
#endif
}

static size_t
align_up(size_t value, size_t alignment) {
    if (alignment == 0) {
        return value;
    }
    const size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

static void*
orus_jit_alloc_executable(size_t size, size_t page_size, size_t* out_capacity) {
    if (!size) {
        return NULL;
    }

    size_t capacity = align_up(size, page_size ? page_size : orus_jit_detect_page_size());

#ifdef _WIN32
    void* buffer = VirtualAlloc(NULL, capacity, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!buffer) {
        return NULL;
    }
#else
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if ORUS_JIT_USE_APPLE_JIT
    flags |= MAP_JIT;
#endif

    void* buffer = mmap(NULL, capacity, prot, flags, -1, 0);
    if (buffer == MAP_FAILED) {
        return NULL;
    }
#endif

    if (out_capacity) {
        *out_capacity = capacity;
    }
    return buffer;
}

#if ORUS_JIT_USE_APPLE_JIT
static inline void
orus_jit_set_write_protection(bool enable) {
    pthread_jit_write_protect_np(enable);
}
#else
static inline void
orus_jit_set_write_protection(bool enable) {
    (void)enable;
}
#endif

#if !defined(_WIN32)
static bool
orus_jit_make_executable(void* ptr, size_t size) {
    if (!ptr || !size) {
        return false;
    }
    orus_jit_set_write_protection(false);
    int result = mprotect(ptr, size, PROT_READ | PROT_EXEC);
    orus_jit_set_write_protection(true);
    return result == 0;
}
#endif

static void
orus_jit_release_executable(void* ptr, size_t capacity) {
    if (!ptr || !capacity) {
        return;
    }
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, capacity);
#endif
}

static void
orus_jit_flush_icache(void* ptr, size_t size) {
    if (!ptr || size == 0) {
        return;
    }
#if defined(__GNUC__) || defined(__clang__)
    __builtin___clear_cache((char*)ptr, (char*)ptr + size);
#elif defined(_MSC_VER)
    FlushInstructionCache(GetCurrentProcess(), ptr, size);
#else
    (void)ptr;
    (void)size;
#endif
}

bool
orus_jit_backend_is_available(void) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

struct OrusJitBackend*
orus_jit_backend_create(void) {
    struct OrusJitBackend* backend =
        (struct OrusJitBackend*)calloc(1, sizeof(struct OrusJitBackend));
    if (!backend) {
        return NULL;
    }

    backend->page_size = orus_jit_detect_page_size();
    backend->available = orus_jit_backend_is_available();
    return backend;
}

void
orus_jit_backend_destroy(struct OrusJitBackend* backend) {
    if (!backend) {
        return;
    }
    free(backend);
}

#if ORUS_JIT_HAS_DYNASM_X86
struct DynAsmActionBuffer {
    unsigned char* data;
    size_t size;
    size_t capacity;
};

static void
dynasm_action_buffer_init(struct DynAsmActionBuffer* buffer) {
    if (!buffer) {
        return;
    }
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

static void
dynasm_action_buffer_release(struct DynAsmActionBuffer* buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0;
    buffer->capacity = 0;
}

static bool
dynasm_action_buffer_reserve(struct DynAsmActionBuffer* buffer, size_t additional) {
    if (!buffer) {
        return false;
    }
    if (additional > SIZE_MAX - buffer->size) {
        return false;
    }
    size_t required = buffer->size + additional;
    if (required <= buffer->capacity) {
        return true;
    }
    size_t new_capacity = buffer->capacity ? buffer->capacity : 16u;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }
    unsigned char* data =
        (unsigned char*)realloc(buffer->data, new_capacity * sizeof(unsigned char));
    if (!data) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = new_capacity;
    return true;
}

static bool
dynasm_action_buffer_push(struct DynAsmActionBuffer* buffer, unsigned char value) {
    if (!dynasm_action_buffer_reserve(buffer, 1u)) {
        return false;
    }
    buffer->data[buffer->size++] = value;
    return true;
}

static bool
orus_jit_ir_emit_x86(const OrusJitIRProgram* program,
                     struct DynAsmActionBuffer* actions) {
    if (!program || !actions || program->count == 0) {
        return false;
    }

    if (!dynasm_action_buffer_push(actions, DASM_SECTION) ||
        !dynasm_action_buffer_push(actions, ORUS_JIT_SECTION_CODE)) {
        return false;
    }

    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_RETURN:
                if (!dynasm_action_buffer_push(actions, DASM_ESC) ||
                    !dynasm_action_buffer_push(actions, 0xC3u)) {
                    return false;
                }
                break;
            default:
                return false;
        }
    }

    if (!dynasm_action_buffer_push(actions, DASM_STOP)) {
        return false;
    }

    return true;
}

static JITBackendStatus
orus_jit_backend_compile_ir_x86(struct OrusJitBackend* backend,
                                const OrusJitIRProgram* program,
                                JITEntry* entry) {
    if (!backend || !program || !entry) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    struct DynAsmActionBuffer actions;
    dynasm_action_buffer_init(&actions);

    if (!orus_jit_ir_emit_x86(program, &actions)) {
        dynasm_action_buffer_release(&actions);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    dasm_State* dasm = NULL;
    size_t encoded_size = 0;

    dasm_init(&dasm, ORUS_JIT_MAX_SECTIONS);
    dasm_setup(&dasm, actions.data);
    dasm_growpc(&dasm, 1);

    dasm_put(&dasm, 0);

    int status = dasm_link(&dasm, &encoded_size);
    if (status != DASM_S_OK) {
        dasm_free(&dasm);
        dynasm_action_buffer_release(&actions);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    size_t capacity = 0;
    size_t page_size = backend->page_size ? backend->page_size : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(encoded_size, page_size, &capacity);
    if (!buffer) {
        dasm_free(&dasm);
        dynasm_action_buffer_release(&actions);
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    status = dasm_encode(&dasm, buffer);
    dasm_free(&dasm);
    dynasm_action_buffer_release(&actions);

    if (status != DASM_S_OK) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    (void)orus_jit_make_executable;
#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif
    orus_jit_flush_icache(buffer, encoded_size);

    entry->code_ptr = buffer;
    entry->code_size = encoded_size;
    entry->code_capacity = capacity;
    entry->entry_point = (JITEntryPoint)buffer;
    entry->debug_name = "orus_jit_ir_stub";

    return JIT_BACKEND_OK;
}
#endif // ORUS_JIT_HAS_DYNASM_X86

#if defined(__aarch64__)
static JITBackendStatus
orus_jit_backend_compile_ir_arm64(struct OrusJitBackend* backend,
                                  const OrusJitIRProgram* program,
                                  JITEntry* entry) {
    if (!backend || !program || !entry || program->count == 0) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    size_t encoded_size = program->count * sizeof(uint32_t);
    size_t capacity = 0;
    size_t page_size = backend->page_size ? backend->page_size : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(encoded_size, page_size, &capacity);
    if (!buffer) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    orus_jit_set_write_protection(false);
    uint32_t* cursor = (uint32_t*)buffer;
    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        if (inst->opcode != ORUS_JIT_IR_OP_RETURN) {
            orus_jit_set_write_protection(true);
            orus_jit_release_executable(buffer, capacity);
            return JIT_BACKEND_ASSEMBLY_ERROR;
        }
        cursor[i] = 0xD65F03C0u; // ret
    }
    orus_jit_set_write_protection(true);

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif

    orus_jit_flush_icache(buffer, encoded_size);

    entry->code_ptr = buffer;
    entry->code_size = encoded_size;
    entry->code_capacity = capacity;
    entry->entry_point = (JITEntryPoint)buffer;
    entry->debug_name = "orus_jit_ir_stub_arm64";

    return JIT_BACKEND_OK;
}
#endif

JITBackendStatus
orus_jit_backend_compile_noop(struct OrusJitBackend* backend,
                              JITEntry* out_entry) {
    if (!backend) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
    if (!backend->available) {
        if (out_entry) {
            memset(out_entry, 0, sizeof(*out_entry));
        }
        return JIT_BACKEND_UNSUPPORTED;
    }
    OrusJitIRInstruction instructions[] = {
        { .opcode = ORUS_JIT_IR_OP_RETURN },
    };
    OrusJitIRProgram program = {
        .instructions = instructions,
        .count = sizeof(instructions) / sizeof(instructions[0]),
    };
    return orus_jit_backend_compile_ir(backend, &program, out_entry);
}

JITBackendStatus
orus_jit_backend_compile_ir(struct OrusJitBackend* backend,
                            const OrusJitIRProgram* program,
                            JITEntry* out_entry) {
    if (!backend || !program || !out_entry || program->count == 0) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
    if (!backend->available) {
        memset(out_entry, 0, sizeof(*out_entry));
        return JIT_BACKEND_UNSUPPORTED;
    }

#if ORUS_JIT_HAS_DYNASM_X86
#if defined(__x86_64__) || defined(_M_X64)
    return orus_jit_backend_compile_ir_x86(backend, program, out_entry);
#endif
#endif
#if defined(__aarch64__)
    return orus_jit_backend_compile_ir_arm64(backend, program, out_entry);
#else
    (void)program;
    memset(out_entry, 0, sizeof(*out_entry));
    return JIT_BACKEND_UNSUPPORTED;
#endif
}

void
orus_jit_backend_release_entry(struct OrusJitBackend* backend, JITEntry* entry) {
    (void)backend;
    if (!entry || !entry->code_ptr) {
        return;
    }
    orus_jit_release_executable(entry->code_ptr, entry->code_capacity);
    entry->code_ptr = NULL;
    entry->entry_point = NULL;
    entry->code_capacity = 0;
    entry->code_size = 0;
    entry->debug_name = NULL;
}

static void
orus_jit_enter_stub(struct VM* vm, const JITEntry* entry) {
    if (!entry || !entry->entry_point) {
        return;
    }
    entry->entry_point(vm);
}

static void
orus_jit_invalidate_stub(struct VM* vm, const JITDeoptTrigger* trigger) {
    (void)vm;
    vm_jit_invalidate_entry(trigger);
}

static void
orus_jit_flush_stub(struct VM* vm) {
    (void)vm;
    vm_jit_flush_entries();
}

const JITBackendVTable*
orus_jit_backend_vtable(void) {
    static const JITBackendVTable vtable = {
        .enter = orus_jit_enter_stub,
        .invalidate = orus_jit_invalidate_stub,
        .flush = orus_jit_flush_stub,
    };
    return &vtable;
}
