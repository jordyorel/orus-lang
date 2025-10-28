// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_backend.c
// Description: DynASM-backed JIT backend bootstrap providing minimal native
//              entry compilation for the Orus VM tiering roadmap.

#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/jit_debug.h"
#include "vm/jit_layout.h"
#include "vm/vm_comparison.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"
#include "vm/vm_string_ops.h"
#include "vm/register_file.h"
#include "vm/vm_tagged_union.h"
#include "runtime/builtins.h"
#include "runtime/memory.h"

#include "internal/logging.h"

#if defined(__x86_64__) || defined(_M_X64)
#include "dasm_proto.h"
#include "dasm_x86.h"
#define ORUS_JIT_HAS_DYNASM_X86 1
#else
#define ORUS_JIT_HAS_DYNASM_X86 0
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <setjmp.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

#if defined(__APPLE__) && defined(__aarch64__)
#if defined(__has_include) && __has_include(<ptrauth.h>)
#include <ptrauth.h>
#if defined(__has_feature) && __has_feature(ptrauth_intrinsics)
#define ORUS_JIT_USE_PTRAUTH 1
#endif
#endif
#endif

#ifndef ORUS_JIT_USE_PTRAUTH
#define ORUS_JIT_USE_PTRAUTH 0
#endif

static inline uint64_t
orus_jit_function_ptr_bits(const void* ptr) {
#if ORUS_JIT_USE_PTRAUTH
    (void*)ptr, ptrauth_key_function_pointer,
        ptrauth_type_discriminator(JITEntryPoint));
    return (uint64_t)(uintptr_t)signed_ptr;
#else
    return (uint64_t)(uintptr_t)ptr;
#endif
}

static inline JITEntryPoint
orus_jit_make_entry_point(void* ptr) {
#if ORUS_JIT_USE_PTRAUTH
    return (JITEntryPoint)__builtin_ptrauth_sign_unauthenticated(
        ptr, ptrauth_key_function_pointer,
        ptrauth_type_discriminator(JITEntryPoint));
#else
    return (JITEntryPoint)ptr;
#endif
}

static size_t orus_jit_detect_page_size(void);
static void* orus_jit_alloc_executable(size_t size, size_t page_size, size_t* out_capacity);
static inline bool orus_jit_set_write_protection(bool enable);
#if !defined(_WIN32)
static bool orus_jit_make_executable(void* ptr, size_t size);
#endif
static void orus_jit_release_executable(void* ptr, size_t capacity);
static void orus_jit_flush_icache(void* ptr, size_t size);

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
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

typedef struct {
    bool supported;
    OrusJitBackendTarget target;
    JITBackendStatus status;
    const char* message;
} OrusJitBackendAvailability;

static OrusJitBackendAvailability
orus_jit_backend_detect_availability(void) {
    const char* forced_disable = getenv("ORUS_JIT_FORCE_UNSUPPORTED");
    if (forced_disable && forced_disable[0] != '\0') {
        return (OrusJitBackendAvailability){
            .supported = false,
            .target = ORUS_JIT_BACKEND_TARGET_NATIVE,
            .status = JIT_BACKEND_UNSUPPORTED,
            .message =
                "JIT backend force-disabled via ORUS_JIT_FORCE_UNSUPPORTED.",
        };
    }

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return (OrusJitBackendAvailability){
        .supported = true,
        .target = ORUS_JIT_BACKEND_TARGET_X86_64,
        .status = JIT_BACKEND_OK,
        .message = "Detected x86_64 host architecture.",
    };
#elif defined(__aarch64__) || defined(_M_ARM64)
    return (OrusJitBackendAvailability){
        .supported = true,
        .target = ORUS_JIT_BACKEND_TARGET_AARCH64,
        .status = JIT_BACKEND_OK,
        .message = "Detected AArch64 host architecture.",
    };
#elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
    return (OrusJitBackendAvailability){
        .supported = false,
        .target = ORUS_JIT_BACKEND_TARGET_RISCV64,
        .status = JIT_BACKEND_UNSUPPORTED,
        .message =
            "RISC-V 64-bit host detected but the JIT backend is not yet implemented.",
    };
#else
    return (OrusJitBackendAvailability){
        .supported = false,
        .target = ORUS_JIT_BACKEND_TARGET_NATIVE,
        .status = JIT_BACKEND_UNSUPPORTED,
        .message =
            "Host architecture is not supported by the Orus JIT backend.",
    };
#endif
}

struct OrusJitBackend {
    size_t page_size;
    bool available;
    OrusJitBackendTarget target;
    JITBackendStatus availability_status;
    const char* availability_message;
};

typedef struct OrusJitNativeBlock {
    OrusJitIRProgram program;
    void* code_ptr;
    size_t code_capacity;
    struct OrusJitNativeBlock* next;
} OrusJitNativeBlock;

static OrusJitNativeBlock* g_native_blocks = NULL;

typedef struct {
    void* base;
    size_t size;
    bool executable;
    bool uses_mmap;
    bool requires_write_protect;
} OrusJitExecutableRegion;

static OrusJitExecutableRegion* g_orus_jit_regions = NULL;
static size_t g_orus_jit_region_count = 0u;
static size_t g_orus_jit_region_capacity = 0u;
static int g_orus_jit_linear_emitter_override = -1;
static sigjmp_buf g_orus_jit_write_probe_env;

#if defined(_WIN32)
static CRITICAL_SECTION g_orus_jit_region_lock;
static bool g_orus_jit_region_lock_initialized = false;

static void
orus_jit_region_lock(void) {
    if (!g_orus_jit_region_lock_initialized) {
        InitializeCriticalSection(&g_orus_jit_region_lock);
        g_orus_jit_region_lock_initialized = true;
    }
    EnterCriticalSection(&g_orus_jit_region_lock);
}

static void
orus_jit_region_unlock(void) {
    if (g_orus_jit_region_lock_initialized) {
        LeaveCriticalSection(&g_orus_jit_region_lock);
    }
}
#else
static pthread_mutex_t g_orus_jit_region_lock = PTHREAD_MUTEX_INITIALIZER;

static void
orus_jit_region_lock(void) {
    pthread_mutex_lock(&g_orus_jit_region_lock);
}

static void
orus_jit_region_unlock(void) {
    pthread_mutex_unlock(&g_orus_jit_region_lock);
}
#endif

#if ORUS_JIT_USE_APPLE_JIT
static bool
orus_jit_regions_need_write_toggle(void) {
    bool needed = false;
    orus_jit_region_lock();
    for (size_t i = 0; i < g_orus_jit_region_count; ++i) {
        if (g_orus_jit_regions[i].requires_write_protect) {
            needed = true;
            break;
        }
    }
    orus_jit_region_unlock();
    return needed;
}
#endif

static void
orus_jit_write_probe_handler(int signal_number) {
    (void)signal_number;
    siglongjmp(g_orus_jit_write_probe_env, 1);
}

static bool
orus_jit_backend_probe_executable(struct OrusJitBackend* backend) {
#if defined(__unix__) || defined(__APPLE__)
    if (!backend) {
        return false;
    }

    struct sigaction new_action;
    memset(&new_action, 0, sizeof(new_action));
    new_action.sa_handler = orus_jit_write_probe_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = SA_NODEFER;

    struct sigaction old_bus;
    struct sigaction old_segv;
    const int install_bus = sigaction(SIGBUS, &new_action, &old_bus);
    const int install_segv = sigaction(SIGSEGV, &new_action, &old_segv);

    bool supported = true;
    void* buffer = NULL;
    size_t capacity = 0u;

    if (install_bus != 0 || install_segv != 0) {
        supported = false;
    } else if (sigsetjmp(g_orus_jit_write_probe_env, 1) == 0) {
        buffer = orus_jit_alloc_executable(64u, backend->page_size, &capacity);
        if (!buffer) {
            supported = false;
        } else if (!orus_jit_set_write_protection(false)) {
            supported = false;
        } else {
            volatile uint8_t* bytes = (volatile uint8_t*)buffer;
            bytes[0] = 0xCCu;
            if (!orus_jit_set_write_protection(true)) {
                supported = false;
            }
        }
    } else {
        supported = false;
    }

    if (buffer) {
        orus_jit_release_executable(buffer, capacity);
    }

    if (install_bus == 0) {
        sigaction(SIGBUS, &old_bus, NULL);
    }
    if (install_segv == 0) {
        sigaction(SIGSEGV, &old_segv, NULL);
    }

    return supported;
#else
    (void)backend;
    return true;
#endif
}

static bool
orus_jit_register_region(void* base,
                         size_t size,
                         bool uses_mmap,
                         bool requires_write_protect) {
    if (!base || size == 0u) {
        return false;
    }

    orus_jit_region_lock();

    for (size_t i = 0; i < g_orus_jit_region_count; ++i) {
        if (g_orus_jit_regions[i].base == base) {
            g_orus_jit_regions[i].size = size;
            g_orus_jit_regions[i].executable = requires_write_protect;
            g_orus_jit_regions[i].uses_mmap = uses_mmap;
            g_orus_jit_regions[i].requires_write_protect = requires_write_protect;
            orus_jit_region_unlock();
            return true;
        }
    }

    if (g_orus_jit_region_count == g_orus_jit_region_capacity) {
        size_t new_capacity = g_orus_jit_region_capacity ? g_orus_jit_region_capacity * 2u : 8u;
        OrusJitExecutableRegion* resized =
            (OrusJitExecutableRegion*)realloc(g_orus_jit_regions,
                                              new_capacity * sizeof(OrusJitExecutableRegion));
        if (!resized) {
            orus_jit_region_unlock();
            return false;
        }
        g_orus_jit_regions = resized;
        g_orus_jit_region_capacity = new_capacity;
    }

    g_orus_jit_regions[g_orus_jit_region_count].base = base;
    g_orus_jit_regions[g_orus_jit_region_count].size = size;
    g_orus_jit_regions[g_orus_jit_region_count].executable = requires_write_protect;
    g_orus_jit_regions[g_orus_jit_region_count].uses_mmap = uses_mmap;
    g_orus_jit_regions[g_orus_jit_region_count].requires_write_protect =
        requires_write_protect;
    g_orus_jit_region_count++;

    orus_jit_region_unlock();
    return true;
}

static bool
orus_jit_unregister_region(void* base, size_t size, bool* out_uses_mmap) {
    (void)size;
    if (!base) {
        return false;
    }

    bool uses_mmap = true;
    bool removed = false;

    orus_jit_region_lock();
    for (size_t i = 0; i < g_orus_jit_region_count; ++i) {
        if (g_orus_jit_regions[i].base == base) {
            uses_mmap = g_orus_jit_regions[i].uses_mmap;
            g_orus_jit_regions[i] =
                g_orus_jit_regions[g_orus_jit_region_count - 1u];
            g_orus_jit_region_count--;
            removed = true;
            break;
        }
    }
    orus_jit_region_unlock();

    if (removed && out_uses_mmap) {
        *out_uses_mmap = uses_mmap;
    }

    return removed;
}

static bool
orus_jit_protect_region(OrusJitExecutableRegion* region, bool executable) {
    if (!region || !region->base || region->size == 0u) {
        return false;
    }
    if (region->executable == executable) {
        return true;
    }

#ifdef _WIN32
    DWORD protect = executable ? PAGE_EXECUTE_READ : PAGE_READWRITE;
    DWORD old_protect = 0u;
    if (!VirtualProtect(region->base, region->size, protect, &old_protect)) {
        LOG_ERROR("[JIT] VirtualProtect(%p, %zu, %s) failed with %lu",
                  region->base, region->size,
                  executable ? "PAGE_EXECUTE_READ" : "PAGE_READWRITE",
                  GetLastError());
        return false;
    }
#else
    int prot = executable ? (PROT_READ | PROT_EXEC) : (PROT_READ | PROT_WRITE);
    if (mprotect(region->base, region->size, prot) != 0) {
        int protect_errno = errno;
#if ORUS_JIT_USE_APPLE_JIT
        if (protect_errno == EPERM || protect_errno == ENOTSUP) {
            LOG_WARN("[JIT] mprotect(PROT_%s) rejected with %s (errno=%d). Ensure the binary carries the com.apple.security.cs.allow-jit entitlement to enable native execution.",
                     executable ? "EXEC" : "WRITE",
                     strerror(protect_errno), protect_errno);
        } else {
            LOG_WARN("[JIT] mprotect(PROT_%s) failed with %s (errno=%d).",
                     executable ? "EXEC" : "WRITE",
                     strerror(protect_errno), protect_errno);
        }
#endif
        LOG_ERROR("[JIT] mprotect(%p, %zu, %s) failed: %s (errno=%d)",
                  region->base, region->size,
                  executable ? "RX" : "RW",
                  strerror(protect_errno), protect_errno);
        return false;
    }
#endif

    region->executable = executable;
    return true;
}

static bool
orus_jit_apply_region_protection(bool executable) {
    bool ok = true;
    orus_jit_region_lock();
    for (size_t i = 0; i < g_orus_jit_region_count; ++i) {
        if (!orus_jit_protect_region(&g_orus_jit_regions[i], executable)) {
            ok = false;
        }
    }
    orus_jit_region_unlock();
    return ok;
}

typedef struct OrusJitNativeFrame {
    const OrusJitNativeBlock* block;
    struct OrusJitNativeFrame* prev;
    TypedRegisterWindow* active_window;
    uint32_t window_version;
    bool slow_path_requested;
    uint64_t canary;
} OrusJitNativeFrame;

#define ORUS_JIT_NATIVE_FRAME_CANARY UINT64_C(0xB5D1C0DECAFEBABE)

static void orus_jit_native_propagate_runtime_error(struct VM* vm_instance,
                                                    OrusJitNativeFrame* frame);

static void
orus_jit_native_verify_frame(OrusJitNativeFrame* frame) {
    if (!frame) {
        return;
    }
    if (frame->canary != ORUS_JIT_NATIVE_FRAME_CANARY) {
        LOG_ERROR("[JIT] Detected native frame canary violation (%p). Aborting to maintain stack integrity.",
                  (void*)frame);
        abort();
    }
}

typedef enum {
    ORUS_JIT_HELPER_STUB_KIND_GC = 0,
    ORUS_JIT_HELPER_STUB_KIND_STRING,
    ORUS_JIT_HELPER_STUB_KIND_RUNTIME,
    ORUS_JIT_HELPER_STUB_KIND_COUNT
} OrusJitHelperStubKind;

typedef struct {
    void* code;
    size_t size;
    size_t capacity;
} OrusJitHelperStub;

static OrusJitHelperStub g_dynasm_helper_stubs[ORUS_JIT_HELPER_STUB_KIND_COUNT] = {0};
static size_t g_dynasm_helper_stub_page_size = 0u;
static size_t g_dynasm_helper_stub_users = 0u;

typedef struct {
    const void* helper;
    OrusJitHelperStubKind kind;
    const char* name;
} OrusJitHelperRegistration;

static OrusJitHelperRegistration* g_orus_jit_helper_registry = NULL;
static size_t g_orus_jit_helper_count = 0u;
static size_t g_orus_jit_helper_capacity = 0u;

#if ORUS_JIT_HAS_DYNASM_X86
#if defined(_WIN32)
static CRITICAL_SECTION g_orus_jit_helper_lock;
static bool g_orus_jit_helper_lock_initialized = false;

static void
orus_jit_helper_lock(void) {
    if (!g_orus_jit_helper_lock_initialized) {
        InitializeCriticalSection(&g_orus_jit_helper_lock);
        g_orus_jit_helper_lock_initialized = true;
    }
    EnterCriticalSection(&g_orus_jit_helper_lock);
}

static void
orus_jit_helper_unlock(void) {
    if (g_orus_jit_helper_lock_initialized) {
        LeaveCriticalSection(&g_orus_jit_helper_lock);
    }
}
#else
static pthread_mutex_t g_orus_jit_helper_lock = PTHREAD_MUTEX_INITIALIZER;

static void
orus_jit_helper_lock(void) {
    pthread_mutex_lock(&g_orus_jit_helper_lock);
}

static void
orus_jit_helper_unlock(void) {
    pthread_mutex_unlock(&g_orus_jit_helper_lock);
}
#endif
#endif

#if ORUS_JIT_HAS_DYNASM_X86
static bool
orus_jit_helper_registry_record(const void* helper,
                                OrusJitHelperStubKind kind,
                                const char* name) {
    if (!helper || kind >= ORUS_JIT_HELPER_STUB_KIND_COUNT) {
        return false;
    }

    orus_jit_helper_lock();

    for (size_t i = 0; i < g_orus_jit_helper_count; ++i) {
        OrusJitHelperRegistration* entry = &g_orus_jit_helper_registry[i];
        if (entry->helper == helper) {
            if (entry->kind != kind) {
                LOG_ERROR("[JIT] Helper %p previously registered for stub kind %d but requested with kind %d",
                          helper, (int)entry->kind, (int)kind);
                orus_jit_helper_unlock();
                return false;
            }
            orus_jit_helper_unlock();
            return true;
        }
    }

    if (g_orus_jit_helper_count == g_orus_jit_helper_capacity) {
        size_t new_capacity = g_orus_jit_helper_capacity ? g_orus_jit_helper_capacity * 2u : 16u;
        OrusJitHelperRegistration* resized =
            (OrusJitHelperRegistration*)realloc(g_orus_jit_helper_registry,
                                                new_capacity * sizeof(OrusJitHelperRegistration));
        if (!resized) {
            orus_jit_helper_unlock();
            return false;
        }
        g_orus_jit_helper_registry = resized;
        g_orus_jit_helper_capacity = new_capacity;
    }

    g_orus_jit_helper_registry[g_orus_jit_helper_count].helper = helper;
    g_orus_jit_helper_registry[g_orus_jit_helper_count].kind = kind;
    g_orus_jit_helper_registry[g_orus_jit_helper_count].name = name;
    g_orus_jit_helper_count++;

    orus_jit_helper_unlock();
    return true;
}
#endif

static inline VM*
orus_jit_native_vm(struct VM* vm_instance) {
    return vm_instance ? vm_instance : &vm;
}

static void
orus_jit_native_request_slow_path(struct VM* vm_instance) {
    VM* target = orus_jit_native_vm(vm_instance);
    vm_mark_native_slow_path(target);
    if (target->jit_native_frame_top) {
        target->jit_native_frame_top->slow_path_requested = true;
    }
}

static void
orus_jit_helper_stub_release(OrusJitHelperStub* stub) {
    if (!stub || !stub->code) {
        return;
    }
    orus_jit_release_executable(stub->code, stub->capacity);
    stub->code = NULL;
    stub->capacity = 0u;
    stub->size = 0u;
}

static void
orus_jit_helper_stubs_release_all(void) {
    for (size_t i = 0; i < ORUS_JIT_HELPER_STUB_KIND_COUNT; ++i) {
        orus_jit_helper_stub_release(&g_dynasm_helper_stubs[i]);
    }
}

#if ORUS_JIT_HAS_DYNASM_X86
static bool
orus_jit_helper_stub_init(OrusJitHelperStub* stub, OrusJitHelperStubKind kind) {
    if (!stub) {
        return false;
    }
    if (stub->code) {
        return true;
    }

    size_t capacity = 0u;
    size_t page_size =
        g_dynasm_helper_stub_page_size ? g_dynasm_helper_stub_page_size
                                       : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(64u, page_size, &capacity);
    if (!buffer) {
        return false;
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        return false;
    }
    uint8_t* code = (uint8_t*)buffer;
    size_t stub_size = 0u;

#if defined(_WIN32)
    (void)kind;
    // The Windows x64 ABI expects arguments in rcx, rdx, r8, r9 with 32 bytes of
    // shadow space and additional arguments spilled on the stack. Our DynASM
    // helpers marshal arguments using the System V register order, so the stub
    // remaps registers and materializes spill slots before tail-calling the
    // actual helper in rax.
    const uint8_t windows_stub[] = {
        0x48, 0x83, 0xEC, 0x30,             // sub rsp, 0x30
        0x4C, 0x89, 0x44, 0x24, 0x20,       // mov [rsp+0x20], r8
        0x4C, 0x89, 0x4C, 0x24, 0x28,       // mov [rsp+0x28], r9
        0x4C, 0x8B, 0xD2,                   // mov r10, rdx
        0x4C, 0x8B, 0xD9,                   // mov r11, rcx
        0x48, 0x8B, 0xCF,                   // mov rcx, rdi
        0x48, 0x8B, 0xD6,                   // mov rdx, rsi
        0x4D, 0x8B, 0xC2,                   // mov r8, r10
        0x4D, 0x8B, 0xCB,                   // mov r9, r11
        0xFF, 0xD0,                         // call rax
        0x48, 0x83, 0xC4, 0x30,             // add rsp, 0x30
        0xC3                                // ret
    };
    memcpy(code, windows_stub, sizeof(windows_stub));
    stub_size = sizeof(windows_stub);
#else
    (void)kind;
    // System V x86_64 already matches the register mapping used by the DynASM
    // helpers, so the stub can tail-call the helper directly.
    code[0] = 0xFFu;
    code[1] = 0xE0u; // jmp rax
    stub_size = 2u;
#endif

    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        return false;
    }

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return false;
    }
#endif

    orus_jit_flush_icache(buffer, stub_size);

    stub->code = buffer;
    stub->capacity = capacity;
    stub->size = stub_size;
    return true;
}
#endif

#if ORUS_JIT_HAS_DYNASM_X86
static const void*
orus_jit_helper_stub_address(OrusJitHelperStubKind kind) {
    if (kind >= ORUS_JIT_HELPER_STUB_KIND_COUNT) {
        return NULL;
    }
    OrusJitHelperStub* stub = &g_dynasm_helper_stubs[kind];
    if (!orus_jit_helper_stub_init(stub, kind)) {
        return NULL;
    }
    return stub->code;
}
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

    size_t alignment = page_size ? page_size : orus_jit_detect_page_size();
    size_t capacity = align_up(size, alignment);
    bool used_mmap = true;
    bool requires_write_protect = false;

#ifdef _WIN32
    void* buffer = VirtualAlloc(NULL, capacity, MEM_COMMIT | MEM_RESERVE,
                                PAGE_EXECUTE_READWRITE);
    if (!buffer) {
        return NULL;
    }
#else
    int prot = PROT_READ | PROT_WRITE;
    int flags = MAP_PRIVATE;
#ifdef MAP_ANONYMOUS
    flags |= MAP_ANONYMOUS;
#elif defined(MAP_ANON)
    flags |= MAP_ANON;
#endif
#if ORUS_JIT_USE_APPLE_JIT
    flags |= MAP_JIT;
    prot = PROT_READ | PROT_EXEC;
#endif

    void* buffer = mmap(NULL, capacity, prot, flags, -1, 0);
    if (buffer == MAP_FAILED) {
        int map_errno = errno;
        buffer = NULL;
#if ORUS_JIT_USE_APPLE_JIT
        if (map_errno == EPERM || map_errno == ENOTSUP) {
            LOG_WARN("[JIT] mmap(MAP_JIT) failed with %s (errno=%d). macOS requires the com.apple.security.cs.allow-jit entitlement to enable native tier execution. The build tries to sign targets automatically; rerun scripts/macos/sign-with-jit.sh if codesign was unavailable during build.",
                     strerror(map_errno), map_errno);
        } else {
            LOG_WARN("[JIT] mmap(MAP_JIT) failed with %s (errno=%d).",
                     strerror(map_errno), map_errno);
        }
#else
        LOG_WARN("[JIT] mmap failed with %s (errno=%d) while allocating executable pages.",
                 strerror(map_errno), map_errno);
#endif
        errno = 0;
    }
#if ORUS_JIT_USE_APPLE_JIT
    if (buffer) {
        requires_write_protect = true;
    } else {
        int fallback_flags = flags & ~MAP_JIT;
#if ORUS_JIT_USE_APPLE_JIT
        int fallback_prot = PROT_READ | PROT_WRITE | PROT_EXEC;
        buffer = mmap(NULL, capacity, fallback_prot, fallback_flags, -1, 0);
#else
        buffer = mmap(NULL, capacity, prot, fallback_flags, -1, 0);
#endif
        if (buffer == MAP_FAILED) {
            buffer = NULL;
            errno = 0;
        }
    }
#endif
#endif

    if (!buffer) {
        return NULL;
    }

    if (!orus_jit_register_region(buffer, capacity, used_mmap,
                                  requires_write_protect)) {
#ifdef _WIN32
        VirtualFree(buffer, 0, MEM_RELEASE);
#else
        if (used_mmap) {
            munmap(buffer, capacity);
        } else {
            free(buffer);
        }
#endif
        return NULL;
    }

    if (out_capacity) {
        *out_capacity = capacity;
    }
    return buffer;
}

static inline bool
orus_jit_set_write_protection(bool enable) {
#if ORUS_JIT_USE_APPLE_JIT
    bool needs_toggle = orus_jit_regions_need_write_toggle();
    if (!enable && needs_toggle) {
        pthread_jit_write_protect_np(false);
    }
#endif

    if (!orus_jit_apply_region_protection(enable)) {
        LOG_ERROR("[JIT] Failed to transition executable heap to %s mode",
                  enable ? "read/execute" : "read/write");
#if ORUS_JIT_USE_APPLE_JIT
        if (!enable && needs_toggle) {
            pthread_jit_write_protect_np(true);
        }
#endif
        return false;
    }

#if ORUS_JIT_USE_APPLE_JIT
    if (enable && needs_toggle) {
        pthread_jit_write_protect_np(true);
    }
#endif
    return true;
}

#if !defined(_WIN32)
static bool
orus_jit_make_executable(void* ptr, size_t size) {
    (void)ptr;
    (void)size;
    return orus_jit_apply_region_protection(true);
}
#endif

static void
orus_jit_release_executable(void* ptr, size_t capacity) {
    if (!ptr || !capacity) {
        return;
    }
    bool used_mmap = true;
    orus_jit_unregister_region(ptr, capacity, &used_mmap);
#ifdef _WIN32
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    if (used_mmap) {
        munmap(ptr, capacity);
    } else {
        free(ptr);
    }
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

static void orus_jit_execute_block(struct VM* vm_instance,
                                   const OrusJitNativeBlock* block);

#if ORUS_JIT_HAS_DYNASM_X86
static JITBackendStatus orus_jit_backend_compile_ir_x86(struct OrusJitBackend* backend,
                                                        const OrusJitIRProgram* program,
                                                        JITEntry* entry);
#endif

#if defined(__aarch64__)
static bool
orus_jit_emit_a64_mov_imm64(uint32_t* code,
                            size_t* index,
                            size_t capacity_words,
                            uint8_t reg,
                            uint64_t value) {
    if (*index >= capacity_words) {
        return false;
    }
    code[(*index)++] = 0xD2800000u | ((uint32_t)(value & 0xFFFFu) << 5) | reg;
    for (uint32_t shift = 16u; shift < 64u; shift += 16u) {
        uint16_t part = (uint16_t)((value >> shift) & 0xFFFFu);
        if (!part) {
            continue;
        }
        if (*index >= capacity_words) {
            return false;
        }
        uint32_t hw = shift / 16u;
        code[(*index)++] = 0xF2800000u | (hw << 21) | ((uint32_t)part << 5) | reg;
    }
    return true;
}
#endif

static JITBackendStatus
orus_jit_backend_emit_helper_stub(struct OrusJitBackend* backend,
                                  OrusJitNativeBlock* block,
                                  JITEntry* entry) {
    if (!backend || !block || !entry) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

#if defined(__x86_64__) || defined(_M_X64)
    size_t capacity = 0;
    size_t stub_size = 0;
    void* buffer = orus_jit_alloc_executable(32u, backend->page_size, &capacity);
    if (!buffer) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
    uint8_t* code = (uint8_t*)buffer;

#if defined(_WIN32)
    // mov rdx, imm64
    code[0] = 0x48u;
    code[1] = 0xBAu;
    memcpy(&code[2], &block, sizeof(void*));
    // mov rax, imm64
    code[10] = 0x48u;
    code[11] = 0xB8u;
    void* helper = (void*)&orus_jit_execute_block;
    memcpy(&code[12], &helper, sizeof(void*));
    // jmp rax
    code[20] = 0xFFu;
    code[21] = 0xE0u;
    stub_size = 22u;
#else
    // mov rsi, imm64
    code[0] = 0x48u;
    code[1] = 0xBEu;
    memcpy(&code[2], &block, sizeof(void*));
    // mov rax, imm64
    code[10] = 0x48u;
    code[11] = 0xB8u;
    void* helper = (void*)&orus_jit_execute_block;
    memcpy(&code[12], &helper, sizeof(void*));
    // jmp rax
    code[20] = 0xFFu;
    code[21] = 0xE0u;
    stub_size = 22u;
#endif

    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif

    orus_jit_flush_icache(buffer, stub_size);

    block->code_ptr = buffer;
    block->code_capacity = capacity;

    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->code_ptr = buffer;
    entry->code_size = stub_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_helper_stub";
    orus_jit_debug_publish_disassembly(&block->program,
                                       backend->target,
                                       buffer,
                                       stub_size);
    return JIT_BACKEND_OK;
#elif defined(__aarch64__)
    size_t capacity = 0;
    size_t stub_size = 0;
    void* buffer = orus_jit_alloc_executable(64u, backend->page_size, &capacity);
    if (!buffer) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    uint32_t* code = (uint32_t*)buffer;
    size_t capacity_words = capacity / sizeof(uint32_t);
    size_t index = 0u;
    bool success = true;

    if (index < capacity_words) {
        code[index++] = 0xA9BF7BF0u; // stp x29, x30, [sp, #-16]!
    } else {
        success = false;
    }
    if (success) {
        if (index < capacity_words) {
            code[index++] = 0x910003FDu; // mov x29, sp
        } else {
            success = false;
        }
    }

    if (success) {
        success = orus_jit_emit_a64_mov_imm64(code, &index, capacity_words, 0x1u,
                                              (uint64_t)(uintptr_t)block);
    }

    if (success) {
        success = orus_jit_emit_a64_mov_imm64(
            code, &index, capacity_words, 0x10u,
            orus_jit_function_ptr_bits(&orus_jit_execute_block));
    }

    if (success) {
        if (index < capacity_words) {
            code[index++] = 0xD63F0200u; // blr x16
        } else {
            success = false;
        }
    }

    if (success) {
        if (index < capacity_words) {
            code[index++] = 0xA8C17BF0u; // ldp x29, x30, [sp], #16
        } else {
            success = false;
        }
    }

    if (success) {
        if (index < capacity_words) {
            code[index++] = 0xD65F03C0u; // ret
        } else {
            success = false;
        }
    }

    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    if (!success) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    stub_size = index * sizeof(uint32_t);

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif

    orus_jit_flush_icache(buffer, stub_size);

    block->code_ptr = buffer;
    block->code_capacity = capacity;

    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->code_ptr = buffer;
    entry->code_size = stub_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_helper_stub";
    orus_jit_debug_publish_disassembly(&block->program,
                                       backend->target,
                                       buffer,
                                       stub_size);
    return JIT_BACKEND_OK;
#else
    (void)block;
    (void)entry;
    return JIT_BACKEND_UNSUPPORTED;
#endif
}

static OrusJitNativeBlock*
orus_jit_native_block_create(const OrusJitIRProgram* program) {
    if (!program || program->count == 0 || !program->instructions) {
        return NULL;
    }

    OrusJitNativeBlock* block =
        (OrusJitNativeBlock*)calloc(1, sizeof(OrusJitNativeBlock));
    if (!block) {
        return NULL;
    }

    block->program = *program;
    block->program.capacity = program->count;
    block->program.instructions =
        (OrusJitIRInstruction*)malloc(program->count * sizeof(OrusJitIRInstruction));
    if (!block->program.instructions) {
        free(block);
        return NULL;
    }

    memcpy(block->program.instructions, program->instructions,
           program->count * sizeof(OrusJitIRInstruction));
    return block;
}

static void
orus_jit_native_block_destroy(OrusJitNativeBlock* block) {
    if (!block) {
        return;
    }
    orus_jit_ir_program_reset(&block->program);
    free(block);
}

static void
orus_jit_native_block_register(OrusJitNativeBlock* block) {
    if (!block) {
        return;
    }
    block->next = g_native_blocks;
    g_native_blocks = block;
}

static OrusJitNativeBlock*
orus_jit_native_block_find(void* code_ptr, OrusJitNativeBlock** out_prev) {
    OrusJitNativeBlock* prev = NULL;
    OrusJitNativeBlock* current = g_native_blocks;
    while (current) {
        if (current->code_ptr == code_ptr) {
            if (out_prev) {
                *out_prev = prev;
            }
            return current;
        }
        prev = current;
        current = current->next;
    }
    if (out_prev) {
        *out_prev = NULL;
    }
    return NULL;
}

static void
jit_bailout_and_deopt(struct VM* vm_instance,
                      const OrusJitNativeBlock* block) {
    if (!vm_instance) {
        return;
    }

    vm_instance->jit_native_type_deopts++;

    if (block) {
        orus_jit_debug_record_guard_exit(
            vm_instance,
            block->program.function_index,
            block->program.loop_index,
            "guard_exit",
            ORUS_JIT_DEBUG_INVALID_INSTRUCTION_INDEX);
    } else {
        orus_jit_debug_record_guard_exit(
            vm_instance,
            UINT16_MAX,
            UINT16_MAX,
            "guard_exit",
            ORUS_JIT_DEBUG_INVALID_INSTRUCTION_INDEX);
    }

    if (block) {
        vm_instance->jit_loop_blocklist[block->program.loop_index] = true;

        JITDeoptTrigger trigger = {
            .function_index = block->program.function_index,
            .loop_index = block->program.loop_index,
            .generation = 0,
        };
        vm_instance->jit_pending_invalidate = true;
        vm_instance->jit_pending_trigger = trigger;

        if (block->program.function_index < vm_instance->functionCount) {
            Function* function = &vm_instance->functions[block->program.function_index];
            if (function && !function->deopt_handler) {
                function->deopt_handler = vm_default_deopt_stub;
            }
        }
    }

    vm_handle_type_error_deopt();
}

static bool orus_jit_native_safepoint(struct VM* vm_instance);

static bool orus_jit_opcode_is_add(OrusJitIROpcode opcode);
static bool orus_jit_opcode_is_sub(OrusJitIROpcode opcode);
static bool orus_jit_opcode_is_mul(OrusJitIROpcode opcode);

static size_t g_orus_jit_helper_safepoint_count = 0u;

#define ORUS_JIT_INLINE_TOSTRING_CACHE_SIZE 16u

typedef struct {
    uint8_t kind;
    uint64_t bits;
    ObjString* value;
} OrusJitInlineToStringCacheEntry;

static OrusJitInlineToStringCacheEntry
    g_orus_jit_inline_tostring_cache[ORUS_JIT_INLINE_TOSTRING_CACHE_SIZE];

static inline bool
orus_jit_inline_tostring_signature(Value value, uint8_t* out_kind, uint64_t* out_bits) {
    if (!out_kind || !out_bits) {
        return false;
    }
    if (IS_I32(value)) {
        *out_kind = 0x01u;
        *out_bits = (uint64_t)(uint32_t)AS_I32(value);
        return true;
    }
    if (IS_I64(value)) {
        *out_kind = 0x02u;
        *out_bits = (uint64_t)AS_I64(value);
        return true;
    }
    if (IS_U32(value)) {
        *out_kind = 0x03u;
        *out_bits = (uint64_t)AS_U32(value);
        return true;
    }
    if (IS_U64(value)) {
        *out_kind = 0x04u;
        *out_bits = AS_U64(value);
        return true;
    }
    if (IS_F64(value)) {
        *out_kind = 0x05u;
        double number = AS_F64(value);
        memcpy(out_bits, &number, sizeof(double));
        return true;
    }
    if (IS_BOOL(value)) {
        *out_kind = 0x06u;
        *out_bits = (uint64_t)AS_BOOL(value);
        return true;
    }
    return false;
}

static inline size_t
orus_jit_inline_tostring_slot(uint8_t kind, uint64_t bits) {
    uint64_t mix = bits ^ ((uint64_t)kind * 0x9E3779B97F4A7C15ull);
    return (size_t)(mix % ORUS_JIT_INLINE_TOSTRING_CACHE_SIZE);
}

static inline ObjString*
orus_jit_inline_tostring_cache_lookup(uint8_t kind, uint64_t bits) {
    size_t slot = orus_jit_inline_tostring_slot(kind, bits);
    OrusJitInlineToStringCacheEntry* entry = &g_orus_jit_inline_tostring_cache[slot];
    if (entry->kind == kind && entry->bits == bits) {
        return entry->value;
    }
    return NULL;
}

static inline void
orus_jit_inline_tostring_cache_store(uint8_t kind,
                                     uint64_t bits,
                                     ObjString* string_value) {
    if (!string_value) {
        return;
    }
    size_t slot = orus_jit_inline_tostring_slot(kind, bits);
    g_orus_jit_inline_tostring_cache[slot].kind = kind;
    g_orus_jit_inline_tostring_cache[slot].bits = bits;
    g_orus_jit_inline_tostring_cache[slot].value = string_value;
}

static inline uint16_t
orus_jit_native_select_bit(uint64_t mask) {
#if defined(_MSC_VER)
    unsigned long index = 0;
    _BitScanForward64(&index, mask);
    return (uint16_t)index;
#else
    return (uint16_t)__builtin_ctzll(mask);
#endif
}

static void
orus_jit_native_flush_active_window(struct VM* vm_instance) {
    if (!vm_instance) {
        return;
    }

    TypedRegisterWindow* window = vm_instance->typed_regs.active_window
                                      ? vm_instance->typed_regs.active_window
                                      : &vm_instance->typed_regs.root_window;
    if (!window) {
        return;
    }

    for (uint16_t word = 0; word < TYPED_WINDOW_LIVE_WORDS; ++word) {
        uint64_t dirty = window->dirty_mask[word];
        while (dirty) {
            uint16_t bit_index = orus_jit_native_select_bit(dirty);
            uint16_t reg = (uint16_t)(word * 64u + bit_index);
            vm_reconcile_typed_register(reg);
            dirty &= dirty - 1u;
        }
    }
}

static inline TypedRegisterWindow*
orus_jit_native_active_window(struct VM* vm_instance) {
    if (!vm_instance) {
        return NULL;
    }
    if (vm_instance->typed_regs.active_window) {
        return vm_instance->typed_regs.active_window;
    }
    return &vm_instance->typed_regs.root_window;
}

static inline void
orus_jit_helper_safepoint(struct VM* vm_instance) {
    if (!vm_instance) {
        return;
    }
    g_orus_jit_helper_safepoint_count++;
    (void)orus_jit_native_safepoint(vm_instance);
}

size_t
orus_jit_helper_safepoint_count(void) {
    return g_orus_jit_helper_safepoint_count;
}

void
orus_jit_helper_safepoint_reset(void) {
    g_orus_jit_helper_safepoint_count = 0u;
}

static void
orus_jit_native_flush_typed_range(struct VM* vm_instance,
                                  uint16_t spill_base,
                                  uint16_t spill_count) {
    if (!vm_instance || spill_count == 0u) {
        return;
    }

    uint32_t start = spill_base;
    uint32_t end = start + (uint32_t)spill_count;
    if (start >= REGISTER_COUNT) {
        start = REGISTER_COUNT;
    }
    if (end > REGISTER_COUNT) {
        end = REGISTER_COUNT;
    }
    if (start >= end) {
        return;
    }

    TypedRegisterWindow* window = vm_instance->typed_regs.active_window
                                      ? vm_instance->typed_regs.active_window
                                      : &vm_instance->typed_regs.root_window;
    if (!window) {
        return;
    }

    for (uint32_t index = start; index < end; ++index) {
        uint16_t reg = (uint16_t)index;
        if (!typed_window_slot_live(window, reg)) {
            continue;
        }
        vm_reconcile_typed_register(reg);
    }
}

static size_t
orus_jit_program_find_index(const OrusJitIRProgram* program,
                            uint32_t bytecode_offset) {
    if (!program || !program->instructions) {
        return SIZE_MAX;
    }
    for (size_t i = 0; i < program->count; ++i) {
        if (program->instructions[i].bytecode_offset == bytecode_offset) {
            return i;
        }
    }
    return SIZE_MAX;
}

static bool
orus_jit_native_safepoint(struct VM* vm_instance) {
    if (!vm_instance) {
        return false;
    }

    OrusJitNativeFrame* frame = vm_instance->jit_native_frame_top;
    bool slow_path = vm_instance->jit_native_slow_path_pending;
    if (frame && frame->slow_path_requested) {
        slow_path = true;
    }

    TypedRegisterWindow* active_window = orus_jit_native_active_window(vm_instance);
    orus_jit_native_flush_active_window(vm_instance);

    size_t gc_before = vm_instance->gcCount;
    GC_SAFEPOINT(vm_instance);
    bool collected = vm_instance->gcCount != gc_before;

    if (collected) {
        slow_path = true;
    }

    if (frame) {
        frame->active_window = active_window;
        frame->window_version = vm_instance->typed_regs.window_version;
        frame->slow_path_requested = slow_path;
    }
    vm_instance->jit_native_slow_path_pending = slow_path;

    return !slow_path;
}

static void
orus_jit_native_propagate_runtime_error(struct VM* vm_instance,
                                        OrusJitNativeFrame* frame) {
    if (!vm_instance || !IS_ERROR(vm_instance->lastError)) {
        return;
    }

    orus_jit_native_flush_active_window(vm_instance);

    if (vm_instance->tryFrameCount > 0) {
        TryFrame try_frame = vm_instance->tryFrames[--vm_instance->tryFrameCount];
        vm_unwind_to_stack_depth(try_frame.stackDepth);
        vm_instance->ip = try_frame.handler;
        if (try_frame.catchRegister != TRY_CATCH_REGISTER_NONE) {
            vm_set_register_safe(try_frame.catchRegister, vm_instance->lastError);
        }
        vm_set_error_report_pending(false);
        vm_instance->lastError = BOOL_VAL(false);
    } else {
        vm_report_unhandled_error();
    }

    vm_instance->jit_native_slow_path_pending = true;
    if (frame) {
        frame->slow_path_requested = true;
    }
}

static void
orus_jit_native_type_bailout(struct VM* vm_instance,
                             OrusJitNativeBlock* block) {
    jit_bailout_and_deopt(vm_instance, block);
}

static bool
jit_read_i32(struct VM* vm_instance, uint16_t reg, int32_t* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.i32_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_I32(value)) {
        return false;
    }
    *out = AS_I32(value);
    vm_cache_i32_typed(reg, *out);
    return true;
}

static bool
jit_read_i64(struct VM* vm_instance, uint16_t reg, int64_t* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.i64_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_I64(value)) {
        return false;
    }
    *out = AS_I64(value);
    vm_store_i64_typed_hot(reg, *out);
    return true;
}

static bool
jit_read_u32(struct VM* vm_instance, uint16_t reg, uint32_t* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.u32_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_U32(value)) {
        return false;
    }
    *out = AS_U32(value);
    vm_store_u32_typed_hot(reg, *out);
    return true;
}

static bool
jit_read_u64(struct VM* vm_instance, uint16_t reg, uint64_t* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.u64_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_U64(value)) {
        return false;
    }
    *out = AS_U64(value);
    vm_store_u64_typed_hot(reg, *out);
    return true;
}

static bool
jit_read_f64(struct VM* vm_instance, uint16_t reg, double* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.f64_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_F64(value)) {
        return false;
    }
    *out = AS_F64(value);
    vm_store_f64_typed_hot(reg, *out);
    return true;
}

static bool
jit_read_bool(struct VM* vm_instance, uint16_t reg, bool* out) {
    if (!vm_instance || !out) {
        return false;
    }
    if (vm_typed_reg_in_range(reg) && vm_instance->typed_regs.reg_types) {
        uint8_t reg_type = vm_instance->typed_regs.reg_types[reg];
        switch (reg_type) {
            case REG_TYPE_BOOL:
                *out = vm_instance->typed_regs.bool_regs[reg];
                return true;
            case REG_TYPE_I32:
                *out = vm_instance->typed_regs.i32_regs[reg] != 0;
                vm_store_bool_typed_hot(reg, *out);
                return true;
            case REG_TYPE_I64:
                *out = vm_instance->typed_regs.i64_regs[reg] != 0;
                vm_store_bool_typed_hot(reg, *out);
                return true;
            case REG_TYPE_U32:
                *out = vm_instance->typed_regs.u32_regs[reg] != 0u;
                vm_store_bool_typed_hot(reg, *out);
                return true;
            case REG_TYPE_U64:
                *out = vm_instance->typed_regs.u64_regs[reg] != 0u;
                vm_store_bool_typed_hot(reg, *out);
                return true;
            case REG_TYPE_F64:
                *out = vm_instance->typed_regs.f64_regs[reg] != 0.0;
                vm_store_bool_typed_hot(reg, *out);
                return true;
            default:
                break;
        }
    }

    Value value = vm_get_register_safe(reg);
    switch (value.type) {
        case VAL_BOOL:
            *out = AS_BOOL(value);
            break;
        case VAL_I32:
            *out = AS_I32(value) != 0;
            break;
        case VAL_I64:
            *out = AS_I64(value) != 0;
            break;
        case VAL_U32:
            *out = AS_U32(value) != 0;
            break;
        case VAL_U64:
            *out = AS_U64(value) != 0;
            break;
        case VAL_F64:
            *out = AS_F64(value) != 0.0;
            break;
        default:
            return false;
    }

    vm_store_bool_typed_hot(reg, *out);
    return true;
}

static void
jit_store_value(uint16_t dst, OrusJitValueKind kind, Value value) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            if (IS_I32(value)) {
                vm_store_i32_typed_hot(dst, AS_I32(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_I64:
            if (IS_I64(value)) {
                vm_store_i64_typed_hot(dst, AS_I64(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_U32:
            if (IS_U32(value)) {
                vm_store_u32_typed_hot(dst, AS_U32(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_U64:
            if (IS_U64(value)) {
                vm_store_u64_typed_hot(dst, AS_U64(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_F64:
            if (IS_F64(value)) {
                vm_store_f64_typed_hot(dst, AS_F64(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_BOOL:
            if (IS_BOOL(value)) {
                vm_store_bool_typed_hot(dst, AS_BOOL(value));
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_STRING:
            if (IS_STRING(value)) {
                vm_set_register_safe(dst, value);
            } else {
                vm_set_register_safe(dst, value);
            }
            break;
        case ORUS_JIT_VALUE_BOXED:
            vm_set_register_safe(dst, value);
            break;
        case ORUS_JIT_VALUE_KIND_COUNT:
            break;
    }
}

static void
jit_store_constant(struct VM* vm_instance,
                   const Chunk* chunk,
                   const OrusJitIRInstruction* inst) {
    if (!vm_instance || !chunk || !inst) {
        return;
    }
    if (inst->operands.load_const.constant_index >= (uint16_t)chunk->constants.count) {
        return;
    }
    Value value = chunk->constants.values[inst->operands.load_const.constant_index];
    jit_store_value(inst->operands.load_const.dst_reg,
                    inst->value_kind, value);
}

static void
jit_move_typed(struct VM* vm_instance, const OrusJitIRInstruction* inst) {
    if (!vm_instance || !inst) {
        return;
    }
    uint16_t dst = inst->operands.move.dst_reg;
    uint16_t src = inst->operands.move.src_reg;
    switch (inst->value_kind) {
        case ORUS_JIT_VALUE_I32:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_i32_typed_hot(dst, vm_instance->typed_regs.i32_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_i32_typed_hot(dst, AS_I32(v));
            }
            break;
        case ORUS_JIT_VALUE_I64:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_i64_typed_hot(dst, vm_instance->typed_regs.i64_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_i64_typed_hot(dst, AS_I64(v));
            }
            break;
        case ORUS_JIT_VALUE_U32:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_u32_typed_hot(dst, vm_instance->typed_regs.u32_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_u32_typed_hot(dst, AS_U32(v));
            }
            break;
        case ORUS_JIT_VALUE_U64:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_u64_typed_hot(dst, vm_instance->typed_regs.u64_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_u64_typed_hot(dst, AS_U64(v));
            }
            break;
        case ORUS_JIT_VALUE_F64:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_f64_typed_hot(dst, vm_instance->typed_regs.f64_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_f64_typed_hot(dst, AS_F64(v));
            }
            break;
        case ORUS_JIT_VALUE_BOOL:
            if (vm_typed_reg_in_range(src) && vm_typed_reg_in_range(dst)) {
                vm_store_bool_typed_hot(dst, vm_instance->typed_regs.bool_regs[src]);
            } else {
                Value v = vm_get_register_safe(src);
                vm_store_bool_typed_hot(dst, AS_BOOL(v));
            }
            break;
        case ORUS_JIT_VALUE_STRING: {
            Value v = vm_get_register_safe(src);
            if (!IS_STRING(v)) {
                vm_set_register_safe(dst, v);
            } else {
                vm_set_register_safe(dst, v);
            }
            break;
        }
        case ORUS_JIT_VALUE_BOXED:
        case ORUS_JIT_VALUE_KIND_COUNT:
            break;
    }
}

static void
jit_move_value(struct VM* vm_instance, const OrusJitIRInstruction* inst) {
    if (!vm_instance || !inst) {
        return;
    }
    uint16_t dst = inst->operands.move.dst_reg;
    uint16_t src = inst->operands.move.src_reg;
    Value value = vm_get_register_safe(src);
    vm_set_register_safe(dst, value);
}

static OrusJitValueKind
jit_value_kind_from_value(Value value) {
    if (IS_I32(value)) {
        return ORUS_JIT_VALUE_I32;
    }
    if (IS_I64(value)) {
        return ORUS_JIT_VALUE_I64;
    }
    if (IS_U32(value)) {
        return ORUS_JIT_VALUE_U32;
    }
    if (IS_U64(value)) {
        return ORUS_JIT_VALUE_U64;
    }
    if (IS_F64(value)) {
        return ORUS_JIT_VALUE_F64;
    }
    if (IS_BOOL(value)) {
        return ORUS_JIT_VALUE_BOOL;
    }
    if (IS_STRING(value)) {
        return ORUS_JIT_VALUE_STRING;
    }
    return ORUS_JIT_VALUE_BOXED;
}

static bool
orus_jit_native_load_value_const(struct VM* vm_instance,
                                 OrusJitNativeBlock* block,
                                 uint16_t dst,
                                 uint16_t constant_index,
                                 uint32_t expected_kind) {
    if (!vm_instance || !block) {
        return false;
    }

    const Chunk* chunk = (const Chunk*)block->program.source_chunk;
    if (!chunk || constant_index >= (uint16_t)chunk->constants.count) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    Value constant = chunk->constants.values[constant_index];
    OrusJitValueKind actual_kind = jit_value_kind_from_value(constant);
    OrusJitValueKind expected =
        (expected_kind < (uint32_t)ORUS_JIT_VALUE_KIND_COUNT)
            ? (OrusJitValueKind)expected_kind
            : ORUS_JIT_VALUE_BOXED;

    if (expected != ORUS_JIT_VALUE_BOXED && actual_kind != expected) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    OrusJitValueKind store_kind =
        (expected == ORUS_JIT_VALUE_BOXED) ? actual_kind : expected;
    jit_store_value(dst, store_kind, constant);

    if (store_kind == ORUS_JIT_VALUE_BOXED) {
        vm_set_register_safe(dst, constant);
    }

    return true;
}

static bool
orus_jit_native_load_string_const(struct VM* vm_instance,
                                  OrusJitNativeBlock* block,
                                  uint16_t dst,
                                  ObjString* string_value) {
    if (!vm_instance || !string_value) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst, STRING_VAL(string_value));
    return true;
}

static bool
orus_jit_native_move_string(struct VM* vm_instance,
                            OrusJitNativeBlock* block,
                            uint16_t dst,
                            uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    Value value = vm_get_register_safe(src);
    if (!IS_STRING(value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst, value);
    return true;
}

static bool
orus_jit_native_move_value(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           uint16_t dst,
                           uint16_t src) {
    (void)block;
    if (!vm_instance) {
        return false;
    }

    Value value = vm_get_register_safe(src);
    vm_set_register_safe(dst, value);
    return true;
}

static ObjString*
orus_jit_value_to_string_obj(Value value) {
    if (IS_STRING(value)) {
        return AS_STRING(value);
    }

    uint8_t cache_kind = 0u;
    uint64_t cache_bits = 0u;
    bool cacheable = orus_jit_inline_tostring_signature(value, &cache_kind, &cache_bits);
    if (cacheable) {
        ObjString* cached = orus_jit_inline_tostring_cache_lookup(cache_kind, cache_bits);
        if (cached) {
            return cached;
        }
    }

    char buffer[64];
    if (IS_I32(value)) {
        snprintf(buffer, sizeof(buffer), "%d", AS_I32(value));
    } else if (IS_I64(value)) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_I64(value));
    } else if (IS_U32(value)) {
        snprintf(buffer, sizeof(buffer), "%u", AS_U32(value));
    } else if (IS_U64(value)) {
        snprintf(buffer, sizeof(buffer), "%llu",
                 (unsigned long long)AS_U64(value));
    } else if (IS_F64(value)) {
        snprintf(buffer, sizeof(buffer), "%g", AS_F64(value));
    } else if (IS_BOOL(value)) {
        snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(value) ? "true" : "false");
    } else {
        snprintf(buffer, sizeof(buffer), "nil");
    }

    size_t length = strlen(buffer);
    ObjString* result = cacheable ? intern_string(buffer, (int)length)
                                  : allocateString(buffer, (int)length);
    if (cacheable && result) {
        orus_jit_inline_tostring_cache_store(cache_kind, cache_bits, result);
    }
    return result;
}

static bool
orus_jit_native_coerce_string(struct VM* vm_instance,
                              Value value,
                              ObjString** out_string) {
    if (!out_string) {
        return false;
    }

    ObjString* result = NULL;
    if (IS_STRING(value)) {
        result = AS_STRING(value);
    } else {
        result = orus_jit_value_to_string_obj(value);
    }

    if (!result) {
        orus_jit_native_request_slow_path(vm_instance);
        return false;
    }

    *out_string = result;
    return true;
}

static bool
orus_jit_native_concat_string(struct VM* vm_instance,
                              OrusJitNativeBlock* block,
                              uint16_t dst,
                              uint16_t lhs,
                              uint16_t rhs) {
    if (!vm_instance) {
        return false;
    }

    (void)block;

    Value left_value = vm_get_register_safe(lhs);
    Value right_value = vm_get_register_safe(rhs);

    ObjString* left_string = NULL;
    ObjString* right_string = NULL;
    if (!orus_jit_native_coerce_string(vm_instance, left_value, &left_string) ||
        !orus_jit_native_coerce_string(vm_instance, right_value, &right_string)) {
        return false;
    }

    ObjString* result = rope_concat_strings(left_string, right_string);
    if (!result) {
        orus_jit_native_request_slow_path(vm_instance);
        return false;
    }

    vm_set_register_safe(dst, STRING_VAL(result));
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_vector_pair(struct VM* vm_instance,
                            OrusJitNativeBlock* block,
                            const OrusJitIRInstruction* head,
                            const OrusJitIRInstruction* tail) {
    if (!vm_instance || !block || !head || !tail) {
        return false;
    }
    OrusJitIROpcode opcode = head->opcode;
    switch (head->value_kind) {
        case ORUS_JIT_VALUE_I32: {
            int32_t lhs_vals[2];
            int32_t rhs_vals[2];
            if (!jit_read_i32(vm_instance, head->operands.arithmetic.lhs_reg, &lhs_vals[0]) ||
                !jit_read_i32(vm_instance, tail->operands.arithmetic.lhs_reg, &lhs_vals[1]) ||
                !jit_read_i32(vm_instance, head->operands.arithmetic.rhs_reg, &rhs_vals[0]) ||
                !jit_read_i32(vm_instance, tail->operands.arithmetic.rhs_reg, &rhs_vals[1])) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            int32_t results[2];
#if defined(__SSE2__)
            if (orus_jit_opcode_is_mul(opcode)) {
                results[0] = lhs_vals[0] * rhs_vals[0];
                results[1] = lhs_vals[1] * rhs_vals[1];
            } else {
                __m128i lhs_vec = _mm_set_epi32(0, 0, lhs_vals[1], lhs_vals[0]);
                __m128i rhs_vec = _mm_set_epi32(0, 0, rhs_vals[1], rhs_vals[0]);
                __m128i out_vec;
                if (orus_jit_opcode_is_add(opcode)) {
                    out_vec = _mm_add_epi32(lhs_vec, rhs_vec);
                } else if (orus_jit_opcode_is_sub(opcode)) {
                    out_vec = _mm_sub_epi32(lhs_vec, rhs_vec);
                } else {
                    jit_bailout_and_deopt(vm_instance, block);
                    return false;
                }
                int32_t tmp[4];
                _mm_storeu_si128((__m128i*)tmp, out_vec);
                results[0] = tmp[0];
                results[1] = tmp[1];
            }
#else
            for (int idx = 0; idx < 2; ++idx) {
                int32_t lhs = lhs_vals[idx];
                int32_t rhs = rhs_vals[idx];
                if (orus_jit_opcode_is_add(opcode)) {
                    results[idx] = lhs + rhs;
                } else if (orus_jit_opcode_is_sub(opcode)) {
                    results[idx] = lhs - rhs;
                } else if (orus_jit_opcode_is_mul(opcode)) {
                    results[idx] = lhs * rhs;
                } else {
                    jit_bailout_and_deopt(vm_instance, block);
                    return false;
                }
            }
#endif
            vm_store_i32_typed_hot(head->operands.arithmetic.dst_reg, results[0]);
            vm_store_i32_typed_hot(tail->operands.arithmetic.dst_reg, results[1]);
            vm_set_register_safe(head->operands.arithmetic.dst_reg,
                                 I32_VAL(results[0]));
            vm_set_register_safe(tail->operands.arithmetic.dst_reg,
                                 I32_VAL(results[1]));
            return true;
        }
        case ORUS_JIT_VALUE_F64: {
            double lhs_vals[2];
            double rhs_vals[2];
            if (!jit_read_f64(vm_instance, head->operands.arithmetic.lhs_reg, &lhs_vals[0]) ||
                !jit_read_f64(vm_instance, tail->operands.arithmetic.lhs_reg, &lhs_vals[1]) ||
                !jit_read_f64(vm_instance, head->operands.arithmetic.rhs_reg, &rhs_vals[0]) ||
                !jit_read_f64(vm_instance, tail->operands.arithmetic.rhs_reg, &rhs_vals[1])) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            double results[2];
#if defined(__SSE2__)
            __m128d lhs_vec = _mm_set_pd(lhs_vals[1], lhs_vals[0]);
            __m128d rhs_vec = _mm_set_pd(rhs_vals[1], rhs_vals[0]);
            __m128d out_vec;
            if (orus_jit_opcode_is_add(opcode)) {
                out_vec = _mm_add_pd(lhs_vec, rhs_vec);
            } else if (orus_jit_opcode_is_sub(opcode)) {
                out_vec = _mm_sub_pd(lhs_vec, rhs_vec);
            } else if (orus_jit_opcode_is_mul(opcode)) {
                out_vec = _mm_mul_pd(lhs_vec, rhs_vec);
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            _mm_storeu_pd(results, out_vec);
#else
            for (int idx = 0; idx < 2; ++idx) {
                double lhs = lhs_vals[idx];
                double rhs = rhs_vals[idx];
                if (orus_jit_opcode_is_add(opcode)) {
                    results[idx] = lhs + rhs;
                } else if (orus_jit_opcode_is_sub(opcode)) {
                    results[idx] = lhs - rhs;
                } else if (orus_jit_opcode_is_mul(opcode)) {
                    results[idx] = lhs * rhs;
                } else {
                    jit_bailout_and_deopt(vm_instance, block);
                    return false;
                }
            }
#endif
            vm_store_f64_typed_hot(head->operands.arithmetic.dst_reg, results[0]);
            vm_store_f64_typed_hot(tail->operands.arithmetic.dst_reg, results[1]);
            vm_set_register_safe(head->operands.arithmetic.dst_reg,
                                 F64_VAL(results[0]));
            vm_set_register_safe(tail->operands.arithmetic.dst_reg,
                                 F64_VAL(results[1]));
            return true;
        }
        default:
            break;
    }
    return false;
}

static bool
orus_jit_native_to_string(struct VM* vm_instance,
                          OrusJitNativeBlock* block,
                          uint16_t dst,
                          uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    (void)block;

    Value val = vm_get_register_safe(src);
    ObjString* result = NULL;
    if (!orus_jit_native_coerce_string(vm_instance, val, &result)) {
        return false;
    }

    vm_set_register_safe(dst, STRING_VAL(result));
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_type_of(struct VM* vm_instance,
                        OrusJitNativeBlock* block,
                        uint16_t dst,
                        uint16_t value_reg) {
    if (!vm_instance) {
        return false;
    }

    (void)block;

    Value value = vm_get_register_safe(value_reg);
    Value result;
    if (!builtin_typeof(value, &result)) {
        orus_jit_native_request_slow_path(vm_instance);
        return false;
    }

    vm_set_register_safe(dst, result);
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_is_type(struct VM* vm_instance,
                        OrusJitNativeBlock* block,
                        uint16_t dst,
                        uint16_t value_reg,
                        uint16_t type_reg) {
    if (!vm_instance) {
        return false;
    }

    (void)block;

    Value value = vm_get_register_safe(value_reg);
    Value type_identifier = vm_get_register_safe(type_reg);
    Value result;
    if (!builtin_istype(value, type_identifier, &result)) {
        orus_jit_native_request_slow_path(vm_instance);
        return false;
    }

    bool bool_result = IS_BOOL(result) ? AS_BOOL(result) : false;
    vm_store_bool_typed_hot(dst, bool_result);
    vm_set_register_safe(dst, BOOL_VAL(bool_result));
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_range(struct VM* vm_instance,
                      OrusJitNativeBlock* block,
                      uint16_t dst,
                      uint16_t arg_count,
                      const uint16_t* arg_regs) {
    if (!vm_instance) {
        return false;
    }

    if ((arg_count == 0u) || (arg_count > 3u) || (!arg_regs && arg_count > 0u)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    Value args_storage[3];
    for (uint16_t i = 0u; i < arg_count; ++i) {
        uint16_t reg = arg_regs[i];
        args_storage[i] = vm_get_register_safe(reg);
    }

    Value* args_ptr = (arg_count > 0u) ? args_storage : NULL;
    Value result;
    if (!builtin_range(args_ptr, (int)arg_count, &result)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst, result);
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_get_iter(struct VM* vm_instance,
                         OrusJitNativeBlock* block,
                         uint16_t dst,
                         uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    Value iterable = vm_get_register_safe(src);
    vm_set_register_safe(dst, iterable);

    if (IS_RANGE_ITERATOR(iterable) || IS_ARRAY_ITERATOR(iterable)) {
        return true;
    }

    if (IS_I32(iterable) || IS_I64(iterable) || IS_U32(iterable) ||
        IS_U64(iterable)) {
        int64_t count = 0;
        if (IS_I32(iterable)) {
            count = (int64_t)AS_I32(iterable);
        } else if (IS_I64(iterable)) {
            count = AS_I64(iterable);
        } else if (IS_U32(iterable)) {
            count = (int64_t)AS_U32(iterable);
        } else {
            uint64_t unsigned_count = AS_U64(iterable);
            if (unsigned_count > (uint64_t)INT64_MAX) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            count = (int64_t)unsigned_count;
        }

        if (count < 0) {
            jit_bailout_and_deopt(vm_instance, block);
            return false;
        }

        ObjRangeIterator* iterator = allocateRangeIterator(0, count, 1);
        if (!iterator) {
            jit_bailout_and_deopt(vm_instance, block);
            return false;
        }

        vm_set_register_safe(dst, RANGE_ITERATOR_VAL(iterator));
        orus_jit_helper_safepoint(vm_instance);
        return true;
    }

    if (IS_ARRAY(iterable)) {
        ObjArray* array = AS_ARRAY(iterable);
        ObjArrayIterator* iterator = allocateArrayIterator(array);
        if (!iterator) {
            jit_bailout_and_deopt(vm_instance, block);
            return false;
        }

        vm_set_register_safe(dst, ARRAY_ITERATOR_VAL(iterator));
        orus_jit_helper_safepoint(vm_instance);
        return true;
    }

    jit_bailout_and_deopt(vm_instance, block);
    return false;
}

static bool
orus_jit_native_iter_next(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           uint16_t value_reg,
                           uint16_t iterator_reg,
                           uint16_t has_value_reg) {
    if (!vm_instance) {
        return false;
    }

    Value iterator_value = vm_get_register_safe(iterator_reg);
    bool has_value = false;

    if (IS_RANGE_ITERATOR(iterator_value)) {
        ObjRangeIterator* it = AS_RANGE_ITERATOR(iterator_value);
        if (!it) {
            jit_bailout_and_deopt(vm_instance, block);
            return false;
        }

        int64_t current = it->current;
        int64_t end = it->end;
        int64_t step = it->step;
        if (step != 0) {
            bool forward_in_range = (step > 0) && (current < end);
            bool backward_in_range = (step < 0) && (current > end);
            if (forward_in_range || backward_in_range) {
                has_value = true;
                it->current = current + step;
                if (vm_typed_reg_in_range(value_reg)) {
                    vm_store_i64_typed_hot(value_reg, current);
                } else {
                    vm_set_register_safe(value_reg, I64_VAL(current));
                }
            }
        }
        vm_store_bool_register(has_value_reg, has_value);
        return true;
    }

    if (IS_ARRAY_ITERATOR(iterator_value)) {
        ObjArrayIterator* it = AS_ARRAY_ITERATOR(iterator_value);
        ObjArray* array = it ? it->array : NULL;
        if (array && it->index < array->length) {
            Value element = array->elements[it->index++];
            has_value = true;
            bool stored_typed = false;
            if (vm_typed_reg_in_range(value_reg)) {
                switch (element.type) {
                    case VAL_I32:
                        vm_store_i32_typed_hot(value_reg, AS_I32(element));
                        stored_typed = true;
                        break;
                    case VAL_I64:
                        vm_store_i64_typed_hot(value_reg, AS_I64(element));
                        stored_typed = true;
                        break;
                    case VAL_U32:
                        vm_store_u32_typed_hot(value_reg, AS_U32(element));
                        stored_typed = true;
                        break;
                    case VAL_U64:
                        vm_store_u64_typed_hot(value_reg, AS_U64(element));
                        stored_typed = true;
                        break;
                    case VAL_BOOL:
                        vm_store_bool_typed_hot(value_reg, AS_BOOL(element));
                        stored_typed = true;
                        break;
                    default:
                        break;
                }
            }
            if (!stored_typed) {
                vm_set_register_safe(value_reg, element);
            }
        }

        vm_store_bool_register(has_value_reg, has_value);
        return true;
    }

    jit_bailout_and_deopt(vm_instance, block);
    return false;
}

static bool
orus_jit_native_time_stamp(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           uint16_t dst) {
    (void)block;
    if (!vm_instance) {
        return false;
    }

    double timestamp = builtin_timestamp();
    vm_store_f64_typed_hot(dst, timestamp);
    vm_set_register_safe(dst, F64_VAL(timestamp));
    return true;
}

static bool
orus_jit_native_array_push(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           uint16_t array_reg,
                           uint16_t value_reg) {
    if (!vm_instance) {
        return false;
    }

    Value array_value = vm_get_register_safe(array_reg);
    Value element = vm_get_register_safe(value_reg);
    if (!builtin_array_push(array_value, element)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_array_pop(struct VM* vm_instance,
                          OrusJitNativeBlock* block,
                          uint16_t dst_reg,
                          uint16_t array_reg) {
    if (!vm_instance) {
        return false;
    }

    Value array_value = vm_get_register_safe(array_reg);
    Value popped;
    if (!builtin_array_pop(array_value, &popped)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst_reg, popped);
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_make_array(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           const OrusJitIRInstruction* inst) {
    if (!vm_instance || !block || !inst) {
        return false;
    }

    uint16_t dst_reg = inst->operands.make_array.dst_reg;
    uint16_t first_reg = inst->operands.make_array.first_reg;
    uint16_t count = inst->operands.make_array.count;

    if ((uint32_t)first_reg + (uint32_t)count > (uint32_t)REGISTER_COUNT) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    ObjArray* array = allocateArray((int)count);
    if (!array || !array->elements) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    for (uint16_t i = 0u; i < count; ++i) {
        arrayEnsureCapacity(array, (int)(i + 1u));
        array->elements[i] =
            vm_get_register_safe((uint16_t)(first_reg + i));
    }
    array->length = count;

    vm_set_register_safe(dst_reg, ARRAY_VAL(array));
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_enum_new(struct VM* vm_instance,
                         OrusJitNativeBlock* block,
                         const OrusJitIRInstruction* inst) {
    if (!vm_instance || !block || !inst) {
        return false;
    }

    const Chunk* chunk = (const Chunk*)block->program.source_chunk;
    if (!chunk || chunk->count <= 0) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    uint16_t dst_reg = inst->operands.enum_new.dst_reg;
    uint16_t variant_index = inst->operands.enum_new.variant_index;
    uint16_t payload_count = inst->operands.enum_new.payload_count;
    uint16_t payload_start = inst->operands.enum_new.payload_start;
    uint16_t type_const_index = inst->operands.enum_new.type_const_index;
    uint16_t variant_const_index = inst->operands.enum_new.variant_const_index;

    if (type_const_index >= (uint16_t)chunk->constants.count ||
        variant_const_index >= (uint16_t)chunk->constants.count) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if ((uint32_t)payload_start + (uint32_t)payload_count >
        (uint32_t)REGISTER_COUNT) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    Value type_constant = chunk->constants.values[type_const_index];
    if (!IS_STRING(type_constant)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    ObjString* type_name = AS_STRING(type_constant);
    ObjString* variant_name = NULL;
    Value variant_constant = chunk->constants.values[variant_const_index];
    if (IS_STRING(variant_constant)) {
        variant_name = AS_STRING(variant_constant);
    }

    Value payload_storage[UINT8_MAX];
    const Value* payload_ptr = NULL;
    if (payload_count > 0u) {
        for (uint16_t i = 0u; i < payload_count; ++i) {
            payload_storage[i] =
                vm_get_register_safe((uint16_t)(payload_start + i));
        }
        payload_ptr = payload_storage;
    }

    TaggedUnionSpec spec = {
        .type_name = type_name ? string_get_chars(type_name) : NULL,
        .variant_name = variant_name ? string_get_chars(variant_name) : NULL,
        .variant_index = (int)variant_index,
        .payload = payload_ptr,
        .payload_count = (int)payload_count,
    };

    Value enum_value;
    if (!vm_make_tagged_union(&spec, &enum_value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst_reg, enum_value);
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_print(struct VM* vm_instance,
                      OrusJitNativeBlock* block,
                      uint16_t first_reg,
                      uint16_t arg_count,
                      uint16_t newline_flag) {
    if (!vm_instance) {
        return false;
    }

    if (arg_count > (uint16_t)FRAME_REGISTERS) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    Value args_storage[FRAME_REGISTERS];
    Value* args_ptr = NULL;
    if (arg_count > 0u) {
        args_ptr = args_storage;
        for (uint16_t i = 0; i < arg_count; ++i) {
            args_storage[i] = vm_get_register_safe((uint16_t)(first_reg + i));
        }
    }

    builtin_print(args_ptr, (int)arg_count, newline_flag != 0u);
    return true;
}

static bool
orus_jit_native_assert_eq(struct VM* vm_instance,
                          OrusJitNativeBlock* block,
                          uint16_t dst,
                          uint16_t label_reg,
                          uint16_t actual_reg,
                          uint16_t expected_reg) {
    if (!vm_instance) {
        return false;
    }

    Value label = vm_get_register_safe(label_reg);
    Value actual = vm_get_register_safe(actual_reg);
    Value expected = vm_get_register_safe(expected_reg);
    char* failure_message = NULL;
    bool ok = builtin_assert_eq(label, actual, expected, &failure_message);
    if (!ok) {
        free(failure_message);
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    free(failure_message);
    vm_store_bool_typed_hot(dst, true);
    return true;
}

static bool
orus_jit_native_call_native(struct VM* vm_instance,
                            OrusJitNativeBlock* block,
                            uint16_t dst,
                            uint16_t first_arg_reg,
                            uint16_t arg_count,
                            uint16_t native_index) {
    if (!vm_instance) {
        return false;
    }

    if (native_index >= (uint16_t)vm_instance->nativeFunctionCount) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    NativeFunction* native = &vm_instance->nativeFunctions[native_index];
    if (!native->function) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (native->arity >= 0 && (uint16_t)native->arity != arg_count) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (arg_count > (uint16_t)FRAME_REGISTERS) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    Value args_storage[FRAME_REGISTERS];
    Value* args_ptr = NULL;
    if (arg_count > 0u) {
        args_ptr = args_storage;
        for (uint16_t i = 0; i < arg_count; ++i) {
            args_storage[i] =
                vm_get_register_safe((uint16_t)(first_arg_reg + i));
        }
    }

    profileFunctionHit((void*)native, true);
    Value result = native->function((int)arg_count, args_ptr);
    vm_set_register_safe(dst, result);
    orus_jit_helper_safepoint(vm_instance);
    return true;
}

static bool
orus_jit_native_call_foreign(struct VM* vm_instance,
                             OrusJitNativeBlock* block,
                             uint16_t dst,
                             uint16_t first_arg_reg,
                             uint16_t arg_count,
                             uint16_t foreign_index) {
    // The runtime currently reuses the native function table for foreign
    // bindings. This helper mirrors the native call path so the translator and
    // backends can specialize `OP_CALL_FOREIGN` without forcing the
    // interpreter to stay resident.
    return orus_jit_native_call_native(vm_instance, block, dst, first_arg_reg,
                                       arg_count, foreign_index);
}

static bool
orus_jit_native_compare_op(struct VM* vm_instance,
                           OrusJitNativeBlock* block,
                           OrusJitIROpcode opcode,
                           uint16_t dst,
                           uint16_t lhs,
                           uint16_t rhs) {
    if (!vm_instance) {
        return false;
    }

    bool result = false;
    switch (opcode) {
        case ORUS_JIT_IR_OP_LT_I32:
        case ORUS_JIT_IR_OP_LE_I32:
        case ORUS_JIT_IR_OP_GT_I32:
        case ORUS_JIT_IR_OP_GE_I32:
        case ORUS_JIT_IR_OP_EQ_I32:
        case ORUS_JIT_IR_OP_NE_I32: {
            int32_t lhs_value = 0;
            int32_t rhs_value = 0;
            if (!jit_read_i32(vm_instance, lhs, &lhs_value) ||
                !jit_read_i32(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            switch (opcode) {
                case ORUS_JIT_IR_OP_LT_I32:
                    result = lhs_value < rhs_value;
                    break;
                case ORUS_JIT_IR_OP_LE_I32:
                    result = lhs_value <= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GT_I32:
                    result = lhs_value > rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GE_I32:
                    result = lhs_value >= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_EQ_I32:
                    result = lhs_value == rhs_value;
                    break;
                case ORUS_JIT_IR_OP_NE_I32:
                    result = lhs_value != rhs_value;
                    break;
                default:
                    break;
            }
            break;
        }
        case ORUS_JIT_IR_OP_LT_I64:
        case ORUS_JIT_IR_OP_LE_I64:
        case ORUS_JIT_IR_OP_GT_I64:
        case ORUS_JIT_IR_OP_GE_I64:
        case ORUS_JIT_IR_OP_EQ_I64:
        case ORUS_JIT_IR_OP_NE_I64: {
            int64_t lhs_value = 0;
            int64_t rhs_value = 0;
            if (!jit_read_i64(vm_instance, lhs, &lhs_value) ||
                !jit_read_i64(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            switch (opcode) {
                case ORUS_JIT_IR_OP_LT_I64:
                    result = lhs_value < rhs_value;
                    break;
                case ORUS_JIT_IR_OP_LE_I64:
                    result = lhs_value <= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GT_I64:
                    result = lhs_value > rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GE_I64:
                    result = lhs_value >= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_EQ_I64:
                    result = lhs_value == rhs_value;
                    break;
                case ORUS_JIT_IR_OP_NE_I64:
                    result = lhs_value != rhs_value;
                    break;
                default:
                    break;
            }
            break;
        }
        case ORUS_JIT_IR_OP_LT_U32:
        case ORUS_JIT_IR_OP_LE_U32:
        case ORUS_JIT_IR_OP_GT_U32:
        case ORUS_JIT_IR_OP_GE_U32:
        case ORUS_JIT_IR_OP_EQ_U32:
        case ORUS_JIT_IR_OP_NE_U32: {
            uint32_t lhs_value = 0;
            uint32_t rhs_value = 0;
            if (!jit_read_u32(vm_instance, lhs, &lhs_value) ||
                !jit_read_u32(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            switch (opcode) {
                case ORUS_JIT_IR_OP_LT_U32:
                    result = lhs_value < rhs_value;
                    break;
                case ORUS_JIT_IR_OP_LE_U32:
                    result = lhs_value <= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GT_U32:
                    result = lhs_value > rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GE_U32:
                    result = lhs_value >= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_EQ_U32:
                    result = lhs_value == rhs_value;
                    break;
                case ORUS_JIT_IR_OP_NE_U32:
                    result = lhs_value != rhs_value;
                    break;
                default:
                    break;
            }
            break;
        }
        case ORUS_JIT_IR_OP_LT_U64:
        case ORUS_JIT_IR_OP_LE_U64:
        case ORUS_JIT_IR_OP_GT_U64:
        case ORUS_JIT_IR_OP_GE_U64:
        case ORUS_JIT_IR_OP_EQ_U64:
        case ORUS_JIT_IR_OP_NE_U64: {
            uint64_t lhs_value = 0;
            uint64_t rhs_value = 0;
            if (!jit_read_u64(vm_instance, lhs, &lhs_value) ||
                !jit_read_u64(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            switch (opcode) {
                case ORUS_JIT_IR_OP_LT_U64:
                    result = lhs_value < rhs_value;
                    break;
                case ORUS_JIT_IR_OP_LE_U64:
                    result = lhs_value <= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GT_U64:
                    result = lhs_value > rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GE_U64:
                    result = lhs_value >= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_EQ_U64:
                    result = lhs_value == rhs_value;
                    break;
                case ORUS_JIT_IR_OP_NE_U64:
                    result = lhs_value != rhs_value;
                    break;
                default:
                    break;
            }
            break;
        }
        case ORUS_JIT_IR_OP_LT_F64:
        case ORUS_JIT_IR_OP_LE_F64:
        case ORUS_JIT_IR_OP_GT_F64:
        case ORUS_JIT_IR_OP_GE_F64:
        case ORUS_JIT_IR_OP_EQ_F64:
        case ORUS_JIT_IR_OP_NE_F64: {
            double lhs_value = 0.0;
            double rhs_value = 0.0;
            if (!jit_read_f64(vm_instance, lhs, &lhs_value) ||
                !jit_read_f64(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            switch (opcode) {
                case ORUS_JIT_IR_OP_LT_F64:
                    result = lhs_value < rhs_value;
                    break;
                case ORUS_JIT_IR_OP_LE_F64:
                    result = lhs_value <= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GT_F64:
                    result = lhs_value > rhs_value;
                    break;
                case ORUS_JIT_IR_OP_GE_F64:
                    result = lhs_value >= rhs_value;
                    break;
                case ORUS_JIT_IR_OP_EQ_F64:
                    result = lhs_value == rhs_value;
                    break;
                case ORUS_JIT_IR_OP_NE_F64:
                    result = lhs_value != rhs_value;
                    break;
                default:
                    break;
            }
            break;
        }
        case ORUS_JIT_IR_OP_EQ_BOOL:
        case ORUS_JIT_IR_OP_NE_BOOL: {
            Value lhs_raw = vm_get_register_safe(lhs);
            Value rhs_raw = vm_get_register_safe(rhs);
            if (IS_STRING(lhs_raw) || IS_STRING(rhs_raw)) {
                ObjString* lhs_str = NULL;
                ObjString* rhs_str = NULL;
                if (!orus_jit_native_coerce_string(vm_instance, lhs_raw,
                                                   &lhs_str) ||
                    !orus_jit_native_coerce_string(vm_instance, rhs_raw,
                                                   &rhs_str)) {
                    return false;
                }
                bool equals = valuesEqual(STRING_VAL(lhs_str), STRING_VAL(rhs_str));
                result = (opcode == ORUS_JIT_IR_OP_EQ_BOOL) ? equals : !equals;
                orus_jit_helper_safepoint(vm_instance);
                break;
            }

            bool lhs_value = false;
            bool rhs_value = false;
            if (!jit_read_bool(vm_instance, lhs, &lhs_value) ||
                !jit_read_bool(vm_instance, rhs, &rhs_value)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            result = lhs_value == rhs_value;
            if (opcode == ORUS_JIT_IR_OP_NE_BOOL) {
                result = !result;
            }
            break;
        }
        default:
            jit_bailout_and_deopt(vm_instance, block);
            return false;
    }

    vm_store_bool_typed_hot(dst, result);
    return true;
}

static bool
orus_jit_native_convert_i32_to_i64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int32_t value = 0;
    if (!jit_read_i32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i64_typed_hot(dst, (int64_t)value);
    return true;
}

static bool
orus_jit_native_convert_u32_to_u64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint32_t value = 0u;
    if (!jit_read_u32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u64_typed_hot(dst, (uint64_t)value);
    return true;
}

static bool
orus_jit_native_convert_u32_to_i32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint32_t value = 0u;
    if (!jit_read_u32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i32_typed_hot(dst, (int32_t)value);
    return true;
}

static bool
orus_jit_native_convert_i32_to_f64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int32_t value = 0;
    if (!jit_read_i32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_f64_typed_hot(dst, (double)value);
    return true;
}

static bool
orus_jit_native_convert_i64_to_f64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int64_t value = 0;
    if (!jit_read_i64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_f64_typed_hot(dst, (double)value);
    return true;
}

static bool
orus_jit_native_convert_f64_to_i32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    double value = 0.0;
    if (!jit_read_f64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i32_typed_hot(dst, (int32_t)value);
    return true;
}

static bool
orus_jit_native_convert_f64_to_i64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    double value = 0.0;
    if (!jit_read_f64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i64_typed_hot(dst, (int64_t)value);
    return true;
}

static bool
orus_jit_native_convert_f64_to_u32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    double value = 0.0;
    if (!jit_read_f64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value < 0.0 || value > (double)UINT32_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u32_typed_hot(dst, (uint32_t)value);
    return true;
}

static bool
orus_jit_native_convert_u32_to_f64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint32_t value = 0u;
    if (!jit_read_u32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_f64_typed_hot(dst, (double)value);
    return true;
}

static bool
orus_jit_native_convert_i32_to_u32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    (void)block;
    if (!vm_instance) {
        return false;
    }

    int32_t value = 0;
    if (!jit_read_i32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u32_typed_hot(dst, (uint32_t)value);
    return true;
}

static bool
orus_jit_native_convert_i64_to_u32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int64_t value = 0;
    if (!jit_read_i64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value < 0 || value > (int64_t)UINT32_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u32_typed_hot(dst, (uint32_t)value);
    return true;
}

static bool
orus_jit_native_convert_i32_to_u64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int32_t value = 0;
    if (!jit_read_i32(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value < 0) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u64_typed_hot(dst, (uint64_t)value);
    return true;
}

static bool
orus_jit_native_convert_i64_to_u64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    int64_t value = 0;
    if (!jit_read_i64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value < 0) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u64_typed_hot(dst, (uint64_t)value);
    return true;
}

static bool
orus_jit_native_convert_u64_to_i32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint64_t value = 0u;
    if (!jit_read_u64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value > (uint64_t)INT32_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i32_typed_hot(dst, (int32_t)value);
    return true;
}

static bool
orus_jit_native_convert_u64_to_i64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint64_t value = 0u;
    if (!jit_read_u64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value > (uint64_t)INT64_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_i64_typed_hot(dst, (int64_t)value);
    return true;
}

static bool
orus_jit_native_convert_u64_to_u32(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    uint64_t value = 0u;
    if (!jit_read_u64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value > (uint64_t)UINT32_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u32_typed_hot(dst, (uint32_t)value);
    return true;
}

static bool
orus_jit_native_convert_f64_to_u64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    double value = 0.0;
    if (!jit_read_f64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    if (value < 0.0 || value > (double)UINT64_MAX) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_u64_typed_hot(dst, (uint64_t)value);
    return true;
}

static bool
orus_jit_native_convert_u64_to_f64(struct VM* vm_instance,
                                   OrusJitNativeBlock* block,
                                   uint16_t dst,
                                   uint16_t src) {
    (void)block;
    if (!vm_instance) {
        return false;
    }

    uint64_t value = 0u;
    if (!jit_read_u64(vm_instance, src, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_store_f64_typed_hot(dst, (double)value);
    return true;
}

static bool
orus_jit_native_linear_load(struct VM* vm_instance,
                            OrusJitNativeBlock* block,
                            uint32_t raw_kind,
                            uint16_t dst,
                            uint64_t bits) {
    if (!vm_instance) {
        return false;
    }

    OrusJitValueKind kind = (OrusJitValueKind)raw_kind;
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            vm_store_i32_typed_hot(dst, (int32_t)(uint32_t)bits);
            return true;
        case ORUS_JIT_VALUE_I64:
            vm_store_i64_typed_hot(dst, (int64_t)bits);
            return true;
        case ORUS_JIT_VALUE_U32:
            vm_store_u32_typed_hot(dst, (uint32_t)bits);
            return true;
        case ORUS_JIT_VALUE_U64:
            vm_store_u64_typed_hot(dst, (uint64_t)bits);
            return true;
        case ORUS_JIT_VALUE_BOOL:
            vm_store_bool_typed_hot(dst, bits != 0u);
            return true;
        case ORUS_JIT_VALUE_F64: {
            double value = 0.0;
            memcpy(&value, &bits, sizeof(double));
            vm_store_f64_typed_hot(dst, value);
            return true;
        }
        case ORUS_JIT_VALUE_STRING:
            return orus_jit_native_load_string_const(
                vm_instance, block, dst,
                (ObjString*)(uintptr_t)bits);
        case ORUS_JIT_VALUE_BOXED:
        case ORUS_JIT_VALUE_KIND_COUNT:
            break;
    }

    jit_bailout_and_deopt(vm_instance, block);
    return false;
}

static bool
orus_jit_native_linear_move(struct VM* vm_instance,
                            OrusJitNativeBlock* block,
                            uint32_t raw_kind,
                            uint16_t dst,
                            uint16_t src) {
    (void)block;
    if (!vm_instance) {
        return false;
    }

    OrusJitIRInstruction inst;
    memset(&inst, 0, sizeof(inst));
    inst.value_kind = (OrusJitValueKind)raw_kind;
    inst.operands.move.dst_reg = dst;
    inst.operands.move.src_reg = src;
    jit_move_typed(vm_instance, &inst);
    return true;
}

static bool
orus_jit_native_linear_arithmetic(struct VM* vm_instance,
                                  OrusJitNativeBlock* block,
                                  uint32_t raw_opcode,
                                  uint32_t raw_kind,
                                  uint16_t dst,
                                  uint16_t lhs,
                                  uint16_t rhs) {
    if (!vm_instance) {
        return false;
    }

    OrusJitIROpcode opcode = (OrusJitIROpcode)raw_opcode;
    OrusJitValueKind kind = (OrusJitValueKind)raw_kind;

    switch (kind) {
        case ORUS_JIT_VALUE_I32: {
            int32_t left = 0;
            int32_t right = 0;
            if (!jit_read_i32(vm_instance, lhs, &left) ||
                !jit_read_i32(vm_instance, rhs, &right)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            int32_t result = left;
            if (orus_jit_opcode_is_add(opcode)) {
                result = left + right;
            } else if (orus_jit_opcode_is_sub(opcode)) {
                result = left - right;
            } else if (orus_jit_opcode_is_mul(opcode)) {
                result = left * right;
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            vm_store_i32_typed_hot(dst, result);
            return true;
        }
        case ORUS_JIT_VALUE_I64: {
            int64_t left = 0;
            int64_t right = 0;
            if (!jit_read_i64(vm_instance, lhs, &left) ||
                !jit_read_i64(vm_instance, rhs, &right)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            int64_t result = left;
            if (orus_jit_opcode_is_add(opcode)) {
                result = left + right;
            } else if (orus_jit_opcode_is_sub(opcode)) {
                result = left - right;
            } else if (orus_jit_opcode_is_mul(opcode)) {
                result = left * right;
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            vm_store_i64_typed_hot(dst, result);
            return true;
        }
        case ORUS_JIT_VALUE_BOOL:
            jit_bailout_and_deopt(vm_instance, block);
            return false;
        case ORUS_JIT_VALUE_U32: {
            uint32_t left = 0u;
            uint32_t right = 0u;
            if (!jit_read_u32(vm_instance, lhs, &left) ||
                !jit_read_u32(vm_instance, rhs, &right)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            uint32_t result = left;
            if (orus_jit_opcode_is_add(opcode)) {
                result = left + right;
            } else if (orus_jit_opcode_is_sub(opcode)) {
                result = left - right;
            } else if (orus_jit_opcode_is_mul(opcode)) {
                result = left * right;
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            vm_store_u32_typed_hot(dst, result);
            return true;
        }
        case ORUS_JIT_VALUE_U64: {
            uint64_t left = 0u;
            uint64_t right = 0u;
            if (!jit_read_u64(vm_instance, lhs, &left) ||
                !jit_read_u64(vm_instance, rhs, &right)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            uint64_t result = left;
            if (orus_jit_opcode_is_add(opcode)) {
                result = left + right;
            } else if (orus_jit_opcode_is_sub(opcode)) {
                result = left - right;
            } else if (orus_jit_opcode_is_mul(opcode)) {
                result = left * right;
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            vm_store_u64_typed_hot(dst, result);
            return true;
        }
        case ORUS_JIT_VALUE_F64: {
            double left = 0.0;
            double right = 0.0;
            if (!jit_read_f64(vm_instance, lhs, &left) ||
                !jit_read_f64(vm_instance, rhs, &right)) {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            double result = left;
            if (orus_jit_opcode_is_add(opcode)) {
                result = left + right;
            } else if (orus_jit_opcode_is_sub(opcode)) {
                result = left - right;
            } else if (orus_jit_opcode_is_mul(opcode)) {
                result = left * right;
            } else {
                jit_bailout_and_deopt(vm_instance, block);
                return false;
            }
            vm_store_f64_typed_hot(dst, result);
            return true;
        }
        case ORUS_JIT_VALUE_STRING:
        case ORUS_JIT_VALUE_BOXED:
        case ORUS_JIT_VALUE_KIND_COUNT:
            break;
    }

    jit_bailout_and_deopt(vm_instance, block);
    return false;
}

static bool
orus_jit_native_linear_safepoint(struct VM* vm_instance) {
    if (!vm_instance) {
        return false;
    }
    g_orus_jit_helper_safepoint_count++;
    return orus_jit_native_safepoint(vm_instance);
}

#if defined(__x86_64__) || defined(_M_X64)
static const uint8_t MOV_RDI_R12[] = {0x4C, 0x89, 0xE7};
static const uint8_t MOV_RSI_RBX_BYTES[] = {0x48, 0x89, 0xDE};
static const uint8_t CALL_RAX[] = {0xFF, 0xD0};
#endif

static int
orus_jit_native_evaluate_branch_false(struct VM* vm_instance,
                                      OrusJitNativeBlock* block,
                                      uint16_t predicate_reg) {
    if (!vm_instance) {
        return -1;
    }
    bool value = false;
    if (!jit_read_bool(vm_instance, predicate_reg, &value)) {
        jit_bailout_and_deopt(vm_instance, block);
        return -1;
    }
    return value ? 0 : 1;
}

#if defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64)
static int
orus_jit_native_fused_loop_step(struct VM* vm_instance,
                                OrusJitNativeBlock* block,
                                uint32_t raw_kind,
                                uint16_t counter_reg,
                                uint16_t limit_reg,
                                int32_t step,
                                uint32_t raw_compare_kind,
                                int32_t direction) {
    if (!vm_instance) {
        return -1;
    }

    OrusJitValueKind kind = (OrusJitValueKind)raw_kind;
    OrusJitIRLoopCompareKind compare_kind =
        (OrusJitIRLoopCompareKind)raw_compare_kind;

    if (direction == 0 || step == 0 ||
        (direction > 0 && step <= 0) || (direction < 0 && step >= 0) ||
        (compare_kind != ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN &&
         compare_kind != ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN)) {
        jit_bailout_and_deopt(vm_instance, block);
        return -1;
    }

    bool should_branch = false;

    switch (kind) {
        case ORUS_JIT_VALUE_I32: {
            int32_t counter = 0;
            int32_t limit = 0;
            if (!jit_read_i32(vm_instance, counter_reg, &counter) ||
                !jit_read_i32(vm_instance, limit_reg, &limit)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            int32_t updated = 0;
            if (__builtin_add_overflow(counter, step, &updated)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            vm_store_i32_typed_hot(counter_reg, updated);
            should_branch = (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                                ? (updated < limit)
                                : (updated > limit);
            break;
        }
        case ORUS_JIT_VALUE_I64: {
            int64_t counter = 0;
            int64_t limit = 0;
            if (!jit_read_i64(vm_instance, counter_reg, &counter) ||
                !jit_read_i64(vm_instance, limit_reg, &limit)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            int64_t updated = 0;
            int64_t step64 = (int64_t)step;
            if (__builtin_add_overflow(counter, step64, &updated)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            vm_store_i64_typed_hot(counter_reg, updated);
            should_branch = (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                                ? (updated < limit)
                                : (updated > limit);
            break;
        }
        case ORUS_JIT_VALUE_U32: {
            uint32_t counter = 0;
            uint32_t limit = 0;
            if (!jit_read_u32(vm_instance, counter_reg, &counter) ||
                !jit_read_u32(vm_instance, limit_reg, &limit)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            uint32_t magnitude =
                (uint32_t)((step > 0) ? step : -step);
            uint32_t updated = 0;
            if (direction > 0) {
                updated = counter + magnitude;
            } else {
                updated = counter - magnitude;
            }
            vm_store_u32_typed_hot(counter_reg, updated);
            should_branch = (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                                ? (updated < limit)
                                : (updated > limit);
            break;
        }
        case ORUS_JIT_VALUE_U64: {
            uint64_t counter = 0;
            uint64_t limit = 0;
            if (!jit_read_u64(vm_instance, counter_reg, &counter) ||
                !jit_read_u64(vm_instance, limit_reg, &limit)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            uint64_t magnitude =
                (uint64_t)((step > 0) ? step : -step);
            uint64_t updated = 0;
            if (direction > 0) {
                updated = counter + magnitude;
            } else {
                updated = counter - magnitude;
            }
            vm_store_u64_typed_hot(counter_reg, updated);
            should_branch = (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                                ? (updated < limit)
                                : (updated > limit);
            break;
        }
        case ORUS_JIT_VALUE_F64: {
            double counter = 0.0;
            double limit = 0.0;
            if (!jit_read_f64(vm_instance, counter_reg, &counter) ||
                !jit_read_f64(vm_instance, limit_reg, &limit)) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }
            double updated = counter + (double)step;
            vm_store_f64_typed_hot(counter_reg, updated);
            should_branch = (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                                ? (updated < limit)
                                : (updated > limit);
            break;
        }
        case ORUS_JIT_VALUE_BOXED: {
            Value counter_value = vm_get_register_safe(counter_reg);
            Value limit_value = vm_get_register_safe(limit_reg);
            int32_t magnitude = (step > 0) ? step : -step;
            if (magnitude <= 0) {
                jit_bailout_and_deopt(vm_instance, block);
                return -1;
            }

            if (IS_I32(counter_value) && IS_I32(limit_value)) {
                int32_t current = AS_I32(counter_value);
                int32_t limit = AS_I32(limit_value);
                int32_t updated = current;
                if (direction > 0) {
                    if (__builtin_add_overflow(current, magnitude, &updated)) {
                        jit_bailout_and_deopt(vm_instance, block);
                        return -1;
                    }
                } else {
                    if (__builtin_sub_overflow(current, magnitude, &updated)) {
                        jit_bailout_and_deopt(vm_instance, block);
                        return -1;
                    }
                }
                vm_store_i32_typed_hot(counter_reg, updated);
                should_branch =
                    (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                        ? (updated < limit)
                        : (updated > limit);
                break;
            }

            if (IS_I64(counter_value) && IS_I64(limit_value)) {
                int64_t current = AS_I64(counter_value);
                int64_t limit = AS_I64(limit_value);
                int64_t magnitude64 = (int64_t)magnitude;
                int64_t updated = current;
                if (direction > 0) {
                    if (__builtin_add_overflow(current, magnitude64, &updated)) {
                        jit_bailout_and_deopt(vm_instance, block);
                        return -1;
                    }
                } else {
                    if (__builtin_sub_overflow(current, magnitude64, &updated)) {
                        jit_bailout_and_deopt(vm_instance, block);
                        return -1;
                    }
                }
                vm_store_i64_typed_hot(counter_reg, updated);
                should_branch =
                    (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                        ? (updated < limit)
                        : (updated > limit);
                break;
            }

            if (IS_U32(counter_value) && IS_U32(limit_value)) {
                uint32_t current = AS_U32(counter_value);
                uint32_t limit = AS_U32(limit_value);
                uint32_t magnitude32 = (uint32_t)magnitude;
                uint32_t updated =
                    (direction > 0) ? (current + magnitude32)
                                     : (current - magnitude32);
                vm_store_u32_typed_hot(counter_reg, updated);
                should_branch =
                    (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                        ? (updated < limit)
                        : (updated > limit);
                break;
            }

            if (IS_U64(counter_value) && IS_U64(limit_value)) {
                uint64_t current = AS_U64(counter_value);
                uint64_t limit = AS_U64(limit_value);
                uint64_t magnitude64 = (uint64_t)magnitude;
                uint64_t updated =
                    (direction > 0) ? (current + magnitude64)
                                     : (current - magnitude64);
                vm_store_u64_typed_hot(counter_reg, updated);
                should_branch =
                    (compare_kind == ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN)
                        ? (updated < limit)
                        : (updated > limit);
                break;
            }

            jit_bailout_and_deopt(vm_instance, block);
            return -1;
        }
        default:
            jit_bailout_and_deopt(vm_instance, block);
            return -1;
    }

    return should_branch ? 1 : 0;
}
#endif // defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64)

static bool
orus_jit_opcode_is_add(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
            return true;
        default:
            return false;
    }
}

static bool
orus_jit_opcode_is_sub(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
            return true;
        default:
            return false;
    }
}

static bool
orus_jit_opcode_is_mul(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
            return true;
        default:
            return false;
    }
}

static void
orus_jit_execute_block(struct VM* vm_instance, const OrusJitNativeBlock* block) {
    if (!vm_instance || !block || !block->program.instructions) {
        return;
    }
    vm_instance->jit_native_dispatch_count++;
    const OrusJitIRInstruction* instructions = block->program.instructions;
    const Chunk* chunk = (const Chunk*)block->program.source_chunk;
    if (!chunk || chunk->count <= 0) {
        jit_bailout_and_deopt(vm_instance, block);
        return;
    }
    size_t chunk_size = (size_t)chunk->count;
    size_t* bytecode_to_inst = (size_t*)malloc(chunk_size * sizeof(size_t));
    if (!bytecode_to_inst) {
        jit_bailout_and_deopt(vm_instance, block);
        return;
    }
    for (size_t idx = 0; idx < chunk_size; ++idx) {
        bytecode_to_inst[idx] = SIZE_MAX;
    }
    for (size_t idx = 0; idx < block->program.count; ++idx) {
        uint32_t bytecode_offset = instructions[idx].bytecode_offset;
        if (bytecode_offset < chunk_size &&
            bytecode_to_inst[bytecode_offset] == SIZE_MAX) {
            bytecode_to_inst[bytecode_offset] = idx;
        }
    }

    size_t ip = 0u;
    while (ip < block->program.count) {
        orus_jit_helper_safepoint(vm_instance);
        const OrusJitIRInstruction* inst = &instructions[ip];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
                jit_store_constant(vm_instance, chunk, inst);
                break;
            case ORUS_JIT_IR_OP_MOVE_I32:
            case ORUS_JIT_IR_OP_MOVE_I64:
            case ORUS_JIT_IR_OP_MOVE_U32:
            case ORUS_JIT_IR_OP_MOVE_U64:
            case ORUS_JIT_IR_OP_MOVE_F64:
            case ORUS_JIT_IR_OP_MOVE_BOOL:
            case ORUS_JIT_IR_OP_MOVE_STRING:
                jit_move_typed(vm_instance, inst);
                break;
            case ORUS_JIT_IR_OP_MOVE_VALUE:
                jit_move_value(vm_instance, inst);
                break;
            case ORUS_JIT_IR_OP_SAFEPOINT:
                orus_jit_helper_safepoint(vm_instance);
                break;
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_I32:
            case ORUS_JIT_IR_OP_MUL_I64:
            case ORUS_JIT_IR_OP_MUL_U32:
            case ORUS_JIT_IR_OP_MUL_U64:
            case ORUS_JIT_IR_OP_MUL_F64:
            case ORUS_JIT_IR_OP_DIV_I32:
            case ORUS_JIT_IR_OP_DIV_I64:
            case ORUS_JIT_IR_OP_DIV_U32:
            case ORUS_JIT_IR_OP_DIV_U64:
            case ORUS_JIT_IR_OP_DIV_F64:
            case ORUS_JIT_IR_OP_MOD_I32:
            case ORUS_JIT_IR_OP_MOD_I64:
            case ORUS_JIT_IR_OP_MOD_U32:
            case ORUS_JIT_IR_OP_MOD_U64:
            case ORUS_JIT_IR_OP_MOD_F64: {
                uint16_t dst = inst->operands.arithmetic.dst_reg;
                uint16_t lhs_reg = inst->operands.arithmetic.lhs_reg;
                uint16_t rhs_reg = inst->operands.arithmetic.rhs_reg;
                if ((inst->optimization_flags & ORUS_JIT_IR_FLAG_VECTOR_HEAD) &&
                    ip + 1u < block->program.count) {
                    const OrusJitIRInstruction* tail = &instructions[ip + 1u];
                    if ((tail->optimization_flags & ORUS_JIT_IR_FLAG_VECTOR_TAIL) &&
                        tail->opcode == inst->opcode &&
                        tail->value_kind == inst->value_kind) {
                        if (!orus_jit_native_vector_pair(vm_instance,
                                                         (OrusJitNativeBlock*)block,
                                                         inst,
                                                         tail)) {
                            free(bytecode_to_inst);
                            return;
                        }
                        ip += 2u;
                        continue;
                    }
                }
                switch (inst->value_kind) {
                    case ORUS_JIT_VALUE_I32: {
                        int32_t lhs = 0;
                        int32_t rhs = 0;
                        if (!jit_read_i32(vm_instance, lhs_reg, &lhs) ||
                            !jit_read_i32(vm_instance, rhs_reg, &rhs)) {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        int32_t result = lhs;
                        if (orus_jit_opcode_is_add(inst->opcode)) {
                            result = lhs + rhs;
                        } else if (orus_jit_opcode_is_sub(inst->opcode)) {
                            result = lhs - rhs;
                        } else if (orus_jit_opcode_is_mul(inst->opcode)) {
                            result = lhs * rhs;
                        } else {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        vm_store_i32_typed_hot(dst, result);
                        break;
                    }
                    case ORUS_JIT_VALUE_I64: {
                        int64_t lhs = 0;
                        int64_t rhs = 0;
                        if (!jit_read_i64(vm_instance, lhs_reg, &lhs) ||
                            !jit_read_i64(vm_instance, rhs_reg, &rhs)) {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        int64_t result = lhs;
                        if (orus_jit_opcode_is_add(inst->opcode)) {
                            result = lhs + rhs;
                        } else if (orus_jit_opcode_is_sub(inst->opcode)) {
                            result = lhs - rhs;
                        } else if (orus_jit_opcode_is_mul(inst->opcode)) {
                            result = lhs * rhs;
                        } else {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        vm_store_i64_typed_hot(dst, result);
                        break;
                    }
                    case ORUS_JIT_VALUE_U32: {
                        uint32_t lhs = 0;
                        uint32_t rhs = 0;
                        if (!jit_read_u32(vm_instance, lhs_reg, &lhs) ||
                            !jit_read_u32(vm_instance, rhs_reg, &rhs)) {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        uint32_t result = lhs;
                        if (orus_jit_opcode_is_add(inst->opcode)) {
                            result = lhs + rhs;
                        } else if (orus_jit_opcode_is_sub(inst->opcode)) {
                            result = lhs - rhs;
                        } else if (orus_jit_opcode_is_mul(inst->opcode)) {
                            result = lhs * rhs;
                        } else {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        vm_store_u32_typed_hot(dst, result);
                        break;
                    }
                    case ORUS_JIT_VALUE_U64: {
                        uint64_t lhs = 0;
                        uint64_t rhs = 0;
                        if (!jit_read_u64(vm_instance, lhs_reg, &lhs) ||
                            !jit_read_u64(vm_instance, rhs_reg, &rhs)) {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        uint64_t result = lhs;
                        if (orus_jit_opcode_is_add(inst->opcode)) {
                            result = lhs + rhs;
                        } else if (orus_jit_opcode_is_sub(inst->opcode)) {
                            result = lhs - rhs;
                        } else if (orus_jit_opcode_is_mul(inst->opcode)) {
                            result = lhs * rhs;
                        } else {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        vm_store_u64_typed_hot(dst, result);
                        break;
                    }
                    case ORUS_JIT_VALUE_F64: {
                        double lhs = 0.0;
                        double rhs = 0.0;
                        if (!jit_read_f64(vm_instance, lhs_reg, &lhs) ||
                            !jit_read_f64(vm_instance, rhs_reg, &rhs)) {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        double result = lhs;
                        if (orus_jit_opcode_is_add(inst->opcode)) {
                            result = lhs + rhs;
                        } else if (orus_jit_opcode_is_sub(inst->opcode)) {
                            result = lhs - rhs;
                        } else if (orus_jit_opcode_is_mul(inst->opcode)) {
                            result = lhs * rhs;
                        } else {
                            jit_bailout_and_deopt(vm_instance, block);
                            return;
                        }
                        vm_store_f64_typed_hot(dst, result);
                        break;
                    }
                    default:
                        jit_bailout_and_deopt(vm_instance, block);
                        return;
                }
                break;
            }
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL: {
                if (!orus_jit_native_compare_op(vm_instance,
                                                (OrusJitNativeBlock*)block,
                                                inst->opcode,
                                                inst->operands.arithmetic.dst_reg,
                                                inst->operands.arithmetic.lhs_reg,
                                                inst->operands.arithmetic.rhs_reg)) {
                    return;
                }
                break;
            }
            case ORUS_JIT_IR_OP_CONCAT_STRING:
                if (!orus_jit_native_concat_string(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.arithmetic.dst_reg,
                                                   inst->operands.arithmetic.lhs_reg,
                                                   inst->operands.arithmetic.rhs_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_TO_STRING:
                if (!orus_jit_native_to_string(vm_instance,
                                               (OrusJitNativeBlock*)block,
                                               inst->operands.unary.dst_reg,
                                               inst->operands.unary.src_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_TYPE_OF:
                if (!orus_jit_native_type_of(vm_instance,
                                             (OrusJitNativeBlock*)block,
                                             inst->operands.type_of.dst_reg,
                                             inst->operands.type_of.value_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_IS_TYPE:
                if (!orus_jit_native_is_type(vm_instance,
                                             (OrusJitNativeBlock*)block,
                                             inst->operands.is_type.dst_reg,
                                             inst->operands.is_type.value_reg,
                                             inst->operands.is_type.type_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_TIME_STAMP:
                orus_jit_native_time_stamp(vm_instance, (OrusJitNativeBlock*)block,
                                           inst->operands.time_stamp.dst_reg);
                break;
            case ORUS_JIT_IR_OP_MAKE_ARRAY:
                if (!orus_jit_native_make_array(
                        vm_instance, (OrusJitNativeBlock*)block, inst)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_ARRAY_PUSH:
                if (!orus_jit_native_array_push(vm_instance,
                                                (OrusJitNativeBlock*)block,
                                                inst->operands.array_push.array_reg,
                                                inst->operands.array_push.value_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_ARRAY_POP:
                if (!orus_jit_native_array_pop(vm_instance,
                                               (OrusJitNativeBlock*)block,
                                               inst->operands.array_pop.dst_reg,
                                               inst->operands.array_pop.array_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_ENUM_NEW:
                if (!orus_jit_native_enum_new(
                        vm_instance, (OrusJitNativeBlock*)block, inst)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_PRINT:
                orus_jit_native_print(vm_instance, (OrusJitNativeBlock*)block,
                                      inst->operands.print.first_reg,
                                      inst->operands.print.arg_count,
                                      inst->operands.print.newline);
                break;
            case ORUS_JIT_IR_OP_ASSERT_EQ:
                if (!orus_jit_native_assert_eq(vm_instance,
                                               (OrusJitNativeBlock*)block,
                                               inst->operands.assert_eq.dst_reg,
                                               inst->operands.assert_eq.label_reg,
                                               inst->operands.assert_eq.actual_reg,
                                               inst->operands.assert_eq.expected_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_CALL_NATIVE:
            case ORUS_JIT_IR_OP_CALL_FOREIGN: {
                if (inst->operands.call_native.spill_count > 0u) {
                    orus_jit_native_flush_typed_range(
                        vm_instance, inst->operands.call_native.spill_base,
                        inst->operands.call_native.spill_count);
                }
                bool ok = (inst->opcode == ORUS_JIT_IR_OP_CALL_FOREIGN)
                              ? orus_jit_native_call_foreign(
                                    vm_instance, (OrusJitNativeBlock*)block,
                                    inst->operands.call_native.dst_reg,
                                    inst->operands.call_native.first_arg_reg,
                                    inst->operands.call_native.arg_count,
                                    inst->operands.call_native.native_index)
                              : orus_jit_native_call_native(
                                    vm_instance, (OrusJitNativeBlock*)block,
                                    inst->operands.call_native.dst_reg,
                                    inst->operands.call_native.first_arg_reg,
                                    inst->operands.call_native.arg_count,
                                    inst->operands.call_native.native_index);
                if (!ok) {
                    return;
                }
                break;
            }
            case ORUS_JIT_IR_OP_RANGE:
                if (!orus_jit_native_range(vm_instance,
                                           (OrusJitNativeBlock*)block,
                                           inst->operands.range.dst_reg,
                                           inst->operands.range.arg_count,
                                           inst->operands.range.arg_regs)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_GET_ITER:
                if (!orus_jit_native_get_iter(vm_instance,
                                              (OrusJitNativeBlock*)block,
                                              inst->operands.get_iter.dst_reg,
                                              inst->operands.get_iter.iterable_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_ITER_NEXT:
                if (!orus_jit_native_iter_next(vm_instance,
                                               (OrusJitNativeBlock*)block,
                                               inst->operands.iter_next.value_reg,
                                               inst->operands.iter_next.iterator_reg,
                                               inst->operands.iter_next.has_value_reg)) {
                    return;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_I64:
                orus_jit_native_convert_i32_to_i64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U32_TO_U64:
                orus_jit_native_convert_u32_to_u64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U32_TO_I32:
                orus_jit_native_convert_u32_to_i32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I32_TO_F64:
                orus_jit_native_convert_i32_to_f64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I64_TO_F64:
                orus_jit_native_convert_i64_to_f64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_F64_TO_I32:
                orus_jit_native_convert_f64_to_i32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_F64_TO_I64:
                orus_jit_native_convert_f64_to_i64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_F64_TO_U32:
                orus_jit_native_convert_f64_to_u32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U32_TO_F64:
                orus_jit_native_convert_u32_to_f64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I32_TO_U32:
                orus_jit_native_convert_i32_to_u32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I64_TO_U32:
                orus_jit_native_convert_i64_to_u32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I32_TO_U64:
                orus_jit_native_convert_i32_to_u64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_I64_TO_U64:
                orus_jit_native_convert_i64_to_u64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U64_TO_I32:
                orus_jit_native_convert_u64_to_i32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U64_TO_I64:
                orus_jit_native_convert_u64_to_i64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U64_TO_U32:
                orus_jit_native_convert_u64_to_u32(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_F64_TO_U64:
                orus_jit_native_convert_f64_to_u64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_U64_TO_F64:
                orus_jit_native_convert_u64_to_f64(vm_instance,
                                                   (OrusJitNativeBlock*)block,
                                                   inst->operands.unary.dst_reg,
                                                   inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_JUMP_SHORT: {
                uint32_t fallthrough = inst->bytecode_offset +
                                      inst->operands.jump_short.bytecode_length;
                uint32_t target = fallthrough + inst->operands.jump_short.offset;
                size_t target_index = SIZE_MAX;
                if (target < chunk_size) {
                    target_index = bytecode_to_inst[target];
                }
                if (target_index == SIZE_MAX) {
                    target_index =
                        orus_jit_program_find_index(&block->program, target);
                }
                if (target_index == SIZE_MAX) {
                    jit_bailout_and_deopt(vm_instance, block);
                    goto cleanup;
                }
                ip = target_index;
                continue;
            }
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: {
                uint32_t fallthrough = inst->bytecode_offset + 2u;
                uint16_t back = inst->operands.jump_back_short.back_offset;
                if (fallthrough < back) {
                    jit_bailout_and_deopt(vm_instance, block);
                    goto cleanup;
                }
                uint32_t target = fallthrough - back;
                size_t target_index = SIZE_MAX;
                if (target < chunk_size) {
                    target_index = bytecode_to_inst[target];
                }
                if (target_index == SIZE_MAX) {
                    target_index =
                        orus_jit_program_find_index(&block->program, target);
                }
                if (target_index == SIZE_MAX) {
                    jit_bailout_and_deopt(vm_instance, block);
                    goto cleanup;
                }
                ip = target_index;
                continue;
            }
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: {
                bool predicate = false;
                if (!jit_read_bool(vm_instance,
                                   inst->operands.jump_if_not_short.predicate_reg,
                                   &predicate)) {
                    jit_bailout_and_deopt(vm_instance, block);
                    goto cleanup;
                }
                if (!predicate) {
                    uint32_t fallthrough =
                        inst->bytecode_offset +
                        inst->operands.jump_if_not_short.bytecode_length;
                    uint32_t target = fallthrough +
                                       inst->operands.jump_if_not_short.offset;
                    size_t target_index = SIZE_MAX;
                    if (target < chunk_size) {
                        target_index = bytecode_to_inst[target];
                    }
                    if (target_index == SIZE_MAX) {
                        target_index = orus_jit_program_find_index(
                            &block->program, target);
                    }
                    if (target_index == SIZE_MAX) {
                        jit_bailout_and_deopt(vm_instance, block);
                        goto cleanup;
                    }
                    ip = target_index;
                    continue;
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK:
            case ORUS_JIT_IR_OP_RETURN:
                goto cleanup;
            default:
                jit_bailout_and_deopt(vm_instance, block);
                goto cleanup;
        }
        ip++;
    }

cleanup:
    free(bytecode_to_inst);
    return;
}

bool
orus_jit_backend_is_available(void) {
    OrusJitBackendAvailability availability =
        orus_jit_backend_detect_availability();
    if (!availability.supported) {
        LOG_INFO("JIT backend unavailable: %s", availability.message);
    }
    return availability.supported;
}

JITBackendStatus
orus_jit_backend_availability(const struct OrusJitBackend* backend,
                              OrusJitBackendTarget* out_target,
                              const char** out_message) {
    if (!backend) {
        if (out_target) {
            *out_target = ORUS_JIT_BACKEND_TARGET_NATIVE;
        }
        if (out_message) {
            *out_message = "Orus JIT backend not initialized.";
        }
        return JIT_BACKEND_UNSUPPORTED;
    }

    if (out_target) {
        *out_target = backend->target;
    }
    if (out_message) {
        *out_message = backend->availability_message;
    }
    return backend->availability_status;
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
static bool
orus_jit_should_force_helper_stub(void) {
    const char* value = getenv("ORUS_JIT_FORCE_HELPER_STUB");
    return value && value[0] != '\0';
}
#if ORUS_JIT_HAS_DYNASM_X86
static bool
orus_jit_should_force_dynasm(void) {
    const char* value = getenv("ORUS_JIT_FORCE_DYNASM");
    return value && value[0] != '\0';
}
#endif

static bool
orus_jit_linear_emitter_enabled(void) {
    static int cached = -1;
    if (g_orus_jit_linear_emitter_override != -1) {
        return g_orus_jit_linear_emitter_override == 1;
    }
    if (cached == -1) {
        const char* enable = getenv("ORUS_JIT_ENABLE_LINEAR_EMITTER");
        const char* force = getenv("ORUS_JIT_FORCE_LINEAR_EMITTER");
        /*
         * Support both the new opt-in switch and a legacy-style "force" toggle
         * so existing automation that mirrored other JIT env variables keeps
         * working while the linear emitter stays disabled by default.
         */
        cached = ((enable && enable[0] != '\0') ||
                  (force && force[0] != '\0'))
                     ? 1
                     : 0;
    }
    return cached == 1;
}
#endif

struct OrusJitBackend*
orus_jit_backend_create(void) {
    struct OrusJitBackend* backend =
        (struct OrusJitBackend*)calloc(1, sizeof(struct OrusJitBackend));
    if (!backend) {
        return NULL;
    }

    backend->page_size = orus_jit_detect_page_size();
    OrusJitBackendAvailability availability =
        orus_jit_backend_detect_availability();
    backend->available = availability.supported &&
                         availability.status == JIT_BACKEND_OK;
    backend->target = availability.target;
    backend->availability_status = availability.status;
    backend->availability_message = availability.message;
    if (g_dynasm_helper_stub_page_size == 0u) {
        g_dynasm_helper_stub_page_size = backend->page_size;
    }
    g_dynasm_helper_stub_users++;

    if (backend->available && !orus_jit_backend_probe_executable(backend)) {
        backend->available = false;
        backend->availability_status = JIT_BACKEND_UNSUPPORTED;
        backend->availability_message =
            "Writable executable memory not available in this environment.";
    }

    return backend;
}

void
orus_jit_backend_destroy(struct OrusJitBackend* backend) {
    if (!backend) {
        return;
    }
    if (g_dynasm_helper_stub_users > 0u) {
        g_dynasm_helper_stub_users--;
        if (g_dynasm_helper_stub_users == 0u) {
            orus_jit_helper_stubs_release_all();
            g_dynasm_helper_stub_page_size = 0u;
            free(g_orus_jit_helper_registry);
            g_orus_jit_helper_registry = NULL;
            g_orus_jit_helper_count = 0u;
            g_orus_jit_helper_capacity = 0u;
        }
    }
    free(backend);
}

#if defined(__x86_64__) || defined(_M_X64)
typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
} OrusJitCodeBuffer;

typedef struct {
    size_t* data;
    size_t count;
    size_t capacity;
} OrusJitOffsetList;

typedef struct {
    size_t code_offset;
    uint32_t target_bytecode;
} OrusJitBranchPatch;

typedef struct {
    OrusJitBranchPatch* data;
    size_t count;
    size_t capacity;
} OrusJitBranchPatchList;

static void
orus_jit_code_buffer_init(OrusJitCodeBuffer* buffer) {
    if (!buffer) {
        return;
    }
    buffer->data = NULL;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

static void
orus_jit_code_buffer_release(OrusJitCodeBuffer* buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->size = 0u;
    buffer->capacity = 0u;
}

static bool
orus_jit_code_buffer_reserve(OrusJitCodeBuffer* buffer, size_t additional) {
    if (!buffer) {
        return false;
    }
    if (additional == 0u) {
        return true;
    }
    if (buffer->size > SIZE_MAX - additional) {
        return false;
    }
    size_t required = buffer->size + additional;
    if (required <= buffer->capacity) {
        return true;
    }
    size_t new_capacity = buffer->capacity ? buffer->capacity : 64u;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }
    uint8_t* data = (uint8_t*)realloc(buffer->data, new_capacity * sizeof(uint8_t));
    if (!data) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = new_capacity;
    return true;
}

static bool
orus_jit_code_buffer_emit_bytes(OrusJitCodeBuffer* buffer,
                                const uint8_t* bytes,
                                size_t count) {
    if (!bytes || count == 0u) {
        return true;
    }
    if (!orus_jit_code_buffer_reserve(buffer, count)) {
        return false;
    }
    memcpy(buffer->data + buffer->size, bytes, count);
    buffer->size += count;
    return true;
}

static bool
orus_jit_code_buffer_emit_u8(OrusJitCodeBuffer* buffer, uint8_t value) {
    return orus_jit_code_buffer_emit_bytes(buffer, &value, 1u);
}

static bool
orus_jit_code_buffer_emit_u32(OrusJitCodeBuffer* buffer, uint32_t value) {
    return orus_jit_code_buffer_emit_bytes(buffer, (const uint8_t*)&value,
                                           sizeof(uint32_t));
}

static bool
orus_jit_code_buffer_emit_u64(OrusJitCodeBuffer* buffer, uint64_t value) {
    return orus_jit_code_buffer_emit_bytes(buffer, (const uint8_t*)&value,
                                           sizeof(uint64_t));
}

static void
orus_jit_offset_list_init(OrusJitOffsetList* list) {
    if (!list) {
        return;
    }
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void
orus_jit_offset_list_release(OrusJitOffsetList* list) {
    if (!list) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
orus_jit_offset_list_append(OrusJitOffsetList* list, size_t value) {
    if (!list) {
        return false;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 4u;
        size_t* values =
            (size_t*)realloc(list->data, new_capacity * sizeof(size_t));
        if (!values) {
            return false;
        }
        list->data = values;
        list->capacity = new_capacity;
    }
    list->data[list->count++] = value;
    return true;
}

static void
orus_jit_branch_patch_list_init(OrusJitBranchPatchList* list) {
    if (!list) {
        return;
    }
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void
orus_jit_branch_patch_list_release(OrusJitBranchPatchList* list) {
    if (!list) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
orus_jit_branch_patch_list_append(OrusJitBranchPatchList* list,
                                  size_t code_offset,
                                  uint32_t target_bytecode) {
    if (!list) {
        return false;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 4u;
        OrusJitBranchPatch* patches = (OrusJitBranchPatch*)realloc(
            list->data, new_capacity * sizeof(OrusJitBranchPatch));
        if (!patches) {
            return false;
        }
        list->data = patches;
        list->capacity = new_capacity;
    }
    list->data[list->count].code_offset = code_offset;
    list->data[list->count].target_bytecode = target_bytecode;
    list->count++;
    return true;
}

static bool orus_jit_emit_conditional_jump(OrusJitCodeBuffer* buffer,
                                           uint8_t opcode,
                                           OrusJitOffsetList* patches);
static bool orus_jit_emit_type_guard(OrusJitCodeBuffer* buffer,
                                     uint8_t index_reg_code,
                                     uint8_t expected_type,
                                     OrusJitOffsetList* bail_patches);
static bool orus_jit_emit_load_typed_pointer(OrusJitCodeBuffer* buffer,
                                             uint32_t typed_ptr_offset,
                                             OrusJitOffsetList* bail_patches);

static bool
orus_jit_emit_linear_prologue(OrusJitCodeBuffer* buffer,
                              OrusJitNativeBlock* block,
                              size_t* out_loop_entry,
                              size_t* out_bail_disp,
                              OrusJitOffsetList* bail_patches) {
    static const uint8_t prologue_prefix[] = {
        0x53,       // push rbx
        0x41, 0x54, // push r12
        0x41, 0x55, // push r13
        0x41, 0x56, // push r14
        0x41, 0x57, // push r15 (stack alignment)
    };
    static const uint8_t mov_r12_rdi[] = {0x49, 0x89, 0xFC};
    static const uint8_t test_r14[] = {0x4D, 0x85, 0xF6};

    if (!buffer || !block || !out_loop_entry || !out_bail_disp ||
        !bail_patches) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_bytes(buffer, prologue_prefix,
                                         sizeof(prologue_prefix))) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_u8(buffer, 0x48) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xBB) ||
        !orus_jit_code_buffer_emit_u64(buffer, (uint64_t)(uintptr_t)block)) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_bytes(buffer, mov_r12_rdi,
                                         sizeof(mov_r12_rdi))) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_u8(buffer, 0x4C) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x8D) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xAF) ||
        !orus_jit_code_buffer_emit_u32(buffer,
                                       (uint32_t)ORUS_JIT_OFFSET_VM_TYPED_REGS)) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_u8(buffer, 0x4D) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x8B) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xB5) ||
        !orus_jit_code_buffer_emit_u32(buffer,
                                       (uint32_t)ORUS_JIT_OFFSET_TYPED_I32_PTR)) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_bytes(buffer, test_r14, sizeof(test_r14))) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_u8(buffer, 0x0F) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x84)) {
        return false;
    }
    *out_bail_disp = buffer->size;
    if (!orus_jit_code_buffer_emit_u32(buffer, 0u)) {
        return false;
    }

    if (!orus_jit_code_buffer_emit_u8(buffer, 0x4D) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x8B) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xBD) ||
        !orus_jit_code_buffer_emit_u32(buffer,
                                       (uint32_t)ORUS_JIT_OFFSET_TYPED_REG_TYPES)) {
        return false;
    }

    static const uint8_t test_r15[] = {0x4D, 0x85, 0xFF};
    if (!orus_jit_code_buffer_emit_bytes(buffer, test_r15, sizeof(test_r15))) {
        return false;
    }

    if (!orus_jit_emit_conditional_jump(buffer, 0x84u, bail_patches)) {
        return false;
    }

    *out_loop_entry = buffer->size;
    return true;
}

static bool
orus_jit_emit_safepoint_call(OrusJitCodeBuffer* buffer,
                             OrusJitOffsetList* bail_patches) {
    if (!buffer || !bail_patches) {
        return false;
    }
    if (!orus_jit_code_buffer_emit_u8(buffer, 0x48) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xB8) ||
        !orus_jit_code_buffer_emit_u64(buffer,
                                       orus_jit_function_ptr_bits(&orus_jit_native_linear_safepoint)) ||
        !orus_jit_code_buffer_emit_bytes(buffer, (const uint8_t[]){0xFF, 0xD0}, 2u)) {
        return false;
    }
    (void)bail_patches;
    return true;
}

static bool
orus_jit_emit_conditional_jump(OrusJitCodeBuffer* buffer,
                               uint8_t opcode,
                               OrusJitOffsetList* patches) {
    if (!buffer || !patches) {
        return false;
    }
    if (!orus_jit_code_buffer_emit_u8(buffer, 0x0F) ||
        !orus_jit_code_buffer_emit_u8(buffer, opcode)) {
        return false;
    }
    size_t offset = buffer->size;
    if (!orus_jit_code_buffer_emit_u32(buffer, 0u)) {
        return false;
    }
    return orus_jit_offset_list_append(patches, offset);
}

static bool
orus_jit_emit_type_guard(OrusJitCodeBuffer* buffer,
                         uint8_t index_reg_code,
                         uint8_t expected_type,
                         OrusJitOffsetList* bail_patches) {
    if (!buffer || !bail_patches) {
        return false;
    }
    if (!orus_jit_code_buffer_emit_u8(buffer, 0x41) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x80) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x3C)) {
        return false;
    }
    uint8_t sib = (uint8_t)((index_reg_code << 3) | 0x07u);
    if (!orus_jit_code_buffer_emit_u8(buffer, sib) ||
        !orus_jit_code_buffer_emit_u8(buffer, expected_type)) {
        return false;
    }
    return orus_jit_emit_conditional_jump(buffer, 0x85u, bail_patches);
}

static bool
orus_jit_emit_load_typed_pointer(OrusJitCodeBuffer* buffer,
                                 uint32_t typed_ptr_offset,
                                 OrusJitOffsetList* bail_patches) {
    if (!buffer || !bail_patches) {
        return false;
    }
    if (!orus_jit_code_buffer_emit_u8(buffer, 0x4D) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x8B) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x95) ||
        !orus_jit_code_buffer_emit_u32(buffer, typed_ptr_offset) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x4D) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0x85) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xD2)) {
        return false;
    }
    return orus_jit_emit_conditional_jump(buffer, 0x84u, bail_patches);
}

static bool
orus_jit_emit_return_placeholder(OrusJitCodeBuffer* buffer,
                                 OrusJitOffsetList* returns) {
    if (!buffer || !returns) {
        return false;
    }
    if (!orus_jit_code_buffer_emit_u8(buffer, 0xE9)) {
        return false;
    }
    size_t offset = buffer->size;
    if (!orus_jit_code_buffer_emit_u32(buffer, 0u)) {
        return false;
    }
    return orus_jit_offset_list_append(returns, offset);
}

static bool
orus_jit_emit_linear_epilogue(OrusJitCodeBuffer* buffer) {
    static const uint8_t epilogue[] = {
        0x41, 0x5F, // pop r15
        0x41, 0x5E, // pop r14
        0x41, 0x5D, // pop r13
        0x41, 0x5C, // pop r12
        0x5B,       // pop rbx
        0xC3,       // ret
    };
    return orus_jit_code_buffer_emit_bytes(buffer, epilogue, sizeof(epilogue));
}

static JITBackendStatus
orus_jit_backend_emit_linear_x86(struct OrusJitBackend* backend,
                                 OrusJitNativeBlock* block,
                                 JITEntry* entry) {
    if (!backend || !block || !entry) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
    if (!block->program.instructions || block->program.count == 0u) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    for (size_t i = 0; i < block->program.count; ++i) {
        const OrusJitIRInstruction* inst = &block->program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_MOVE_I32:
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_MUL_I32:
            case ORUS_JIT_IR_OP_DIV_I32:
            case ORUS_JIT_IR_OP_MOD_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_MOVE_I64:
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64:
            case ORUS_JIT_IR_OP_DIV_I64:
            case ORUS_JIT_IR_OP_MOD_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_MOVE_U32:
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_MUL_U32:
            case ORUS_JIT_IR_OP_DIV_U32:
            case ORUS_JIT_IR_OP_MOD_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_MOVE_U64:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64:
            case ORUS_JIT_IR_OP_DIV_U64:
            case ORUS_JIT_IR_OP_MOD_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            case ORUS_JIT_IR_OP_MOVE_F64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64:
            case ORUS_JIT_IR_OP_DIV_F64:
            case ORUS_JIT_IR_OP_MOD_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_BOOL_CONST:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
                if (inst->value_kind >= ORUS_JIT_VALUE_KIND_COUNT) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL:
            case ORUS_JIT_IR_OP_IS_TYPE:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_INC_CMP_JUMP:
            case ORUS_JIT_IR_OP_DEC_CMP_JUMP: {
                switch (inst->value_kind) {
                    case ORUS_JIT_VALUE_I32:
                    case ORUS_JIT_VALUE_I64:
                    case ORUS_JIT_VALUE_U32:
                    case ORUS_JIT_VALUE_U64:
                    case ORUS_JIT_VALUE_F64:
                    case ORUS_JIT_VALUE_BOXED:
                        break;
                    default:
                        return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_BOOL:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
            case ORUS_JIT_IR_OP_MOVE_STRING:
            case ORUS_JIT_IR_OP_CONCAT_STRING:
            case ORUS_JIT_IR_OP_TO_STRING:
            case ORUS_JIT_IR_OP_TYPE_OF:
                if (inst->value_kind != ORUS_JIT_VALUE_STRING) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_MOVE_VALUE:
                if (inst->value_kind != ORUS_JIT_VALUE_BOXED) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_TIME_STAMP:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_ASSERT_EQ:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_MAKE_ARRAY:
            case ORUS_JIT_IR_OP_ARRAY_POP:
            case ORUS_JIT_IR_OP_ENUM_NEW:
                if (inst->value_kind != ORUS_JIT_VALUE_BOXED) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_SAFEPOINT:
            case ORUS_JIT_IR_OP_LOOP_BACK:
            case ORUS_JIT_IR_OP_RETURN:
            case ORUS_JIT_IR_OP_JUMP_SHORT:
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
            case ORUS_JIT_IR_OP_GET_ITER:
            case ORUS_JIT_IR_OP_ITER_NEXT:
            case ORUS_JIT_IR_OP_RANGE:
            case ORUS_JIT_IR_OP_ARRAY_PUSH:
        case ORUS_JIT_IR_OP_PRINT:
        case ORUS_JIT_IR_OP_CALL_NATIVE:
        case ORUS_JIT_IR_OP_CALL_FOREIGN:
            break;
        default:
            return JIT_BACKEND_ASSEMBLY_ERROR;
        }
    }

    OrusJitCodeBuffer code;
    orus_jit_code_buffer_init(&code);
    OrusJitOffsetList return_patches;
    orus_jit_offset_list_init(&return_patches);
    OrusJitOffsetList bail_patches;
    orus_jit_offset_list_init(&bail_patches);
    OrusJitBranchPatchList branch_patches;
    orus_jit_branch_patch_list_init(&branch_patches);
    size_t* inst_offsets = NULL;
    if (block->program.count) {
        inst_offsets = (size_t*)calloc(block->program.count, sizeof(size_t));
        if (!inst_offsets) {
            orus_jit_code_buffer_release(&code);
            orus_jit_offset_list_release(&return_patches);
            orus_jit_offset_list_release(&bail_patches);
            return JIT_BACKEND_OUT_OF_MEMORY;
        }
        for (size_t i = 0; i < block->program.count; ++i) {
            inst_offsets[i] = SIZE_MAX;
        }
    }

#define RETURN_WITH(status)                                                     \
    do {                                                                        \
        orus_jit_code_buffer_release(&code);                                    \
        orus_jit_offset_list_release(&return_patches);                          \
        orus_jit_offset_list_release(&bail_patches);                            \
        orus_jit_branch_patch_list_release(&branch_patches);                    \
        free(inst_offsets);                                                     \
        return (status);                                                        \
    } while (0)

    size_t loop_entry_offset = 0u;
    size_t bail_disp_offset = 0u;
    if (!orus_jit_emit_linear_prologue(&code, block, &loop_entry_offset,
                                       &bail_disp_offset, &bail_patches)) {
        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
    }

    static const uint8_t MOV_LOAD_EAX[] = {0x41, 0x8B, 0x04, 0x8E};
    static const uint8_t MOV_LOAD_EDX[] = {0x41, 0x8B, 0x14, 0x96};
    static const uint8_t MOV_STORE_EAX[] = {0x41, 0x89, 0x04, 0x8E};
    static const uint8_t MOV_LOAD_RAX_I64[] = {0x49, 0x8B, 0x04, 0xCA};
    static const uint8_t MOV_LOAD_RDX_I64[] = {0x49, 0x8B, 0x14, 0xD2};
    static const uint8_t MOV_STORE_RAX_I64[] = {0x49, 0x89, 0x04, 0xCA};
    static const uint8_t MOV_LOAD_EAX_U32[] = {0x41, 0x8B, 0x04, 0x8A};
    static const uint8_t MOV_LOAD_EDX_U32[] = {0x41, 0x8B, 0x14, 0x92};
    static const uint8_t MOV_STORE_EAX_U32[] = {0x41, 0x89, 0x04, 0x8A};
    static const uint8_t MOVSD_LOAD_XMM0[] = {0xF2, 0x41, 0x0F, 0x10, 0x04, 0xCA};
    static const uint8_t MOVSD_LOAD_XMM1[] = {0xF2, 0x41, 0x0F, 0x10, 0x0C, 0xD2};
    static const uint8_t MOVSD_STORE_XMM0[] = {0xF2, 0x41, 0x0F, 0x11, 0x04, 0xCA};
    static const uint8_t MOVZX_EAX_BOOL[] = {0x41, 0x0F, 0xB6, 0x04, 0x0A};
    static const uint8_t MOV_STORE_AL_BOOL[] = {0x41, 0x88, 0x04, 0x0A};
    static const uint8_t ADDSD_XMM0_XMM1[] = {0xF2, 0x0F, 0x58, 0xC1};
    static const uint8_t SUBSD_XMM0_XMM1[] = {0xF2, 0x0F, 0x5C, 0xC1};
    static const uint8_t MULSD_XMM0_XMM1[] = {0xF2, 0x0F, 0x59, 0xC1};
    static const uint8_t ADD_EAX_IMM1[] = {0x83, 0xC0, 0x01};
    static const uint8_t SUB_EAX_IMM1[] = {0x83, 0xE8, 0x01};
    static const uint8_t ADD_RAX_IMM1[] = {0x48, 0x83, 0xC0, 0x01};
    static const uint8_t SUB_RAX_IMM1[] = {0x48, 0x83, 0xE8, 0x01};

    for (size_t i = 0; i < block->program.count; ++i) {
        const OrusJitIRInstruction* inst = &block->program.instructions[i];
        if (inst_offsets) {
            inst_offsets[i] = code.size;
        }
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST: {
                int32_t value = (int32_t)(uint32_t)inst->operands.load_const.immediate_bits;
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(&code, (uint32_t)value) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX,
                                                     sizeof(MOV_STORE_EAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, inst->operands.load_const.immediate_bits) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_load_string_const)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.constant_index) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->value_kind) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_load_value_const)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_I32: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_I32,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EAX,
                                                     sizeof(MOV_LOAD_EAX)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX,
                                                     sizeof(MOV_STORE_EAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_BOOL: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_BOOL,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_BOOL_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVZX_EAX_BOOL,
                                                     sizeof(MOVZX_EAX_BOOL)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_AL_BOOL,
                                                     sizeof(MOV_STORE_AL_BOOL))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_STRING: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_move_string)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_VALUE: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_move_value)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_I64_CONST: {
                uint64_t bits = inst->operands.load_const.immediate_bits;
                if (!orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code, bits) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_I64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_I64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_I64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_I64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RAX_I64,
                                                     sizeof(MOV_LOAD_RAX_I64)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_MUL_I32: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_I32,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EAX,
                                                     sizeof(MOV_LOAD_EAX)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u,
                                              (uint8_t)REG_TYPE_I32,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EDX,
                                                     sizeof(MOV_LOAD_EDX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint8_t arithmetic_bytes[3];
                size_t arithmetic_size = 0u;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_ADD_I32:
                        arithmetic_bytes[0] = 0x01;
                        arithmetic_bytes[1] = 0xD0;
                        arithmetic_size = 2u;
                        break;
                    case ORUS_JIT_IR_OP_SUB_I32:
                        arithmetic_bytes[0] = 0x29;
                        arithmetic_bytes[1] = 0xD0;
                        arithmetic_size = 2u;
                        break;
                    case ORUS_JIT_IR_OP_MUL_I32:
                        arithmetic_bytes[0] = 0x0F;
                        arithmetic_bytes[1] = 0xAF;
                        arithmetic_bytes[2] = 0xC2;
                        arithmetic_size = 3u;
                        break;
                    default:
                        break;
                }

                if (arithmetic_size == 0u ||
                    !orus_jit_code_buffer_emit_bytes(&code, arithmetic_bytes,
                                                     arithmetic_size) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX,
                                                     sizeof(MOV_STORE_EAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_I64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_I64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RAX_I64,
                                                     sizeof(MOV_LOAD_RAX_I64)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u,
                                              (uint8_t)REG_TYPE_I64,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RDX_I64,
                                                     sizeof(MOV_LOAD_RDX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint8_t arithmetic_bytes[4];
                size_t arithmetic_size = 0u;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_ADD_I64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x01;
                        arithmetic_bytes[2] = 0xD0;
                        arithmetic_size = 3u;
                        break;
                    case ORUS_JIT_IR_OP_SUB_I64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x29;
                        arithmetic_bytes[2] = 0xD0;
                        arithmetic_size = 3u;
                        break;
                    case ORUS_JIT_IR_OP_MUL_I64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x0F;
                        arithmetic_bytes[2] = 0xAF;
                        arithmetic_bytes[3] = 0xC2;
                        arithmetic_size = 4u;
                        break;
                    default:
                        break;
                }

                if (arithmetic_size == 0u ||
                    !orus_jit_code_buffer_emit_bytes(&code, arithmetic_bytes,
                                                     arithmetic_size) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_U32_CONST: {
                uint32_t value =
                    (uint32_t)inst->operands.load_const.immediate_bits;
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(&code, value) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U32_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX_U32,
                                                     sizeof(MOV_STORE_EAX_U32))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_U32: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_U32,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U32_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EAX_U32,
                                                     sizeof(MOV_LOAD_EAX_U32)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX_U32,
                                                     sizeof(MOV_STORE_EAX_U32))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_MUL_U32: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_U32,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U32_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EAX_U32,
                                                     sizeof(MOV_LOAD_EAX_U32)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u,
                                              (uint8_t)REG_TYPE_U32,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_EDX_U32,
                                                     sizeof(MOV_LOAD_EDX_U32))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint8_t arithmetic_bytes[3];
                size_t arithmetic_size = 0u;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_ADD_U32:
                        arithmetic_bytes[0] = 0x01;
                        arithmetic_bytes[1] = 0xD0;
                        arithmetic_size = 2u;
                        break;
                    case ORUS_JIT_IR_OP_SUB_U32:
                        arithmetic_bytes[0] = 0x29;
                        arithmetic_bytes[1] = 0xD0;
                        arithmetic_size = 2u;
                        break;
                    case ORUS_JIT_IR_OP_MUL_U32:
                        arithmetic_bytes[0] = 0x0F;
                        arithmetic_bytes[1] = 0xAF;
                        arithmetic_bytes[2] = 0xC2;
                        arithmetic_size = 3u;
                        break;
                    default:
                        break;
                }

                if (arithmetic_size == 0u ||
                    !orus_jit_code_buffer_emit_bytes(&code, arithmetic_bytes,
                                                     arithmetic_size) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_EAX_U32,
                                                     sizeof(MOV_STORE_EAX_U32))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_U64_CONST: {
                uint64_t bits = inst->operands.load_const.immediate_bits;
                if (!orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code, bits) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_U64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_U64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RAX_I64,
                                                     sizeof(MOV_LOAD_RAX_I64)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_U64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_U64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RAX_I64,
                                                     sizeof(MOV_LOAD_RAX_I64)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u,
                                              (uint8_t)REG_TYPE_U64,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_LOAD_RDX_I64,
                                                     sizeof(MOV_LOAD_RDX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint8_t arithmetic_bytes[4];
                size_t arithmetic_size = 0u;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_ADD_U64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x01;
                        arithmetic_bytes[2] = 0xD0;
                        arithmetic_size = 3u;
                        break;
                    case ORUS_JIT_IR_OP_SUB_U64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x29;
                        arithmetic_bytes[2] = 0xD0;
                        arithmetic_size = 3u;
                        break;
                    case ORUS_JIT_IR_OP_MUL_U64:
                        arithmetic_bytes[0] = 0x48;
                        arithmetic_bytes[1] = 0x0F;
                        arithmetic_bytes[2] = 0xAF;
                        arithmetic_bytes[3] = 0xC2;
                        arithmetic_size = 4u;
                        break;
                    default:
                        break;
                }

                if (arithmetic_size == 0u ||
                    !orus_jit_code_buffer_emit_bytes(&code, arithmetic_bytes,
                                                     arithmetic_size) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_F64_CONST: {
                uint64_t bits = inst->operands.load_const.immediate_bits;
                if (!orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code, bits) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_F64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_RAX_I64,
                                                     sizeof(MOV_STORE_RAX_I64))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_BOOL_CONST: {
                if (!orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_BOOL_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                                                  (uint32_t)(inst->operands.load_const
                                                                 .immediate_bits &
                                                             0x1u)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_STORE_AL_BOOL,
                                                     sizeof(MOV_STORE_AL_BOOL))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_F64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.src_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_F64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_F64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVSD_LOAD_XMM0,
                                                     sizeof(MOVSD_LOAD_XMM0)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.move.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVSD_STORE_XMM0,
                                                     sizeof(MOVSD_STORE_XMM0))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u,
                                              (uint8_t)REG_TYPE_F64,
                                              &bail_patches) ||
                    !orus_jit_emit_load_typed_pointer(
                        &code, (uint32_t)ORUS_JIT_OFFSET_TYPED_F64_PTR,
                        &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVSD_LOAD_XMM0,
                                                     sizeof(MOVSD_LOAD_XMM0)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u,
                                              (uint8_t)REG_TYPE_F64,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVSD_LOAD_XMM1,
                                                     sizeof(MOVSD_LOAD_XMM1))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                const uint8_t* arithmetic_bytes = NULL;
                size_t arithmetic_size = 0u;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_ADD_F64:
                        arithmetic_bytes = ADDSD_XMM0_XMM1;
                        arithmetic_size = sizeof(ADDSD_XMM0_XMM1);
                        break;
                    case ORUS_JIT_IR_OP_SUB_F64:
                        arithmetic_bytes = SUBSD_XMM0_XMM1;
                        arithmetic_size = sizeof(SUBSD_XMM0_XMM1);
                        break;
                    case ORUS_JIT_IR_OP_MUL_F64:
                        arithmetic_bytes = MULSD_XMM0_XMM1;
                        arithmetic_size = sizeof(MULSD_XMM0_XMM1);
                        break;
                    default:
                        break;
                }

                if (!arithmetic_bytes || arithmetic_size == 0u ||
                    !orus_jit_code_buffer_emit_bytes(&code, arithmetic_bytes,
                                                     arithmetic_size) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOVSD_STORE_XMM0,
                                                     sizeof(MOVSD_STORE_XMM0))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_DIV_I32:
            case ORUS_JIT_IR_OP_DIV_I64:
            case ORUS_JIT_IR_OP_DIV_U32:
            case ORUS_JIT_IR_OP_DIV_U64:
            case ORUS_JIT_IR_OP_DIV_F64:
            case ORUS_JIT_IR_OP_MOD_I32:
            case ORUS_JIT_IR_OP_MOD_I64:
            case ORUS_JIT_IR_OP_MOD_U32:
            case ORUS_JIT_IR_OP_MOD_U64:
            case ORUS_JIT_IR_OP_MOD_F64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->opcode) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->value_kind) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x68) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_arithmetic)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX)) ||
                    !orus_jit_code_buffer_emit_bytes(
                        &code, (const uint8_t[]){0x48, 0x83, 0xC4, 0x08}, 4u) ||
                    !orus_jit_code_buffer_emit_bytes(&code,
                                                     (const uint8_t[]){0x84, 0xC0}, 2u) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                     &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CONCAT_STRING: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_concat_string)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX)) ||
                    !orus_jit_code_buffer_emit_bytes(&code,
                                                     (const uint8_t[]){0x84, 0xC0},
                                                     2u) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                     &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->opcode) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_compare_op)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TO_STRING: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_to_string)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX)) ||
                    !orus_jit_code_buffer_emit_bytes(&code,
                                                     (const uint8_t[]){0x84, 0xC0},
                                                     2u) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                     &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TYPE_OF: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.type_of.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.type_of.value_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_type_of)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX)) ||
                    !orus_jit_code_buffer_emit_bytes(&code,
                                                     (const uint8_t[]){0x84, 0xC0},
                                                     2u) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                     &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_IS_TYPE: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.is_type.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.is_type.value_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.is_type.type_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_is_type)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX)) ||
                    !orus_jit_code_buffer_emit_bytes(&code,
                                                     (const uint8_t[]){0x84, 0xC0},
                                                     2u) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                     &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TIME_STAMP: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.time_stamp.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_time_stamp)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MAKE_ARRAY: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, (uint64_t)(uintptr_t)inst) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_make_array)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_PUSH: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.array_push.array_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.array_push.value_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_array_push)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_POP: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.array_pop.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.array_pop.array_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_array_pop)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ENUM_NEW: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, (uint64_t)(uintptr_t)inst) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_enum_new)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_PRINT: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.print.first_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.print.arg_count) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.print.newline) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_print)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ASSERT_EQ: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.assert_eq.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.assert_eq.label_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.assert_eq.actual_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.assert_eq.expected_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_assert_eq)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CALL_NATIVE:
            case ORUS_JIT_IR_OP_CALL_FOREIGN: {
                if (inst->operands.call_native.spill_count > 0u) {
                    if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                         sizeof(MOV_RDI_R12)) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xBE) ||
                        !orus_jit_code_buffer_emit_u32(
                            &code, (uint32_t)inst->operands.call_native.spill_base) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                        !orus_jit_code_buffer_emit_u32(
                            &code, (uint32_t)inst->operands.call_native.spill_count) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                        !orus_jit_code_buffer_emit_u64(
                            &code, (uint64_t)(uintptr_t)
                                        &orus_jit_native_flush_typed_range) ||
                        !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                         sizeof(CALL_RAX))) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                }
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.call_native.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.call_native.first_arg_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.call_native.arg_count) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.call_native.native_index) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code,
                        (uint64_t)(uintptr_t)(inst->opcode ==
                                                     ORUS_JIT_IR_OP_CALL_FOREIGN
                                                 ? &orus_jit_native_call_foreign
                                                 : &orus_jit_native_call_native)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_GET_ITER: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.get_iter.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.get_iter.iterable_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_get_iter)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ITER_NEXT: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.iter_next.value_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.iter_next.iterator_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.iter_next.has_value_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_iter_next)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_RANGE: {
                const uint16_t* args = inst->operands.range.arg_regs;
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.range.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.range.arg_count) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x49) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, (uint64_t)(uintptr_t)args) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_range)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I32_TO_I64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i32_to_i64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_U64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_u64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_I32: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code, orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_i32)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I32_TO_F64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i32_to_f64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I64_TO_F64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i64_to_f64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_I32: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_i32)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_I64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_i64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_U32: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_u32)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_F64: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code,
                        (uint32_t)inst->operands.unary.src_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(&code,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_f64)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_INC_CMP_JUMP:
            case ORUS_JIT_IR_OP_DEC_CMP_JUMP: {
                bool is_increment = (inst->opcode == ORUS_JIT_IR_OP_INC_CMP_JUMP);
                OrusJitValueKind kind = inst->value_kind;
                uint16_t counter_reg = inst->operands.fused_loop.counter_reg;
                uint16_t limit_reg = inst->operands.fused_loop.limit_reg;
                int16_t jump_offset = inst->operands.fused_loop.jump_offset;
                int8_t step = inst->operands.fused_loop.step;
                uint8_t compare_kind = inst->operands.fused_loop.compare_kind;
                int32_t direction_value = is_increment ? 1 : -1;

                if (kind == ORUS_JIT_VALUE_BOXED ||
                    kind == ORUS_JIT_VALUE_F64) {
                    if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                         sizeof(MOV_RDI_R12)) ||
                        !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                         sizeof(MOV_RSI_RBX_BYTES)) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                        !orus_jit_code_buffer_emit_u32(&code, (uint32_t)kind) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                        !orus_jit_code_buffer_emit_u32(&code,
                                                       (uint32_t)counter_reg) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                        !orus_jit_code_buffer_emit_u32(&code,
                                                       (uint32_t)limit_reg) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x41) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                        !orus_jit_code_buffer_emit_u32(&code,
                                                       (uint32_t)(int32_t)step) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x68) ||
                        !orus_jit_code_buffer_emit_u32(&code,
                                                       (uint32_t)direction_value) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x68) ||
                        !orus_jit_code_buffer_emit_u32(&code,
                                                       (uint32_t)compare_kind) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                        !orus_jit_code_buffer_emit_u64(
                            &code,
                            orus_jit_function_ptr_bits(&orus_jit_native_fused_loop_step)) ||
                        !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                         sizeof(CALL_RAX)) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x83) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xC4) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x10)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }

                    if (!orus_jit_code_buffer_emit_u8(&code, 0x83) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xF8) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xFF)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                    if (!orus_jit_emit_conditional_jump(&code, 0x84u,
                                                         &bail_patches)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                    if (!orus_jit_code_buffer_emit_u8(&code, 0x85) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xC0)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                    if (!orus_jit_code_buffer_emit_u8(&code, 0x0F) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x8Fu)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }

                    size_t boxed_disp = code.size;
                    if (!orus_jit_code_buffer_emit_u32(&code, 0u)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }

                    uint32_t fallthrough = inst->bytecode_offset + 5u;
                    int64_t target_bytecode =
                        (int64_t)(int32_t)fallthrough + (int64_t)jump_offset;
                    if (target_bytecode < 0 ||
                        target_bytecode > (int64_t)UINT32_MAX) {
                        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                    }

                    if (!orus_jit_branch_patch_list_append(&branch_patches,
                                                           boxed_disp,
                                                           (uint32_t)target_bytecode)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                    break;
                }

                uint8_t reg_type = 0u;
                uint32_t typed_ptr_offset = 0u;
                const uint8_t* load_counter_bytes = NULL;
                size_t load_counter_size = 0u;
                const uint8_t* load_limit_bytes = NULL;
                size_t load_limit_size = 0u;
                const uint8_t* store_counter_bytes = NULL;
                size_t store_counter_size = 0u;
                bool use_cached_i32_base = false;
                bool is_signed_kind = false;
                bool is_32bit_kind = false;

                switch (kind) {
                    case ORUS_JIT_VALUE_I32:
                        reg_type = (uint8_t)REG_TYPE_I32;
                        load_counter_bytes = MOV_LOAD_EAX;
                        load_counter_size = sizeof(MOV_LOAD_EAX);
                        load_limit_bytes = MOV_LOAD_EDX;
                        load_limit_size = sizeof(MOV_LOAD_EDX);
                        store_counter_bytes = MOV_STORE_EAX;
                        store_counter_size = sizeof(MOV_STORE_EAX);
                        use_cached_i32_base = true;
                        is_signed_kind = true;
                        is_32bit_kind = true;
                        break;
                    case ORUS_JIT_VALUE_I64:
                        reg_type = (uint8_t)REG_TYPE_I64;
                        typed_ptr_offset = (uint32_t)ORUS_JIT_OFFSET_TYPED_I64_PTR;
                        load_counter_bytes = MOV_LOAD_RAX_I64;
                        load_counter_size = sizeof(MOV_LOAD_RAX_I64);
                        load_limit_bytes = MOV_LOAD_RDX_I64;
                        load_limit_size = sizeof(MOV_LOAD_RDX_I64);
                        store_counter_bytes = MOV_STORE_RAX_I64;
                        store_counter_size = sizeof(MOV_STORE_RAX_I64);
                        is_signed_kind = true;
                        break;
                    case ORUS_JIT_VALUE_U32:
                        reg_type = (uint8_t)REG_TYPE_U32;
                        typed_ptr_offset = (uint32_t)ORUS_JIT_OFFSET_TYPED_U32_PTR;
                        load_counter_bytes = MOV_LOAD_EAX_U32;
                        load_counter_size = sizeof(MOV_LOAD_EAX_U32);
                        load_limit_bytes = MOV_LOAD_EDX_U32;
                        load_limit_size = sizeof(MOV_LOAD_EDX_U32);
                        store_counter_bytes = MOV_STORE_EAX_U32;
                        store_counter_size = sizeof(MOV_STORE_EAX_U32);
                        is_32bit_kind = true;
                        break;
                    case ORUS_JIT_VALUE_U64:
                        reg_type = (uint8_t)REG_TYPE_U64;
                        typed_ptr_offset = (uint32_t)ORUS_JIT_OFFSET_TYPED_U64_PTR;
                        load_counter_bytes = MOV_LOAD_RAX_I64;
                        load_counter_size = sizeof(MOV_LOAD_RAX_I64);
                        load_limit_bytes = MOV_LOAD_RDX_I64;
                        load_limit_size = sizeof(MOV_LOAD_RDX_I64);
                        store_counter_bytes = MOV_STORE_RAX_I64;
                        store_counter_size = sizeof(MOV_STORE_RAX_I64);
                        break;
                    default:
                        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }

                if ((is_increment && step <= 0) || (!is_increment && step >= 0) ||
                    step == 0) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }

                if (!orus_jit_code_buffer_emit_u8(&code, 0xB9) ||
                    !orus_jit_code_buffer_emit_u32(&code, (uint32_t)counter_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x01u, reg_type, &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                if (!use_cached_i32_base) {
                    if (!orus_jit_emit_load_typed_pointer(&code, typed_ptr_offset,
                                                         &bail_patches)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                }

                if (!orus_jit_code_buffer_emit_bytes(&code, load_counter_bytes,
                                                     load_counter_size)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                if (!orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(&code, (uint32_t)limit_reg) ||
                    !orus_jit_emit_type_guard(&code, 0x02u, reg_type,
                                              &bail_patches) ||
                    !orus_jit_code_buffer_emit_bytes(&code, load_limit_bytes,
                                                     load_limit_size)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                const uint8_t* step_bytes = NULL;
                size_t step_size = 0u;
                if (is_32bit_kind) {
                    step_bytes = (step > 0) ? ADD_EAX_IMM1 : SUB_EAX_IMM1;
                    step_size = sizeof(ADD_EAX_IMM1);
                } else {
                    step_bytes = (step > 0) ? ADD_RAX_IMM1 : SUB_RAX_IMM1;
                    step_size = sizeof(ADD_RAX_IMM1);
                }

                if (!orus_jit_code_buffer_emit_bytes(&code, step_bytes, step_size)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                if (is_signed_kind) {
                    if (!orus_jit_emit_conditional_jump(&code, 0x80u, &bail_patches)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                }

                if (!orus_jit_code_buffer_emit_bytes(&code, store_counter_bytes,
                                                     store_counter_size)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                if (is_32bit_kind) {
                    if (!orus_jit_code_buffer_emit_u8(&code, 0x39) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xD0)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                } else {
                    if (!orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0x39) ||
                        !orus_jit_code_buffer_emit_u8(&code, 0xD0)) {
                        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                }

                uint8_t branch_opcode = 0u;
                switch (compare_kind) {
                    case ORUS_JIT_IR_LOOP_COMPARE_LESS_THAN:
                        branch_opcode = is_signed_kind ? 0x8Cu : 0x82u;
                        break;
                    case ORUS_JIT_IR_LOOP_COMPARE_GREATER_THAN:
                        branch_opcode = is_signed_kind ? 0x8Fu : 0x87u;
                        break;
                    default:
                        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }

                if (!orus_jit_code_buffer_emit_u8(&code, 0x0F) ||
                    !orus_jit_code_buffer_emit_u8(&code, branch_opcode)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                size_t disp_offset = code.size;
                if (!orus_jit_code_buffer_emit_u32(&code, 0u)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint32_t fallthrough = inst->bytecode_offset + 5u;
                int64_t target_bytecode =
                    (int64_t)(int32_t)fallthrough + (int64_t)jump_offset;
                if (target_bytecode < 0 || target_bytecode > (int64_t)UINT32_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }

                if (!orus_jit_branch_patch_list_append(&branch_patches, disp_offset,
                                                       (uint32_t)target_bytecode)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_SAFEPOINT: {
                if (!orus_jit_emit_safepoint_call(&code, &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_SHORT: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xE9)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                size_t disp_offset = code.size;
                if (!orus_jit_code_buffer_emit_u32(&code, 0u) ||
                    !orus_jit_branch_patch_list_append(
                        &branch_patches, disp_offset,
                        inst->bytecode_offset +
                            inst->operands.jump_short.bytecode_length +
                            inst->operands.jump_short.offset)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xE9)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                size_t disp_offset = code.size;
                if (!orus_jit_code_buffer_emit_u32(&code, 0u)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                uint32_t fallthrough = inst->bytecode_offset + 2u;
                uint16_t back = inst->operands.jump_back_short.back_offset;
                if (fallthrough < back || !inst_offsets) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                uint32_t target = fallthrough - back;
                size_t target_index =
                    orus_jit_program_find_index(&block->program, target);
                if (target_index == SIZE_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                size_t target_code = inst_offsets[target_index];
                if (target_code == SIZE_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                int64_t rel = (int64_t)target_code -
                              ((int64_t)disp_offset + 4);
                int32_t disp = (int32_t)rel;
                memcpy(code.data + disp_offset, &disp, sizeof(int32_t));
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: {
                if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RDI_R12,
                                                     sizeof(MOV_RDI_R12)) ||
                    !orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                                     sizeof(MOV_RSI_RBX_BYTES)) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xBA) ||
                    !orus_jit_code_buffer_emit_u32(
                        &code, (uint32_t)inst->operands.jump_if_not_short
                                    .predicate_reg) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
                    !orus_jit_code_buffer_emit_u64(
                        &code,
                        (uint64_t)(uintptr_t)
                            &orus_jit_native_evaluate_branch_false) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                if (!orus_jit_code_buffer_emit_u8(&code, 0x83) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xF8) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xFF) ||
                    !orus_jit_emit_conditional_jump(&code, 0x84u,
                                                    &bail_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                if (!orus_jit_code_buffer_emit_u8(&code, 0x85) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0xC0)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                if (!orus_jit_code_buffer_emit_u8(&code, 0x0F) ||
                    !orus_jit_code_buffer_emit_u8(&code, 0x85)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                size_t disp_offset = code.size;
                if (!orus_jit_code_buffer_emit_u32(&code, 0u) ||
                    !orus_jit_branch_patch_list_append(
                        &branch_patches, disp_offset,
                        inst->bytecode_offset +
                            inst->operands.jump_if_not_short.bytecode_length +
                            inst->operands.jump_if_not_short.offset)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK: {
                if (!inst_offsets) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                size_t header_index = orus_jit_program_find_index(
                    &block->program, block->program.loop_start_offset);
                if (header_index == SIZE_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                size_t header_offset = inst_offsets[header_index];
                if (header_offset == SIZE_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                if (!orus_jit_code_buffer_emit_u8(&code, 0xE9)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                size_t disp_offset = code.size;
                int64_t rel = (int64_t)header_offset -
                              ((int64_t)disp_offset + 4);
                if (rel < INT32_MIN || rel > INT32_MAX) {
                    RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                uint32_t disp = (uint32_t)(int32_t)rel;
                if (!orus_jit_code_buffer_emit_u32(&code, disp)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                goto finalize_block;
            }
            case ORUS_JIT_IR_OP_RETURN: {
                if (!orus_jit_emit_return_placeholder(&code, &return_patches)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                goto finalize_block;
            }
            default:
                RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
        }
    }

finalize_block:;
    for (size_t i = 0; i < branch_patches.count; ++i) {
        OrusJitBranchPatch* patch = &branch_patches.data[i];
        if (!inst_offsets) {
            RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_index = orus_jit_program_find_index(
            &block->program, patch->target_bytecode);
        if (target_index == SIZE_MAX) {
            if (!orus_jit_offset_list_append(&bail_patches,
                                             patch->code_offset)) {
                RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
            }
            continue;
        }
        size_t target_code = inst_offsets[target_index];
        if (target_code == SIZE_MAX) {
            RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        int64_t rel = (int64_t)target_code -
                      ((int64_t)patch->code_offset + 4);
        int32_t disp = (int32_t)rel;
        memcpy(code.data + patch->code_offset, &disp, sizeof(int32_t));
    }

    size_t bail_label_offset = code.size;
    if (!orus_jit_code_buffer_emit_bytes(&code, MOV_RSI_RBX_BYTES,
                                         sizeof(MOV_RSI_RBX_BYTES)) ||
        !orus_jit_code_buffer_emit_u8(&code, 0x48) ||
        !orus_jit_code_buffer_emit_u8(&code, 0xB8) ||
        !orus_jit_code_buffer_emit_u64(
            &code, orus_jit_function_ptr_bits(&orus_jit_native_type_bailout)) ||
        !orus_jit_code_buffer_emit_bytes(&code, (const uint8_t[]){0xFF, 0xD0}, 2u)) {
        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (!orus_jit_emit_return_placeholder(&code, &return_patches)) {
        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
    }

    size_t epilogue_offset = code.size;
    if (!orus_jit_emit_linear_epilogue(&code)) {
        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
    }

    int64_t bail_rel = (int64_t)bail_label_offset -
                       ((int64_t)bail_disp_offset + 4);
    int32_t bail_disp = (int32_t)bail_rel;
    memcpy(code.data + bail_disp_offset, &bail_disp, sizeof(int32_t));

    for (size_t i = 0; i < bail_patches.count; ++i) {
        size_t disp_offset = bail_patches.data[i];
        int64_t rel = (int64_t)bail_label_offset -
                      ((int64_t)disp_offset + 4);
        int32_t disp = (int32_t)rel;
        memcpy(code.data + disp_offset, &disp, sizeof(int32_t));
    }

    for (size_t i = 0; i < return_patches.count; ++i) {
        size_t disp_offset = return_patches.data[i];
        int64_t rel = (int64_t)epilogue_offset -
                      ((int64_t)disp_offset + 4);
        int32_t disp = (int32_t)rel;
        memcpy(code.data + disp_offset, &disp, sizeof(int32_t));
    }

    size_t capacity = 0u;
    void* buffer = orus_jit_alloc_executable(code.size, backend->page_size,
                                             &capacity);
    if (!buffer) {
        RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
    }
    memcpy(buffer, code.data, code.size);
    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
    }

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
    }
#endif

    orus_jit_flush_icache(buffer, code.size);

    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->code_ptr = buffer;
    entry->code_size = code.size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_linear_x86";

    block->code_ptr = buffer;
    block->code_capacity = capacity;
    orus_jit_debug_publish_disassembly(&block->program,
                                       ORUS_JIT_BACKEND_TARGET_X86_64,
                                       buffer,
                                       code.size);

    orus_jit_code_buffer_release(&code);
    orus_jit_offset_list_release(&return_patches);
    orus_jit_offset_list_release(&bail_patches);
    orus_jit_branch_patch_list_release(&branch_patches);
    free(inst_offsets);
#undef RETURN_WITH
    return JIT_BACKEND_OK;
}
#endif // defined(__x86_64__) || defined(_M_X64)

#if ORUS_JIT_HAS_DYNASM_X86
struct DynAsmActionBuffer {
    unsigned char* data;
    size_t size;
    size_t capacity;
};

typedef struct DynAsmPatch {
    size_t action_offset;
    size_t code_offset;
} DynAsmPatch;

typedef struct DynAsmPatchList {
    DynAsmPatch* data;
    size_t count;
    size_t capacity;
} DynAsmPatchList;

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
dynasm_emit_bytes(struct DynAsmActionBuffer* buffer,
                  const uint8_t* bytes,
                  size_t count) {
    if (!buffer || (!bytes && count)) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!dynasm_action_buffer_push(buffer, DASM_ESC) ||
            !dynasm_action_buffer_push(buffer, bytes[i])) {
            return false;
        }
    }
    return true;
}

static void
dynasm_patch_list_init(DynAsmPatchList* list) {
    if (!list) {
        return;
    }
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void
dynasm_patch_list_release(DynAsmPatchList* list) {
    if (!list) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
dynasm_patch_list_append(DynAsmPatchList* list,
                         size_t action_offset,
                         size_t code_offset) {
    if (!list) {
        return false;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 4u;
        DynAsmPatch* data =
            (DynAsmPatch*)realloc(list->data, new_capacity * sizeof(DynAsmPatch));
        if (!data) {
            return false;
        }
        list->data = data;
        list->capacity = new_capacity;
    }
    list->data[list->count].action_offset = action_offset;
    list->data[list->count].code_offset = code_offset;
    list->count++;
    return true;
}

static bool
dynasm_emit_bytes_track(struct DynAsmActionBuffer* buffer,
                        size_t* code_offset,
                        const uint8_t* bytes,
                        size_t count) {
    if (!dynasm_emit_bytes(buffer, bytes, count)) {
        return false;
    }
    if (code_offset) {
        *code_offset += count;
    }
    return true;
}

static bool
dynasm_emit_u32_le(struct DynAsmActionBuffer* buffer,
                   size_t* code_offset,
                   uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24) & 0xFFu);
    return dynasm_emit_bytes_track(buffer, code_offset, bytes, sizeof(bytes));
}

static bool
dynasm_emit_u64_le(struct DynAsmActionBuffer* buffer,
                   size_t* code_offset,
                   uint64_t value) {
    uint8_t bytes[8];
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = (uint8_t)((value >> (8u * i)) & 0xFFu);
    }
    return dynasm_emit_bytes_track(buffer, code_offset, bytes, sizeof(bytes));
}

static bool
dynasm_emit_mov_reg_imm32(struct DynAsmActionBuffer* buffer,
                          uint8_t opcode,
                          size_t* code_offset,
                          uint32_t immediate) {
    uint8_t bytes[5];
    bytes[0] = opcode;
    bytes[1] = (uint8_t)(immediate & 0xFFu);
    bytes[2] = (uint8_t)((immediate >> 8) & 0xFFu);
    bytes[3] = (uint8_t)((immediate >> 16) & 0xFFu);
    bytes[4] = (uint8_t)((immediate >> 24) & 0xFFu);
    return dynasm_emit_bytes_track(buffer, code_offset, bytes, sizeof(bytes));
}

static bool
dynasm_emit_load_vm_block(struct DynAsmActionBuffer* buffer, size_t* code_offset) {
    return dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) &&
           dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u);
}

static bool
dynasm_emit_helper_call(struct DynAsmActionBuffer* buffer,
                        size_t* code_offset,
                        const void* helper,
                        OrusJitHelperStubKind stub_kind) {
    if (!orus_jit_helper_registry_record(helper, stub_kind, NULL)) {
        return false;
    }
    const void* stub = orus_jit_helper_stub_address(stub_kind);
    if (!stub) {
        return false;
    }
    return dynasm_emit_bytes_track(buffer, code_offset, (const uint8_t[]){0x48, 0xB8},
                                   2u) &&
           dynasm_emit_u64_le(buffer, code_offset,
                              orus_jit_function_ptr_bits(helper)) &&
           dynasm_emit_bytes_track(buffer, code_offset, (const uint8_t[]){0x49, 0xBB},
                                   2u) &&
           dynasm_emit_u64_le(buffer, code_offset,
                              orus_jit_function_ptr_bits(stub)) &&
           dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x41, 0xFF, 0xD3}, 3u);
}

static bool
dynasm_emit_stack_push_u32(struct DynAsmActionBuffer* buffer,
                           size_t* code_offset,
                           uint32_t value) {
    return dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x48, 0x83, 0xEC, 0x08}, 4u) &&
           dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x48, 0xC7, 0x04, 0x24}, 4u) &&
           dynasm_emit_u32_le(buffer, code_offset, value);
}

static bool
dynasm_emit_stack_pop(struct DynAsmActionBuffer* buffer, size_t* code_offset) {
    return dynasm_emit_bytes_track(buffer, code_offset,
                                   (const uint8_t[]){0x48, 0x83, 0xC4, 0x08}, 4u);
}

static void
dynasm_patch_u32(struct DynAsmActionBuffer* buffer,
                 size_t action_offset,
                 uint32_t value) {
    if (!buffer || !buffer->data || action_offset + 7u >= buffer->size) {
        return;
    }
    buffer->data[action_offset + 1u] = (uint8_t)(value & 0xFFu);
    buffer->data[action_offset + 3u] = (uint8_t)((value >> 8) & 0xFFu);
    buffer->data[action_offset + 5u] = (uint8_t)((value >> 16) & 0xFFu);
    buffer->data[action_offset + 7u] = (uint8_t)((value >> 24) & 0xFFu);
}

typedef struct {
    size_t start;
    size_t end;
    size_t successors[2];
    size_t successor_count;
    uint32_t weight;
    uint16_t unique_reg_count;
    uint16_t invariant_defs;
} DynAsmBasicBlockInfo;

typedef struct {
    bool known;
    bool is_constant;
    uint64_t bits;
    OrusJitValueKind kind;
} DynAsmRegisterState;

typedef struct {
    bool skip;
    bool replaced;
    OrusJitIRInstruction inst;
} DynAsmPeepholeResult;

static void
dynasm_register_state_init(DynAsmRegisterState* state) {
    if (!state) {
        return;
    }
    for (size_t i = 0; i < REGISTER_COUNT; ++i) {
        state[i].known = false;
        state[i].is_constant = false;
        state[i].bits = 0u;
        state[i].kind = ORUS_JIT_VALUE_BOXED;
    }
}

static void
dynasm_register_state_reset_all(DynAsmRegisterState* state) {
    if (!state) {
        return;
    }
    for (size_t i = 0; i < REGISTER_COUNT; ++i) {
        state[i].known = false;
        state[i].is_constant = false;
        state[i].bits = 0u;
        state[i].kind = ORUS_JIT_VALUE_BOXED;
    }
}

static void
dynasm_register_state_invalidate(DynAsmRegisterState* state, uint16_t reg) {
    if (!state || reg >= REGISTER_COUNT) {
        return;
    }
    state[reg].known = false;
    state[reg].is_constant = false;
    state[reg].bits = 0u;
    state[reg].kind = ORUS_JIT_VALUE_BOXED;
}

static bool
dynasm_vector_opcode_supported(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_F64:
            return true;
        default:
            return false;
    }
}

static void
dynasm_mark_vector_pairs(const OrusJitIRProgram* program,
                         OrusJitIRInstruction* instructions,
                         size_t count) {
    if (!program || !instructions || count < 2) {
        return;
    }
    for (size_t i = 0; i + 1 < count; ++i) {
        OrusJitIRInstruction* head = &instructions[i];
        OrusJitIRInstruction* tail = &instructions[i + 1];
        if (!dynasm_vector_opcode_supported(head->opcode)) {
            continue;
        }
        if (head->opcode != tail->opcode) {
            continue;
        }
        if (head->value_kind != tail->value_kind) {
            continue;
        }
        if (head->optimization_flags & ORUS_JIT_IR_FLAG_VECTOR_HEAD) {
            continue;
        }
        if (tail->optimization_flags & ORUS_JIT_IR_FLAG_VECTOR_HEAD) {
            continue;
        }
        uint16_t head_dst = head->operands.arithmetic.dst_reg;
        uint16_t tail_dst = tail->operands.arithmetic.dst_reg;
        uint16_t head_lhs = head->operands.arithmetic.lhs_reg;
        uint16_t head_rhs = head->operands.arithmetic.rhs_reg;
        uint16_t tail_lhs = tail->operands.arithmetic.lhs_reg;
        uint16_t tail_rhs = tail->operands.arithmetic.rhs_reg;
        if ((uint16_t)(head_dst + 1u) != tail_dst) {
            continue;
        }
        if ((uint16_t)(head_lhs + 1u) != tail_lhs) {
            continue;
        }
        if ((uint16_t)(head_rhs + 1u) != tail_rhs) {
            continue;
        }
        uint32_t start = program->loop_start_offset;
        uint32_t end = program->loop_end_offset;
        if (!(head->bytecode_offset >= start && head->bytecode_offset < end)) {
            continue;
        }
        if (!(tail->bytecode_offset >= start && tail->bytecode_offset < end)) {
            continue;
        }
        head->optimization_flags |= ORUS_JIT_IR_FLAG_VECTOR_HEAD;
        tail->optimization_flags |= ORUS_JIT_IR_FLAG_VECTOR_TAIL;
        ++i;
    }
}

static bool
dynasm_opcode_is_move(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_MOVE_I32:
        case ORUS_JIT_IR_OP_MOVE_I64:
        case ORUS_JIT_IR_OP_MOVE_U32:
        case ORUS_JIT_IR_OP_MOVE_U64:
        case ORUS_JIT_IR_OP_MOVE_F64:
        case ORUS_JIT_IR_OP_MOVE_BOOL:
        case ORUS_JIT_IR_OP_MOVE_STRING:
        case ORUS_JIT_IR_OP_MOVE_VALUE:
            return true;
        default:
            return false;
    }
}

static bool
dynasm_opcode_is_numeric_load(OrusJitIROpcode opcode) {
    switch (opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST:
        case ORUS_JIT_IR_OP_LOAD_I64_CONST:
        case ORUS_JIT_IR_OP_LOAD_U32_CONST:
        case ORUS_JIT_IR_OP_LOAD_U64_CONST:
        case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            return true;
        default:
            return false;
    }
}

static bool
dynasm_instruction_dest_reg(const OrusJitIRInstruction* inst, uint16_t* out_reg) {
    if (!inst || !out_reg) {
        return false;
    }
    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST:
        case ORUS_JIT_IR_OP_LOAD_I64_CONST:
        case ORUS_JIT_IR_OP_LOAD_U32_CONST:
        case ORUS_JIT_IR_OP_LOAD_U64_CONST:
        case ORUS_JIT_IR_OP_LOAD_F64_CONST:
        case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
        case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
            *out_reg = inst->operands.load_const.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_MOVE_I32:
        case ORUS_JIT_IR_OP_MOVE_I64:
        case ORUS_JIT_IR_OP_MOVE_U32:
        case ORUS_JIT_IR_OP_MOVE_U64:
        case ORUS_JIT_IR_OP_MOVE_F64:
        case ORUS_JIT_IR_OP_MOVE_BOOL:
        case ORUS_JIT_IR_OP_MOVE_STRING:
        case ORUS_JIT_IR_OP_MOVE_VALUE:
            *out_reg = inst->operands.move.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
        case ORUS_JIT_IR_OP_DIV_I32:
        case ORUS_JIT_IR_OP_DIV_I64:
        case ORUS_JIT_IR_OP_DIV_U32:
        case ORUS_JIT_IR_OP_DIV_U64:
        case ORUS_JIT_IR_OP_DIV_F64:
        case ORUS_JIT_IR_OP_MOD_I32:
        case ORUS_JIT_IR_OP_MOD_I64:
        case ORUS_JIT_IR_OP_MOD_U32:
        case ORUS_JIT_IR_OP_MOD_U64:
        case ORUS_JIT_IR_OP_MOD_F64:
        case ORUS_JIT_IR_OP_LT_I32:
        case ORUS_JIT_IR_OP_LE_I32:
        case ORUS_JIT_IR_OP_GT_I32:
        case ORUS_JIT_IR_OP_GE_I32:
        case ORUS_JIT_IR_OP_LT_I64:
        case ORUS_JIT_IR_OP_LE_I64:
        case ORUS_JIT_IR_OP_GT_I64:
        case ORUS_JIT_IR_OP_GE_I64:
        case ORUS_JIT_IR_OP_LT_U32:
        case ORUS_JIT_IR_OP_LE_U32:
        case ORUS_JIT_IR_OP_GT_U32:
        case ORUS_JIT_IR_OP_GE_U32:
        case ORUS_JIT_IR_OP_LT_U64:
        case ORUS_JIT_IR_OP_LE_U64:
        case ORUS_JIT_IR_OP_GT_U64:
        case ORUS_JIT_IR_OP_GE_U64:
        case ORUS_JIT_IR_OP_LT_F64:
        case ORUS_JIT_IR_OP_LE_F64:
        case ORUS_JIT_IR_OP_GT_F64:
        case ORUS_JIT_IR_OP_GE_F64:
        case ORUS_JIT_IR_OP_EQ_I32:
        case ORUS_JIT_IR_OP_NE_I32:
        case ORUS_JIT_IR_OP_EQ_I64:
        case ORUS_JIT_IR_OP_NE_I64:
        case ORUS_JIT_IR_OP_EQ_U32:
        case ORUS_JIT_IR_OP_NE_U32:
        case ORUS_JIT_IR_OP_EQ_U64:
        case ORUS_JIT_IR_OP_NE_U64:
        case ORUS_JIT_IR_OP_EQ_F64:
        case ORUS_JIT_IR_OP_NE_F64:
        case ORUS_JIT_IR_OP_EQ_BOOL:
        case ORUS_JIT_IR_OP_NE_BOOL:
            *out_reg = inst->operands.arithmetic.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_I32_TO_I64:
        case ORUS_JIT_IR_OP_U32_TO_U64:
        case ORUS_JIT_IR_OP_U32_TO_I32:
        case ORUS_JIT_IR_OP_I32_TO_F64:
        case ORUS_JIT_IR_OP_I64_TO_F64:
        case ORUS_JIT_IR_OP_F64_TO_I32:
        case ORUS_JIT_IR_OP_F64_TO_I64:
        case ORUS_JIT_IR_OP_F64_TO_U32:
        case ORUS_JIT_IR_OP_U32_TO_F64:
        case ORUS_JIT_IR_OP_I32_TO_U32:
        case ORUS_JIT_IR_OP_I64_TO_U32:
        case ORUS_JIT_IR_OP_I32_TO_U64:
        case ORUS_JIT_IR_OP_I64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_I32:
        case ORUS_JIT_IR_OP_U64_TO_I64:
        case ORUS_JIT_IR_OP_U64_TO_U32:
        case ORUS_JIT_IR_OP_F64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_F64:
            *out_reg = inst->operands.unary.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_TYPE_OF:
            *out_reg = inst->operands.type_of.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_IS_TYPE:
            *out_reg = inst->operands.is_type.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_GET_ITER:
            *out_reg = inst->operands.get_iter.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_ITER_NEXT:
            *out_reg = inst->operands.iter_next.value_reg;
            return true;
        case ORUS_JIT_IR_OP_RANGE:
            *out_reg = inst->operands.range.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_MAKE_ARRAY:
            *out_reg = inst->operands.make_array.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_ARRAY_POP:
            *out_reg = inst->operands.array_pop.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_ENUM_NEW:
            *out_reg = inst->operands.enum_new.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_ASSERT_EQ:
            *out_reg = inst->operands.assert_eq.dst_reg;
            return true;
        case ORUS_JIT_IR_OP_PRINT:
        case ORUS_JIT_IR_OP_SAFEPOINT:
        case ORUS_JIT_IR_OP_CALL_NATIVE:
        case ORUS_JIT_IR_OP_CALL_FOREIGN:
        case ORUS_JIT_IR_OP_LOOP_BACK:
        case ORUS_JIT_IR_OP_RETURN:
        case ORUS_JIT_IR_OP_JUMP_SHORT:
        case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
        case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
        case ORUS_JIT_IR_OP_INC_CMP_JUMP:
        case ORUS_JIT_IR_OP_DEC_CMP_JUMP:
            break;
        default:
            break;
    }
    return false;
}

static bool
dynasm_instruction_is_block_terminator(const OrusJitIRInstruction* inst) {
    if (!inst) {
        return false;
    }
    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_RETURN:
        case ORUS_JIT_IR_OP_LOOP_BACK:
        case ORUS_JIT_IR_OP_JUMP_SHORT:
        case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
        case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
        case ORUS_JIT_IR_OP_INC_CMP_JUMP:
        case ORUS_JIT_IR_OP_DEC_CMP_JUMP:
            return true;
        default:
            return false;
    }
}

static void
dynasm_add_successor(DynAsmBasicBlockInfo* block, size_t successor) {
    if (!block || successor == SIZE_MAX) {
        return;
    }
    for (size_t i = 0; i < block->successor_count; ++i) {
        if (block->successors[i] == successor) {
            return;
        }
    }
    if (block->successor_count < (sizeof(block->successors) / sizeof(block->successors[0]))) {
        block->successors[block->successor_count++] = successor;
    }
}

static void
dynasm_mark_branch_target(const OrusJitIRProgram* program,
                          const OrusJitIRInstruction* inst,
                          uint8_t* block_starts) {
    if (!program || !inst || !block_starts) {
        return;
    }

    uint32_t target_bytecode = 0u;
    bool has_target = false;
    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_JUMP_SHORT: {
            uint32_t fallthrough = inst->bytecode_offset +
                                   inst->operands.jump_short.bytecode_length;
            target_bytecode = fallthrough + inst->operands.jump_short.offset;
            has_target = true;
            break;
        }
        case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: {
            uint32_t fallthrough = inst->bytecode_offset + 2u;
            if (fallthrough >= inst->operands.jump_back_short.back_offset) {
                target_bytecode = fallthrough - inst->operands.jump_back_short.back_offset;
                has_target = true;
            }
            break;
        }
        case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: {
            uint32_t fallthrough = inst->bytecode_offset +
                                   inst->operands.jump_if_not_short.bytecode_length;
            target_bytecode = fallthrough + inst->operands.jump_if_not_short.offset;
            has_target = true;
            break;
        }
        case ORUS_JIT_IR_OP_INC_CMP_JUMP:
        case ORUS_JIT_IR_OP_DEC_CMP_JUMP: {
            uint32_t fallthrough = inst->bytecode_offset + 5u;
            int64_t projected = (int64_t)(int32_t)fallthrough +
                                (int64_t)inst->operands.fused_loop.jump_offset;
            if (projected >= 0 && projected <= (int64_t)UINT32_MAX) {
                target_bytecode = (uint32_t)projected;
                has_target = true;
            }
            break;
        }
        case ORUS_JIT_IR_OP_LOOP_BACK: {
            if (inst->bytecode_offset >= inst->operands.loop_back.back_offset) {
                target_bytecode = inst->bytecode_offset - inst->operands.loop_back.back_offset;
                has_target = true;
            }
            break;
        }
        default:
            break;
    }

    if (has_target) {
        size_t target_index = orus_jit_program_find_index(program, target_bytecode);
        if (target_index != SIZE_MAX) {
            block_starts[target_index] = 1u;
        }
    }
}

static void
dynasm_schedule_dfs(const DynAsmBasicBlockInfo* blocks,
                    size_t block_count,
                    size_t index,
                    bool* visited,
                    size_t* order,
                    size_t* order_count) {
    if (!blocks || !visited || !order || !order_count || index >= block_count) {
        return;
    }
    if (visited[index]) {
        return;
    }
    visited[index] = true;
    order[(*order_count)++] = index;

    size_t successors[2];
    size_t succ_count = blocks[index].successor_count;
    for (size_t i = 0; i < succ_count; ++i) {
        successors[i] = blocks[index].successors[i];
    }
    if (succ_count == 2) {
        const DynAsmBasicBlockInfo* lhs = &blocks[successors[0]];
        const DynAsmBasicBlockInfo* rhs = &blocks[successors[1]];
        bool prefer_rhs = false;
        if (rhs->weight > lhs->weight) {
            prefer_rhs = true;
        } else if (rhs->weight == lhs->weight) {
            if (rhs->unique_reg_count < lhs->unique_reg_count) {
                prefer_rhs = true;
            } else if (rhs->unique_reg_count == lhs->unique_reg_count &&
                       rhs->invariant_defs > lhs->invariant_defs) {
                prefer_rhs = true;
            }
        }
        if (prefer_rhs) {
            size_t tmp = successors[0];
            successors[0] = successors[1];
            successors[1] = tmp;
        }
    }
    for (size_t i = 0; i < succ_count; ++i) {
        dynasm_schedule_dfs(blocks, block_count, successors[i], visited, order, order_count);
    }
}

static OrusJitIROpcode
dynasm_load_opcode_for_kind(OrusJitValueKind kind) {
    switch (kind) {
        case ORUS_JIT_VALUE_I32:
            return ORUS_JIT_IR_OP_LOAD_I32_CONST;
        case ORUS_JIT_VALUE_I64:
            return ORUS_JIT_IR_OP_LOAD_I64_CONST;
        case ORUS_JIT_VALUE_U32:
            return ORUS_JIT_IR_OP_LOAD_U32_CONST;
        case ORUS_JIT_VALUE_U64:
            return ORUS_JIT_IR_OP_LOAD_U64_CONST;
        case ORUS_JIT_VALUE_F64:
            return ORUS_JIT_IR_OP_LOAD_F64_CONST;
        default:
            break;
    }
    return ORUS_JIT_IR_OP_LOAD_VALUE_CONST;
}

static inline double
dynasm_bits_to_f64(uint64_t bits) {
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint64_t
dynasm_f64_to_bits(double value) {
    uint64_t bits = 0u;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool
dynasm_try_fold_arithmetic(DynAsmRegisterState* regs,
                           const OrusJitIRInstruction* inst,
                           DynAsmPeepholeResult* result) {
    if (!regs || !inst || !result) {
        return false;
    }

    uint16_t dst = inst->operands.arithmetic.dst_reg;
    uint16_t lhs = inst->operands.arithmetic.lhs_reg;
    uint16_t rhs = inst->operands.arithmetic.rhs_reg;
    if (lhs >= REGISTER_COUNT || rhs >= REGISTER_COUNT || dst >= REGISTER_COUNT) {
        dynasm_register_state_invalidate(regs, dst);
        return false;
    }
    if (!regs[lhs].known || !regs[rhs].known || !regs[lhs].is_constant ||
        !regs[rhs].is_constant) {
        dynasm_register_state_invalidate(regs, dst);
        return false;
    }
    if (regs[lhs].kind != inst->value_kind || regs[rhs].kind != inst->value_kind) {
        dynasm_register_state_invalidate(regs, dst);
        return false;
    }

    uint64_t bits = 0u;
    bool folded = false;
    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_ADD_I32:
            bits = (uint64_t)(uint32_t)((int32_t)regs[lhs].bits + (int32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_I32:
            bits = (uint64_t)(uint32_t)((int32_t)regs[lhs].bits - (int32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_I32:
            bits = (uint64_t)(uint32_t)((int32_t)regs[lhs].bits * (int32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_I64:
            bits = (uint64_t)((int64_t)regs[lhs].bits + (int64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_I64:
            bits = (uint64_t)((int64_t)regs[lhs].bits - (int64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_I64:
            bits = (uint64_t)((int64_t)regs[lhs].bits * (int64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_U32:
            bits = (uint64_t)(uint32_t)((uint32_t)regs[lhs].bits + (uint32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_U32:
            bits = (uint64_t)(uint32_t)((uint32_t)regs[lhs].bits - (uint32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_U32:
            bits = (uint64_t)(uint32_t)((uint32_t)regs[lhs].bits * (uint32_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_U64:
            bits = (uint64_t)((uint64_t)regs[lhs].bits + (uint64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_SUB_U64:
            bits = (uint64_t)((uint64_t)regs[lhs].bits - (uint64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_MUL_U64:
            bits = (uint64_t)((uint64_t)regs[lhs].bits * (uint64_t)regs[rhs].bits);
            folded = true;
            break;
        case ORUS_JIT_IR_OP_ADD_F64: {
            double value = dynasm_bits_to_f64(regs[lhs].bits) +
                           dynasm_bits_to_f64(regs[rhs].bits);
            bits = dynasm_f64_to_bits(value);
            folded = true;
            break;
        }
        case ORUS_JIT_IR_OP_SUB_F64: {
            double value = dynasm_bits_to_f64(regs[lhs].bits) -
                           dynasm_bits_to_f64(regs[rhs].bits);
            bits = dynasm_f64_to_bits(value);
            folded = true;
            break;
        }
        case ORUS_JIT_IR_OP_MUL_F64: {
            double value = dynasm_bits_to_f64(regs[lhs].bits) *
                           dynasm_bits_to_f64(regs[rhs].bits);
            bits = dynasm_f64_to_bits(value);
            folded = true;
            break;
        }
        default:
            break;
    }

    if (!folded) {
        dynasm_register_state_invalidate(regs, dst);
        return false;
    }

    regs[dst].known = true;
    regs[dst].is_constant = true;
    regs[dst].bits = bits;
    regs[dst].kind = inst->value_kind;

    result->replaced = true;
    result->inst = *inst;
    result->inst.opcode = dynasm_load_opcode_for_kind(inst->value_kind);
    result->inst.value_kind = inst->value_kind;
    result->inst.operands.load_const.dst_reg = dst;
    result->inst.operands.load_const.constant_index = 0u;
    result->inst.operands.load_const.immediate_bits = bits;
    return true;
}

static inline void
dynasm_track_register(bool* seen, uint16_t* unique_count, uint16_t reg) {
    if (!seen || !unique_count) {
        return;
    }
    if (reg < REGISTER_COUNT && !seen[reg]) {
        seen[reg] = true;
        ++(*unique_count);
    }
}

static void
dynasm_collect_instruction_regs(const OrusJitIRInstruction* inst,
                                bool* seen,
                                uint16_t* unique_count,
                                uint16_t* invariant_defs) {
    if (!inst || !seen || !unique_count) {
        return;
    }
    bool invariant = (inst->optimization_flags & ORUS_JIT_IR_FLAG_LOOP_INVARIANT) != 0u;
    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST:
        case ORUS_JIT_IR_OP_LOAD_I64_CONST:
        case ORUS_JIT_IR_OP_LOAD_U32_CONST:
        case ORUS_JIT_IR_OP_LOAD_U64_CONST:
        case ORUS_JIT_IR_OP_LOAD_F64_CONST:
        case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
        case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.load_const.dst_reg);
            }
            break;
        case ORUS_JIT_IR_OP_MOVE_I32:
        case ORUS_JIT_IR_OP_MOVE_I64:
        case ORUS_JIT_IR_OP_MOVE_U32:
        case ORUS_JIT_IR_OP_MOVE_U64:
        case ORUS_JIT_IR_OP_MOVE_F64:
        case ORUS_JIT_IR_OP_MOVE_BOOL:
        case ORUS_JIT_IR_OP_MOVE_STRING:
        case ORUS_JIT_IR_OP_MOVE_VALUE:
            dynasm_track_register(seen, unique_count, inst->operands.move.src_reg);
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.move.dst_reg);
            }
            break;
        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
        case ORUS_JIT_IR_OP_DIV_I32:
        case ORUS_JIT_IR_OP_DIV_I64:
        case ORUS_JIT_IR_OP_DIV_U32:
        case ORUS_JIT_IR_OP_DIV_U64:
        case ORUS_JIT_IR_OP_DIV_F64:
        case ORUS_JIT_IR_OP_MOD_I32:
        case ORUS_JIT_IR_OP_MOD_I64:
        case ORUS_JIT_IR_OP_MOD_U32:
        case ORUS_JIT_IR_OP_MOD_U64:
        case ORUS_JIT_IR_OP_MOD_F64:
        case ORUS_JIT_IR_OP_LT_I32:
        case ORUS_JIT_IR_OP_LE_I32:
        case ORUS_JIT_IR_OP_GT_I32:
        case ORUS_JIT_IR_OP_GE_I32:
        case ORUS_JIT_IR_OP_LT_I64:
        case ORUS_JIT_IR_OP_LE_I64:
        case ORUS_JIT_IR_OP_GT_I64:
        case ORUS_JIT_IR_OP_GE_I64:
        case ORUS_JIT_IR_OP_LT_U32:
        case ORUS_JIT_IR_OP_LE_U32:
        case ORUS_JIT_IR_OP_GT_U32:
        case ORUS_JIT_IR_OP_GE_U32:
        case ORUS_JIT_IR_OP_LT_U64:
        case ORUS_JIT_IR_OP_LE_U64:
        case ORUS_JIT_IR_OP_GT_U64:
        case ORUS_JIT_IR_OP_GE_U64:
        case ORUS_JIT_IR_OP_LT_F64:
        case ORUS_JIT_IR_OP_LE_F64:
        case ORUS_JIT_IR_OP_GT_F64:
        case ORUS_JIT_IR_OP_GE_F64:
        case ORUS_JIT_IR_OP_EQ_I32:
        case ORUS_JIT_IR_OP_NE_I32:
        case ORUS_JIT_IR_OP_EQ_I64:
        case ORUS_JIT_IR_OP_NE_I64:
        case ORUS_JIT_IR_OP_EQ_U32:
        case ORUS_JIT_IR_OP_NE_U32:
        case ORUS_JIT_IR_OP_EQ_U64:
        case ORUS_JIT_IR_OP_NE_U64:
        case ORUS_JIT_IR_OP_EQ_F64:
        case ORUS_JIT_IR_OP_NE_F64:
        case ORUS_JIT_IR_OP_EQ_BOOL:
        case ORUS_JIT_IR_OP_NE_BOOL:
            dynasm_track_register(seen, unique_count, inst->operands.arithmetic.lhs_reg);
            dynasm_track_register(seen, unique_count, inst->operands.arithmetic.rhs_reg);
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.arithmetic.dst_reg);
            }
            break;
        case ORUS_JIT_IR_OP_I32_TO_I64:
        case ORUS_JIT_IR_OP_U32_TO_U64:
        case ORUS_JIT_IR_OP_U32_TO_I32:
        case ORUS_JIT_IR_OP_I32_TO_F64:
        case ORUS_JIT_IR_OP_I64_TO_F64:
        case ORUS_JIT_IR_OP_F64_TO_I32:
        case ORUS_JIT_IR_OP_F64_TO_I64:
        case ORUS_JIT_IR_OP_F64_TO_U32:
        case ORUS_JIT_IR_OP_U32_TO_F64:
        case ORUS_JIT_IR_OP_I32_TO_U32:
        case ORUS_JIT_IR_OP_I64_TO_U32:
        case ORUS_JIT_IR_OP_I32_TO_U64:
        case ORUS_JIT_IR_OP_I64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_I32:
        case ORUS_JIT_IR_OP_U64_TO_I64:
        case ORUS_JIT_IR_OP_U64_TO_U32:
        case ORUS_JIT_IR_OP_F64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_F64:
            dynasm_track_register(seen, unique_count, inst->operands.unary.src_reg);
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.unary.dst_reg);
            }
            break;
        case ORUS_JIT_IR_OP_TYPE_OF:
            dynasm_track_register(seen, unique_count, inst->operands.type_of.value_reg);
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.type_of.dst_reg);
            }
            break;
        case ORUS_JIT_IR_OP_IS_TYPE:
            dynasm_track_register(seen, unique_count, inst->operands.is_type.value_reg);
            dynasm_track_register(seen, unique_count, inst->operands.is_type.type_reg);
            if (invariant) {
                if (invariant_defs) {
                    ++(*invariant_defs);
                }
            } else {
                dynasm_track_register(seen, unique_count, inst->operands.is_type.dst_reg);
            }
            break;
        default:
            break;
    }
}

static DynAsmPeepholeResult
dynasm_optimize_instruction(DynAsmRegisterState* regs,
                            const OrusJitIRInstruction* inst) {
    DynAsmPeepholeResult result = {0};
    result.inst = *inst;
    if (!regs || !inst) {
        return result;
    }

    if (dynasm_opcode_is_numeric_load(inst->opcode)) {
        uint16_t dst = inst->operands.load_const.dst_reg;
        if (dst < REGISTER_COUNT) {
            if (regs[dst].known && regs[dst].is_constant &&
                regs[dst].bits == inst->operands.load_const.immediate_bits &&
                regs[dst].kind == inst->value_kind) {
                result.skip = true;
                return result;
            }
            regs[dst].known = true;
            regs[dst].is_constant = true;
            regs[dst].bits = inst->operands.load_const.immediate_bits;
            regs[dst].kind = inst->value_kind;
        }
        return result;
    }

    if (inst->opcode == ORUS_JIT_IR_OP_LOAD_STRING_CONST) {
        uint16_t dst = inst->operands.load_const.dst_reg;
        if (dst < REGISTER_COUNT) {
            if (regs[dst].known && regs[dst].is_constant &&
                regs[dst].bits == inst->operands.load_const.immediate_bits &&
                regs[dst].kind == inst->value_kind) {
                result.skip = true;
                return result;
            }
            regs[dst].known = true;
            regs[dst].is_constant = true;
            regs[dst].bits = inst->operands.load_const.immediate_bits;
            regs[dst].kind = inst->value_kind;
        }
        return result;
    }

    if (dynasm_opcode_is_move(inst->opcode)) {
        uint16_t dst = inst->operands.move.dst_reg;
        uint16_t src = inst->operands.move.src_reg;
        if (dst == src) {
            result.skip = true;
            return result;
        }
        if (dst < REGISTER_COUNT) {
            if (src < REGISTER_COUNT && regs[src].known && regs[dst].known &&
                regs[src].is_constant && regs[dst].is_constant &&
                regs[src].bits == regs[dst].bits &&
                regs[src].kind == regs[dst].kind) {
                result.skip = true;
                return result;
            }
            if (src < REGISTER_COUNT && regs[src].known) {
                regs[dst] = regs[src];
                regs[dst].kind = inst->value_kind;
            } else {
                dynasm_register_state_invalidate(regs, dst);
            }
        }
        return result;
    }

    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_CALL_NATIVE:
        case ORUS_JIT_IR_OP_CALL_FOREIGN:
        case ORUS_JIT_IR_OP_PRINT:
        case ORUS_JIT_IR_OP_ASSERT_EQ:
        case ORUS_JIT_IR_OP_RANGE:
        case ORUS_JIT_IR_OP_ENUM_NEW:
        case ORUS_JIT_IR_OP_GET_ITER:
        case ORUS_JIT_IR_OP_ITER_NEXT:
        case ORUS_JIT_IR_OP_CONCAT_STRING:
        case ORUS_JIT_IR_OP_TO_STRING:
        case ORUS_JIT_IR_OP_TYPE_OF:
        case ORUS_JIT_IR_OP_IS_TYPE:
        case ORUS_JIT_IR_OP_SAFEPOINT:
            dynasm_register_state_reset_all(regs);
            return result;
        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
            if (dynasm_try_fold_arithmetic(regs, inst, &result)) {
                return result;
            }
            return result;
        default:
            break;
    }

    uint16_t dst = 0u;
    if (dynasm_instruction_dest_reg(inst, &dst)) {
        dynasm_register_state_invalidate(regs, dst);
    }
    return result;
}

static bool
dynasm_schedule_program(const OrusJitIRProgram* program,
                        OrusJitIRInstruction** out_instructions,
                        size_t* out_count) {
    if (!out_instructions || !out_count) {
        return false;
    }
    *out_instructions = NULL;
    *out_count = 0u;

    if (!program || program->count == 0u) {
        return true;
    }

    uint8_t* block_starts = (uint8_t*)calloc(program->count, sizeof(uint8_t));
    if (!block_starts) {
        return false;
    }
    block_starts[0] = 1u;
    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        dynasm_mark_branch_target(program, inst, block_starts);
        if (dynasm_instruction_is_block_terminator(inst) && i + 1u < program->count) {
            block_starts[i + 1u] = 1u;
        }
    }

    size_t block_count = 0u;
    for (size_t i = 0; i < program->count; ++i) {
        if (block_starts[i]) {
            ++block_count;
        }
    }
    if (block_count == 0u) {
        free(block_starts);
        return true;
    }

    DynAsmBasicBlockInfo* blocks =
        (DynAsmBasicBlockInfo*)calloc(block_count, sizeof(DynAsmBasicBlockInfo));
    size_t* inst_to_block = (size_t*)calloc(program->count, sizeof(size_t));
    if (!blocks || !inst_to_block) {
        free(blocks);
        free(inst_to_block);
        free(block_starts);
        return false;
    }

    size_t current_block = 0u;
    for (size_t i = 0; i < program->count; ++i) {
        if (block_starts[i]) {
            if (current_block > 0u) {
                blocks[current_block - 1u].end = i;
            }
            blocks[current_block].start = i;
            blocks[current_block].weight = 1u;
            ++current_block;
        }
    }
    blocks[block_count - 1u].end = program->count;

    for (size_t b = 0; b < block_count; ++b) {
        for (size_t idx = blocks[b].start; idx < blocks[b].end; ++idx) {
            inst_to_block[idx] = b;
        }
        if (blocks[b].start < program->count) {
            uint32_t offset = program->instructions[blocks[b].start].bytecode_offset;
            if (offset >= program->loop_start_offset && offset < program->loop_end_offset) {
                blocks[b].weight = 2u;
            }
        }
        bool seen[REGISTER_COUNT] = {0};
        uint16_t unique = 0u;
        uint16_t invariant_defs = 0u;
        for (size_t idx = blocks[b].start; idx < blocks[b].end; ++idx) {
            dynasm_collect_instruction_regs(&program->instructions[idx], seen, &unique,
                                            &invariant_defs);
        }
        blocks[b].unique_reg_count = unique;
        blocks[b].invariant_defs = invariant_defs;
        if (blocks[b].invariant_defs > 0u) {
            blocks[b].weight += blocks[b].invariant_defs;
        }
    }

    for (size_t b = 0; b < block_count; ++b) {
        blocks[b].successor_count = 0u;
        if (blocks[b].end == 0u) {
            continue;
        }
        const OrusJitIRInstruction* last = &program->instructions[blocks[b].end - 1u];
        size_t fallthrough_block = SIZE_MAX;
        if (blocks[b].end < program->count) {
            fallthrough_block = inst_to_block[blocks[b].end];
        }
        switch (last->opcode) {
            case ORUS_JIT_IR_OP_JUMP_SHORT: {
                uint32_t fallthrough = last->bytecode_offset +
                                       last->operands.jump_short.bytecode_length;
                uint32_t target = fallthrough + last->operands.jump_short.offset;
                size_t target_index = orus_jit_program_find_index(program, target);
                if (target_index != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], inst_to_block[target_index]);
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: {
                uint32_t fallthrough = last->bytecode_offset + 2u;
                if (fallthrough >= last->operands.jump_back_short.back_offset) {
                    uint32_t target = fallthrough - last->operands.jump_back_short.back_offset;
                    size_t target_index = orus_jit_program_find_index(program, target);
                    if (target_index != SIZE_MAX) {
                        dynasm_add_successor(&blocks[b], inst_to_block[target_index]);
                    }
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: {
                uint32_t fallthrough = last->bytecode_offset +
                                       last->operands.jump_if_not_short.bytecode_length;
                uint32_t target = fallthrough + last->operands.jump_if_not_short.offset;
                size_t target_index = orus_jit_program_find_index(program, target);
                bool prefer_target = (target_index != SIZE_MAX) &&
                                     (target >= program->loop_start_offset &&
                                      target < program->loop_end_offset);
                if (!prefer_target && fallthrough_block != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], fallthrough_block);
                }
                if (target_index != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], inst_to_block[target_index]);
                }
                if (prefer_target && fallthrough_block != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], fallthrough_block);
                }
                break;
            }
            case ORUS_JIT_IR_OP_INC_CMP_JUMP:
            case ORUS_JIT_IR_OP_DEC_CMP_JUMP: {
                uint32_t fallthrough = last->bytecode_offset + 5u;
                int64_t projected = (int64_t)(int32_t)fallthrough +
                                    (int64_t)last->operands.fused_loop.jump_offset;
                if (projected >= 0 && projected <= (int64_t)UINT32_MAX) {
                    size_t target_index = orus_jit_program_find_index(program, (uint32_t)projected);
                    if (target_index != SIZE_MAX) {
                        dynasm_add_successor(&blocks[b], inst_to_block[target_index]);
                    }
                }
                if (fallthrough_block != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], fallthrough_block);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK:
            case ORUS_JIT_IR_OP_RETURN:
                break;
            default:
                if (fallthrough_block != SIZE_MAX) {
                    dynasm_add_successor(&blocks[b], fallthrough_block);
                }
                break;
        }
    }

    size_t* order = (size_t*)calloc(block_count, sizeof(size_t));
    bool* visited = (bool*)calloc(block_count, sizeof(bool));
    if (!order || !visited) {
        free(order);
        free(visited);
        free(blocks);
        free(inst_to_block);
        free(block_starts);
        return false;
    }

    size_t order_count = 0u;
    dynasm_schedule_dfs(blocks, block_count, 0u, visited, order, &order_count);
    for (size_t i = 0; i < block_count; ++i) {
        if (!visited[i]) {
            dynasm_schedule_dfs(blocks, block_count, i, visited, order, &order_count);
        }
    }

    OrusJitIRInstruction* scheduled =
        (OrusJitIRInstruction*)malloc(program->count * sizeof(OrusJitIRInstruction));
    if (!scheduled) {
        free(order);
        free(visited);
        free(blocks);
        free(inst_to_block);
        free(block_starts);
        return false;
    }

    DynAsmRegisterState reg_state[REGISTER_COUNT];
    dynasm_register_state_init(reg_state);
    size_t scheduled_count = 0u;
    for (size_t i = 0; i < order_count; ++i) {
        size_t block_index = order[i];
        if (block_index >= block_count) {
            continue;
        }
        for (size_t inst_index = blocks[block_index].start;
             inst_index < blocks[block_index].end;
             ++inst_index) {
            const OrusJitIRInstruction* inst = &program->instructions[inst_index];
            DynAsmPeepholeResult peephole = dynasm_optimize_instruction(reg_state, inst);
            if (peephole.skip) {
                continue;
            }
            scheduled[scheduled_count++] = peephole.inst;
        }
    }

    free(order);
    free(visited);
    free(blocks);
    free(inst_to_block);
    free(block_starts);

    if (scheduled_count == program->count) {
        dynasm_mark_vector_pairs(program,
                                 (OrusJitIRInstruction*)program->instructions,
                                 program->count);
        free(scheduled);
        return true;
    }

    dynasm_mark_vector_pairs(program, scheduled, scheduled_count);
    *out_instructions = scheduled;
    *out_count = scheduled_count;
    return true;
}

static bool
orus_jit_ir_emit_x86(const OrusJitIRProgram* program,
                     OrusJitNativeBlock* block,
                     struct DynAsmActionBuffer* actions) {
    if (!program || !block || !actions || program->count == 0u) {
        return false;
    }

    OrusJitIRInstruction* scheduled_instructions = NULL;
    size_t scheduled_count = 0u;
    const OrusJitIRInstruction* instruction_stream = program->instructions;
    size_t instruction_count = program->count;
    if (!dynasm_schedule_program(program, &scheduled_instructions, &scheduled_count)) {
        return false;
    }
    if (scheduled_instructions) {
        instruction_stream = scheduled_instructions;
        instruction_count = scheduled_count;
    }

    size_t code_offset = 0u;
    DynAsmPatchList bail_patches;
    DynAsmPatchList epilogue_patches;
    dynasm_patch_list_init(&bail_patches);
    dynasm_patch_list_init(&epilogue_patches);

#define DYNASM_EMIT_FAIL()                                                           \
    do {                                                                             \
        dynasm_patch_list_release(&bail_patches);                                    \
        dynasm_patch_list_release(&epilogue_patches);                                \
        if (scheduled_instructions) {                                                \
            free(scheduled_instructions);                                            \
        }                                                                            \
        return false;                                                                \
    } while (0)

    bool success = dynasm_emit_bytes_track(actions, &code_offset, (const uint8_t[]){0x55}, 1u) &&
                   dynasm_emit_bytes_track(actions, &code_offset,
                                          (const uint8_t[]){0x48, 0x89, 0xE5}, 3u) &&
                   dynasm_emit_bytes_track(actions, &code_offset,
                                          (const uint8_t[]){0x48, 0x83, 0xEC, 0x28}, 4u) &&
                   dynasm_emit_bytes_track(actions, &code_offset,
                                          (const uint8_t[]){0x48, 0x89, 0x7D, 0xF8}, 4u) &&
                   dynasm_emit_bytes_track(actions, &code_offset,
                                          (const uint8_t[]){0x48, 0xB8}, 2u) &&
                   dynasm_emit_u64_le(actions, &code_offset,
                                      (uint64_t)(uintptr_t)block) &&
                   dynasm_emit_bytes_track(actions, &code_offset,
                                          (const uint8_t[]){0x48, 0x89, 0x45, 0xF0}, 4u);
    if (!success) {
        DYNASM_EMIT_FAIL();
    }

    for (size_t i = 0; i < instruction_count; ++i) {
        const OrusJitIRInstruction* inst = &instruction_stream[i];

        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_LOAD_F64_CONST: {
                uint64_t bits = inst->operands.load_const.immediate_bits;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->value_kind) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.load_const.dst_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x49, 0xB8}, 2u) ||
                    !dynasm_emit_u64_le(actions, &code_offset, bits) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_linear_load,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST: {
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.load_const.dst_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0xB9}, 2u) ||
                    !dynasm_emit_u64_le(actions, &code_offset,
                                        inst->operands.load_const.immediate_bits) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_load_string_const,
                                              ORUS_JIT_HELPER_STUB_KIND_STRING) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST: {
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.load_const.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.load_const.constant_index) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->value_kind) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_load_value_const,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_I32:
            case ORUS_JIT_IR_OP_MOVE_I64:
            case ORUS_JIT_IR_OP_MOVE_U32:
            case ORUS_JIT_IR_OP_MOVE_U64:
            case ORUS_JIT_IR_OP_MOVE_F64:
            case ORUS_JIT_IR_OP_MOVE_BOOL: {
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->value_kind) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.move.dst_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.move.src_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_linear_move,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_STRING: {
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.move.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.move.src_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_move_string,
                                              ORUS_JIT_HELPER_STUB_KIND_STRING) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_VALUE: {
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.move.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.move.src_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_move_value,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_MUL_I32:
            case ORUS_JIT_IR_OP_DIV_I32:
            case ORUS_JIT_IR_OP_MOD_I32:
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64:
            case ORUS_JIT_IR_OP_DIV_I64:
            case ORUS_JIT_IR_OP_MOD_I64:
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_MUL_U32:
            case ORUS_JIT_IR_OP_DIV_U32:
            case ORUS_JIT_IR_OP_MOD_U32:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64:
            case ORUS_JIT_IR_OP_DIV_U64:
            case ORUS_JIT_IR_OP_MOD_U64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64:
            case ORUS_JIT_IR_OP_DIV_F64:
            case ORUS_JIT_IR_OP_MOD_F64: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->opcode) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->value_kind) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB9}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !dynasm_emit_stack_push_u32(actions, &code_offset,
                                                 (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_linear_arithmetic,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_stack_pop(actions, &code_offset)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->opcode) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB9}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_compare_op,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_CONCAT_STRING: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.arithmetic.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.arithmetic.lhs_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.arithmetic.rhs_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_concat_string,
                                              ORUS_JIT_HELPER_STUB_KIND_STRING) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_TO_STRING: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.unary.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.unary.src_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_to_string,
                                              ORUS_JIT_HELPER_STUB_KIND_STRING) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_TYPE_OF: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.type_of.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.type_of.value_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_type_of,
                                              ORUS_JIT_HELPER_STUB_KIND_STRING) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_IS_TYPE: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.is_type.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.is_type.value_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.is_type.type_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_is_type,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x84, 0xC0}, 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                size_t jcc_action = actions->size;
                size_t jcc_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x0F, 0x84, 0x00, 0x00, 0x00, 0x00},
                                             6u) ||
                    !dynasm_patch_list_append(&bail_patches, jcc_action + 4u,
                                              jcc_code + 2u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_TIME_STAMP: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.time_stamp.dst_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_time_stamp,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_MAKE_ARRAY: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0xBA}, 2u) ||
                    !dynasm_emit_u64_le(actions, &code_offset,
                                        (uint64_t)(uintptr_t)inst) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_make_array,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_PUSH: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.array_push.array_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.array_push.value_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_array_push,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_POP: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.array_pop.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.array_pop.array_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_array_pop,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ENUM_NEW: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x48, 0xBA}, 2u) ||
                    !dynasm_emit_u64_le(actions, &code_offset,
                                        (uint64_t)(uintptr_t)inst) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_enum_new,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_PRINT: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.print.first_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.print.arg_count) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.print.newline) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_print,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ASSERT_EQ: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.assert_eq.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.assert_eq.label_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.assert_eq.actual_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB9}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.assert_eq.expected_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_assert_eq,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_CALL_NATIVE:
            case ORUS_JIT_IR_OP_CALL_FOREIGN: {
                if (inst->operands.call_native.spill_count > 0u) {
                    if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                        !dynasm_emit_mov_reg_imm32(actions, 0xBE, &code_offset,
                                                   (uint32_t)inst->operands.call_native.spill_base) ||
                        !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                                   (uint32_t)inst->operands.call_native.spill_count) ||
                        !dynasm_emit_helper_call(actions, &code_offset,
                                                  (const void*)&orus_jit_native_flush_typed_range,
                                                  ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                        DYNASM_EMIT_FAIL();
                    }
                }
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.call_native.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.call_native.first_arg_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.call_native.arg_count) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB9}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.call_native.native_index) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)(inst->opcode ==
                                                                   ORUS_JIT_IR_OP_CALL_FOREIGN
                                                               ? &orus_jit_native_call_foreign
                                                               : &orus_jit_native_call_native),
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_GET_ITER: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.get_iter.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.get_iter.iterable_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_get_iter,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_ITER_NEXT: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.iter_next.value_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.iter_next.iterator_reg) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x41, 0xB8}, 2u) ||
                    !dynasm_emit_u32_le(actions, &code_offset,
                                        (uint32_t)inst->operands.iter_next.has_value_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_iter_next,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_RANGE: {
                const uint16_t* args = inst->operands.range.arg_regs;
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.range.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.range.arg_count) ||
                    !dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0x49, 0xB8}, 2u) ||
                    !dynasm_emit_u64_le(actions, &code_offset,
                                        (uint64_t)(uintptr_t)args) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_range,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_I32_TO_I64:
            case ORUS_JIT_IR_OP_U32_TO_U64:
            case ORUS_JIT_IR_OP_U32_TO_I32:
            case ORUS_JIT_IR_OP_I32_TO_F64:
            case ORUS_JIT_IR_OP_I64_TO_F64:
            case ORUS_JIT_IR_OP_F64_TO_I32:
            case ORUS_JIT_IR_OP_F64_TO_I64:
            case ORUS_JIT_IR_OP_F64_TO_U32:
            case ORUS_JIT_IR_OP_U32_TO_F64:
            case ORUS_JIT_IR_OP_I32_TO_U32:
            case ORUS_JIT_IR_OP_I64_TO_U32:
            case ORUS_JIT_IR_OP_I32_TO_U64:
            case ORUS_JIT_IR_OP_I64_TO_U64:
            case ORUS_JIT_IR_OP_U64_TO_I32:
            case ORUS_JIT_IR_OP_U64_TO_I64:
            case ORUS_JIT_IR_OP_U64_TO_U32:
            case ORUS_JIT_IR_OP_F64_TO_U64:
            case ORUS_JIT_IR_OP_U64_TO_F64: {
                const void* helper = NULL;
                switch (inst->opcode) {
                    case ORUS_JIT_IR_OP_I32_TO_I64:
                        helper = (const void*)&orus_jit_native_convert_i32_to_i64;
                        break;
                    case ORUS_JIT_IR_OP_U32_TO_U64:
                        helper = (const void*)&orus_jit_native_convert_u32_to_u64;
                        break;
                    case ORUS_JIT_IR_OP_U32_TO_I32:
                        helper = (const void*)&orus_jit_native_convert_u32_to_i32;
                        break;
                    case ORUS_JIT_IR_OP_I32_TO_F64:
                        helper = (const void*)&orus_jit_native_convert_i32_to_f64;
                        break;
                    case ORUS_JIT_IR_OP_I64_TO_F64:
                        helper = (const void*)&orus_jit_native_convert_i64_to_f64;
                        break;
                    case ORUS_JIT_IR_OP_F64_TO_I32:
                        helper = (const void*)&orus_jit_native_convert_f64_to_i32;
                        break;
                    case ORUS_JIT_IR_OP_F64_TO_I64:
                        helper = (const void*)&orus_jit_native_convert_f64_to_i64;
                        break;
                    case ORUS_JIT_IR_OP_F64_TO_U32:
                        helper = (const void*)&orus_jit_native_convert_f64_to_u32;
                        break;
                    case ORUS_JIT_IR_OP_U32_TO_F64:
                        helper = (const void*)&orus_jit_native_convert_u32_to_f64;
                        break;
                    case ORUS_JIT_IR_OP_I32_TO_U32:
                        helper = (const void*)&orus_jit_native_convert_i32_to_u32;
                        break;
                    case ORUS_JIT_IR_OP_I64_TO_U32:
                        helper = (const void*)&orus_jit_native_convert_i64_to_u32;
                        break;
                    case ORUS_JIT_IR_OP_I32_TO_U64:
                        helper = (const void*)&orus_jit_native_convert_i32_to_u64;
                        break;
                    case ORUS_JIT_IR_OP_I64_TO_U64:
                        helper = (const void*)&orus_jit_native_convert_i64_to_u64;
                        break;
                    case ORUS_JIT_IR_OP_U64_TO_I32:
                        helper = (const void*)&orus_jit_native_convert_u64_to_i32;
                        break;
                    case ORUS_JIT_IR_OP_U64_TO_I64:
                        helper = (const void*)&orus_jit_native_convert_u64_to_i64;
                        break;
                    case ORUS_JIT_IR_OP_U64_TO_U32:
                        helper = (const void*)&orus_jit_native_convert_u64_to_u32;
                        break;
                    case ORUS_JIT_IR_OP_F64_TO_U64:
                        helper = (const void*)&orus_jit_native_convert_f64_to_u64;
                        break;
                    case ORUS_JIT_IR_OP_U64_TO_F64:
                        helper = (const void*)&orus_jit_native_convert_u64_to_f64;
                        break;
                    default:
                        break;
                }
                if (!helper || !dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xBA, &code_offset,
                                               (uint32_t)inst->operands.unary.dst_reg) ||
                    !dynasm_emit_mov_reg_imm32(actions, 0xB9, &code_offset,
                                               (uint32_t)inst->operands.unary.src_reg) ||
                    !dynasm_emit_helper_call(actions, &code_offset, helper,
                                              ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_SAFEPOINT: {
                if (!dynasm_emit_load_vm_block(actions, &code_offset) ||
                    !dynasm_emit_helper_call(actions, &code_offset,
                                              (const void*)&orus_jit_native_linear_safepoint,
                                              ORUS_JIT_HELPER_STUB_KIND_GC)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            case ORUS_JIT_IR_OP_RETURN: {
                size_t jmp_action = actions->size;
                size_t jmp_code = code_offset;
                if (!dynasm_emit_bytes_track(actions, &code_offset,
                                             (const uint8_t[]){0xE9, 0x00, 0x00, 0x00, 0x00}, 5u) ||
                    !dynasm_patch_list_append(&epilogue_patches, jmp_action + 2u,
                                              jmp_code + 1u)) {
                    DYNASM_EMIT_FAIL();
                }
                break;
            }
            default:
                DYNASM_EMIT_FAIL();
        }
    }

    size_t jmp_action = actions->size;
    size_t jmp_code = code_offset;
    if (!dynasm_emit_bytes_track(actions, &code_offset,
                                 (const uint8_t[]){0xE9, 0x00, 0x00, 0x00, 0x00}, 5u) ||
        !dynasm_patch_list_append(&epilogue_patches, jmp_action + 2u,
                                  jmp_code + 1u)) {
        DYNASM_EMIT_FAIL();
    }

    size_t bail_code_offset = code_offset;
    if (!dynasm_emit_bytes_track(actions, &code_offset,
                                 (const uint8_t[]){0x48, 0x8B, 0x7D, 0xF8}, 4u) ||
        !dynasm_emit_bytes_track(actions, &code_offset,
                                 (const uint8_t[]){0x48, 0x8B, 0x75, 0xF0}, 4u) ||
        !dynasm_emit_helper_call(actions, &code_offset,
                                 (const void*)&orus_jit_native_type_bailout,
                                 ORUS_JIT_HELPER_STUB_KIND_RUNTIME)) {
        DYNASM_EMIT_FAIL();
    }

    size_t epilogue_code_offset = code_offset;
    if (!dynasm_emit_bytes_track(actions, &code_offset,
                                 (const uint8_t[]){0xC9, 0xC3}, 2u)) {
        DYNASM_EMIT_FAIL();
    }

    for (size_t i = 0; i < bail_patches.count; ++i) {
        DynAsmPatch* patch = &bail_patches.data[i];
        int64_t rel = (int64_t)bail_code_offset - ((int64_t)patch->code_offset + 4);
        dynasm_patch_u32(actions, patch->action_offset, (uint32_t)rel);
    }

    for (size_t i = 0; i < epilogue_patches.count; ++i) {
        DynAsmPatch* patch = &epilogue_patches.data[i];
        int64_t rel = (int64_t)epilogue_code_offset - ((int64_t)patch->code_offset + 4);
        dynasm_patch_u32(actions, patch->action_offset, (uint32_t)rel);
    }

    dynasm_patch_list_release(&bail_patches);
    dynasm_patch_list_release(&epilogue_patches);

    if (!dynasm_action_buffer_push(actions, DASM_STOP)) {
        DYNASM_EMIT_FAIL();
    }

    if (scheduled_instructions) {
        free(scheduled_instructions);
    }

#undef DYNASM_EMIT_FAIL

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

    OrusJitNativeBlock* block = orus_jit_native_block_create(program);
    if (!block) {
        dynasm_action_buffer_release(&actions);
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    if (!orus_jit_ir_emit_x86(program, block, &actions)) {
        dynasm_action_buffer_release(&actions);
        orus_jit_native_block_destroy(block);
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
        LOG_ERROR("[JIT] DynASM link failed with status 0x%08X", status);
        dasm_free(&dasm);
        dynasm_action_buffer_release(&actions);
        orus_jit_native_block_destroy(block);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    size_t capacity = 0;
    size_t page_size = backend->page_size ? backend->page_size : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(encoded_size, page_size, &capacity);
    if (!buffer) {
        dasm_free(&dasm);
        dynasm_action_buffer_release(&actions);
        orus_jit_native_block_destroy(block);
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    status = dasm_encode(&dasm, buffer);
    dasm_free(&dasm);
    dynasm_action_buffer_release(&actions);

    if (status != DASM_S_OK) {
        LOG_ERROR("[JIT] DynASM encode failed with status 0x%08X", status);
        orus_jit_release_executable(buffer, capacity);
        orus_jit_native_block_destroy(block);
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
    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->debug_name = "orus_jit_ir_stub";

    block->code_ptr = buffer;
    block->code_capacity = capacity;
    orus_jit_debug_publish_disassembly(&block->program,
                                       ORUS_JIT_BACKEND_TARGET_X86_64,
                                       buffer,
                                       encoded_size);
    orus_jit_native_block_register(block);

    return JIT_BACKEND_OK;
}
#endif // ORUS_JIT_HAS_DYNASM_X86

#if defined(__aarch64__)
typedef struct {
    uint32_t* data;
    size_t count;
    size_t capacity;
} OrusJitA64CodeBuffer;

typedef struct {
    size_t* data;
    size_t count;
    size_t capacity;
} OrusJitA64PatchList;

typedef enum {
    ORUS_JIT_A64_BRANCH_PATCH_KIND_B = 0,
    ORUS_JIT_A64_BRANCH_PATCH_KIND_CBNZ,
} OrusJitA64BranchPatchKind;

typedef struct {
    size_t code_index;
    uint32_t target_bytecode;
    OrusJitA64BranchPatchKind kind;
} OrusJitA64BranchPatch;

typedef struct {
    OrusJitA64BranchPatch* data;
    size_t count;
    size_t capacity;
} OrusJitA64BranchPatchList;

static void
orus_jit_a64_code_buffer_init(OrusJitA64CodeBuffer* buffer) {
    if (!buffer) {
        return;
    }
    buffer->data = NULL;
    buffer->count = 0u;
    buffer->capacity = 0u;
}

static void
orus_jit_a64_code_buffer_release(OrusJitA64CodeBuffer* buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->count = 0u;
    buffer->capacity = 0u;
}

static bool
orus_jit_a64_code_buffer_reserve(OrusJitA64CodeBuffer* buffer, size_t additional) {
    if (!buffer) {
        return false;
    }
    if (additional > SIZE_MAX - buffer->count) {
        return false;
    }
    size_t required = buffer->count + additional;
    if (required <= buffer->capacity) {
        return true;
    }
    size_t new_capacity = buffer->capacity ? buffer->capacity : 32u;
    while (new_capacity < required) {
        if (new_capacity > SIZE_MAX / 2u) {
            new_capacity = required;
            break;
        }
        new_capacity *= 2u;
    }
    uint32_t* data = (uint32_t*)realloc(buffer->data, new_capacity * sizeof(uint32_t));
    if (!data) {
        return false;
    }
    buffer->data = data;
    buffer->capacity = new_capacity;
    return true;
}

static bool
orus_jit_a64_code_buffer_emit_u32(OrusJitA64CodeBuffer* buffer, uint32_t value) {
    if (!orus_jit_a64_code_buffer_reserve(buffer, 1u)) {
        return false;
    }
    buffer->data[buffer->count++] = value;
    return true;
}

static void
orus_jit_a64_patch_list_init(OrusJitA64PatchList* list) {
    if (!list) {
        return;
    }
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void
orus_jit_a64_patch_list_release(OrusJitA64PatchList* list) {
    if (!list) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
orus_jit_a64_patch_list_append(OrusJitA64PatchList* list, size_t value) {
    if (!list) {
        return false;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 8u;
        size_t* data = (size_t*)realloc(list->data, new_capacity * sizeof(size_t));
        if (!data) {
            return false;
        }
        list->data = data;
        list->capacity = new_capacity;
    }
    list->data[list->count++] = value;
    return true;
}

static void
orus_jit_a64_branch_patch_list_init(OrusJitA64BranchPatchList* list) {
    if (!list) {
        return;
    }
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static void
orus_jit_a64_branch_patch_list_release(OrusJitA64BranchPatchList* list) {
    if (!list) {
        return;
    }
    free(list->data);
    list->data = NULL;
    list->count = 0u;
    list->capacity = 0u;
}

static bool
orus_jit_a64_branch_patch_list_append(OrusJitA64BranchPatchList* list,
                                      size_t code_index,
                                      uint32_t target_bytecode,
                                      OrusJitA64BranchPatchKind kind) {
    if (!list) {
        return false;
    }
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2u : 4u;
        OrusJitA64BranchPatch* data = (OrusJitA64BranchPatch*)realloc(
            list->data, new_capacity * sizeof(OrusJitA64BranchPatch));
        if (!data) {
            return false;
        }
        list->data = data;
        list->capacity = new_capacity;
    }
    list->data[list->count].code_index = code_index;
    list->data[list->count].target_bytecode = target_bytecode;
    list->data[list->count].kind = kind;
    list->count++;
    return true;
}

static bool
orus_jit_a64_patch_list_append_branch(OrusJitA64BranchPatchList* list,
                                      OrusJitA64BranchPatchKind kind,
                                      size_t code_index,
                                      uint32_t target_bytecode) {
    return orus_jit_a64_branch_patch_list_append(
        list, code_index, target_bytecode, kind);
}

static bool
orus_jit_a64_emit_mov_imm64_buffer(OrusJitA64CodeBuffer* buffer,
                                    uint8_t reg,
                                    uint64_t value) {
    if (!buffer) {
        return false;
    }
    for (;;) {
        size_t index = buffer->count;
        if (orus_jit_emit_a64_mov_imm64(buffer->data, &index, buffer->capacity,
                                        reg, value)) {
            buffer->count = index;
            return true;
        }
        if (!orus_jit_a64_code_buffer_reserve(buffer,
                                              buffer->capacity ? buffer->capacity : 4u)) {
            return false;
        }
    }
}

#define A64_MOV_REG(dst, src) \
    (0xAA0003E0u | (((uint32_t)(src) & 0x1Fu) << 16) | ((uint32_t)(dst) & 0x1Fu))
#define A64_STR_X(rt, rn, imm) \
    (0xF9000000u | ((((uint32_t)(imm)) & 0xFFFu) << 10) | (((uint32_t)(rn) & 0x1Fu) << 5) | ((uint32_t)(rt) & 0x1Fu))
#define A64_LDR_X(rt, rn, imm) \
    (0xF9400000u | ((((uint32_t)(imm)) & 0xFFFu) << 10) | (((uint32_t)(rn) & 0x1Fu) << 5) | ((uint32_t)(rt) & 0x1Fu))
#define A64_CBZ_W(rt, imm19) \
    (0x34000000u | ((((uint32_t)(imm19)) & 0x7FFFFu) << 5) | ((uint32_t)(rt) & 0x1Fu))
#define A64_CBNZ_W(rt, imm19) \
    (0x35000000u | ((((uint32_t)(imm19)) & 0x7FFFFu) << 5) | ((uint32_t)(rt) & 0x1Fu))
#define A64_B_COND(cond, imm19) \
    (0x54000000u | ((((uint32_t)(imm19)) & 0x7FFFFu) << 5) | ((uint32_t)(cond) & 0x0Fu))
#define A64_B(imm26) (0x14000000u | ((uint32_t)(imm26) & 0x03FFFFFFu))

static JITBackendStatus
orus_jit_backend_emit_linear_a64(struct OrusJitBackend* backend,
                                 OrusJitNativeBlock* block,
                                 JITEntry* entry) {
    if (!backend || !block || !entry || block->program.count == 0u) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    for (size_t i = 0; i < block->program.count; ++i) {
        const OrusJitIRInstruction* inst = &block->program.instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_MOVE_I32:
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_MUL_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_MOVE_I64:
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_MOVE_U32:
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_MUL_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_MOVE_U64:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            case ORUS_JIT_IR_OP_MOVE_F64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_MOVE_BOOL:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL:
            case ORUS_JIT_IR_OP_IS_TYPE:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
            case ORUS_JIT_IR_OP_MOVE_STRING:
            case ORUS_JIT_IR_OP_CONCAT_STRING:
            case ORUS_JIT_IR_OP_TO_STRING:
            case ORUS_JIT_IR_OP_TYPE_OF:
                if (inst->value_kind != ORUS_JIT_VALUE_STRING) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
                if (inst->value_kind >= ORUS_JIT_VALUE_KIND_COUNT) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_TIME_STAMP:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_ASSERT_EQ:
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U32_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I32_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_I64_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_I32:
                if (inst->value_kind != ORUS_JIT_VALUE_I32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_I64:
                if (inst->value_kind != ORUS_JIT_VALUE_I64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_U32:
                if (inst->value_kind != ORUS_JIT_VALUE_U32) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_F64_TO_U64:
                if (inst->value_kind != ORUS_JIT_VALUE_U64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_U64_TO_F64:
                if (inst->value_kind != ORUS_JIT_VALUE_F64) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_MAKE_ARRAY:
            case ORUS_JIT_IR_OP_ARRAY_POP:
            case ORUS_JIT_IR_OP_ENUM_NEW:
                if (inst->value_kind != ORUS_JIT_VALUE_BOXED) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_SAFEPOINT:
            case ORUS_JIT_IR_OP_LOOP_BACK:
            case ORUS_JIT_IR_OP_RETURN:
            case ORUS_JIT_IR_OP_JUMP_SHORT:
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
            case ORUS_JIT_IR_OP_ARRAY_PUSH:
            case ORUS_JIT_IR_OP_PRINT:
            case ORUS_JIT_IR_OP_CALL_NATIVE:
            case ORUS_JIT_IR_OP_CALL_FOREIGN:
                break;
            default:
                return JIT_BACKEND_ASSEMBLY_ERROR;
        }
    }

    OrusJitA64CodeBuffer code;
    orus_jit_a64_code_buffer_init(&code);
    OrusJitA64PatchList bail_patches;
    orus_jit_a64_patch_list_init(&bail_patches);
    OrusJitA64PatchList return_patches;
    orus_jit_a64_patch_list_init(&return_patches);
    OrusJitA64BranchPatchList branch_patches;
    orus_jit_a64_branch_patch_list_init(&branch_patches);
    size_t* inst_offsets = NULL;

#define A64_RETURN(status)                                                        \
    do {                                                                         \
        orus_jit_a64_code_buffer_release(&code);                                  \
        orus_jit_a64_patch_list_release(&bail_patches);                           \
        orus_jit_a64_patch_list_release(&return_patches);                         \
        orus_jit_a64_branch_patch_list_release(&branch_patches);                  \
        free(inst_offsets);                                                       \
        return (status);                                                         \
    } while (0)

    if (block->program.count > 0u) {
        inst_offsets = (size_t*)calloc(block->program.count, sizeof(size_t));
        if (!inst_offsets) {
            A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
        }
        for (size_t i = 0; i < block->program.count; ++i) {
            inst_offsets[i] = SIZE_MAX;
        }
    }

    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xA9BF7BF0u) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0x910003FDu) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD100C3FFu) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xF90003E0u)) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (!orus_jit_a64_emit_mov_imm64_buffer(&code, 1u,
                                            (uint64_t)(uintptr_t)block) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xF90007E1u)) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    size_t loop_entry_index = code.count;
    bool loop_back_patch_pending = false;

    for (size_t i = 0; i < block->program.count; ++i) {
        const OrusJitIRInstruction* inst = &block->program.instructions[i];
        if (inst_offsets) {
            inst_offsets[i] = code.count;
        }

        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                        (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, inst->operands.load_const.immediate_bits) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_load)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOAD_VALUE_CONST: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                        (uint64_t)inst->operands.load_const.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 3u,
                                                        (uint64_t)inst->operands.load_const.constant_index) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 4u,
                                                        (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_load_value_const)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_I32:
            case ORUS_JIT_IR_OP_MOVE_I64:
            case ORUS_JIT_IR_OP_MOVE_U32:
            case ORUS_JIT_IR_OP_MOVE_U64:
            case ORUS_JIT_IR_OP_MOVE_F64:
            case ORUS_JIT_IR_OP_MOVE_BOOL: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                        (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.move.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.move.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_move)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_STRING: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.move.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.move.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_move_string)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MOVE_VALUE: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.move.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.move.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_move_value)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ADD_I32:
            case ORUS_JIT_IR_OP_SUB_I32:
            case ORUS_JIT_IR_OP_MUL_I32:
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64:
            case ORUS_JIT_IR_OP_ADD_U32:
            case ORUS_JIT_IR_OP_SUB_U32:
            case ORUS_JIT_IR_OP_MUL_U32:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->opcode) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u, (uint64_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 6u, (uint64_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_arithmetic)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LT_I32:
            case ORUS_JIT_IR_OP_LE_I32:
            case ORUS_JIT_IR_OP_GT_I32:
            case ORUS_JIT_IR_OP_GE_I32:
            case ORUS_JIT_IR_OP_EQ_I32:
            case ORUS_JIT_IR_OP_NE_I32:
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U32:
            case ORUS_JIT_IR_OP_LE_U32:
            case ORUS_JIT_IR_OP_GT_U32:
            case ORUS_JIT_IR_OP_GE_U32:
            case ORUS_JIT_IR_OP_EQ_U32:
            case ORUS_JIT_IR_OP_NE_U32:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64:
            case ORUS_JIT_IR_OP_EQ_BOOL:
            case ORUS_JIT_IR_OP_NE_BOOL: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->opcode) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u, (uint64_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_compare_op)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CONCAT_STRING: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_concat_string)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_SHORT: {
                size_t branch_index = code.count;
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_B(0u)) ||
                    !orus_jit_a64_branch_patch_list_append(
                        &branch_patches, branch_index,
                        inst->bytecode_offset +
                            inst->operands.jump_short.bytecode_length +
                            inst->operands.jump_short.offset,
                        ORUS_JIT_A64_BRANCH_PATCH_KIND_B)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT: {
                size_t branch_index = code.count;
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_B(0u))) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                uint32_t fallthrough = inst->bytecode_offset + 2u;
                uint16_t back = inst->operands.jump_back_short.back_offset;
                if (fallthrough < back || !inst_offsets) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                uint32_t target = fallthrough - back;
                size_t target_index =
                    orus_jit_program_find_index(&block->program, target);
                if (target_index == SIZE_MAX) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                size_t target_code_index = inst_offsets[target_index];
                if (target_code_index == SIZE_MAX) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                int64_t diff = (int64_t)target_code_index -
                               (int64_t)(branch_index + 1u);
                if (diff < -(1 << 25) || diff > ((1 << 25) - 1)) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                code.data[branch_index] =
                    A64_B((uint32_t)(diff & 0x03FFFFFFu));
                break;
            }
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u,
                        (uint64_t)inst->operands.jump_if_not_short.predicate_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        (uint64_t)(uintptr_t)
                            &orus_jit_native_evaluate_branch_false) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0x3100041Fu) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code,
                                                       A64_B_COND(0x0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches,
                                                    code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }

                size_t branch_index = code.count;
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_CBNZ_W(0u, 0u)) ||
                    !orus_jit_a64_branch_patch_list_append(
                        &branch_patches, branch_index,
                        inst->bytecode_offset +
                            inst->operands.jump_if_not_short.bytecode_length +
                            inst->operands.jump_if_not_short.offset,
                        ORUS_JIT_A64_BRANCH_PATCH_KIND_CBNZ)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_INC_CMP_JUMP:
            case ORUS_JIT_IR_OP_DEC_CMP_JUMP: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.fused_loop.counter_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u,
                        (uint64_t)inst->operands.fused_loop.limit_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u,
                        (uint64_t)(int64_t)inst->operands.fused_loop.step) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 6u,
                        (uint64_t)inst->operands.fused_loop.compare_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 7u,
                        (uint64_t)(int64_t)((inst->opcode ==
                                             ORUS_JIT_IR_OP_INC_CMP_JUMP)
                                                ? 1
                                                : -1)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_fused_loop_step)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0x3100041Fu) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_B_COND(0x0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches,
                                                    code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }

                uint32_t fallthrough = inst->bytecode_offset + 5u;
                int64_t target_bytecode =
                    (int64_t)(int32_t)fallthrough +
                    (int64_t)inst->operands.fused_loop.jump_offset;
                if (target_bytecode < 0 ||
                    target_bytecode > (int64_t)UINT32_MAX) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }

                size_t branch_index = code.count;
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_CBNZ_W(0u, 0u)) ||
                    !orus_jit_a64_branch_patch_list_append(
                        &branch_patches, branch_index,
                        (uint32_t)target_bytecode,
                        ORUS_JIT_A64_BRANCH_PATCH_KIND_CBNZ)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TO_STRING: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_to_string)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TYPE_OF: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.type_of.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.type_of.value_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_type_of)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_IS_TYPE: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.is_type.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.is_type.value_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.is_type.type_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_is_type)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_TIME_STAMP: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.time_stamp.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_time_stamp)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_MAKE_ARRAY: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)(uintptr_t)inst) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_make_array)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_PUSH: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.array_push.array_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.array_push.value_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_array_push)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ARRAY_POP: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.array_pop.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.array_pop.array_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_array_pop)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ENUM_NEW: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)(uintptr_t)inst) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_enum_new)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_PRINT: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.print.first_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.print.arg_count) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.print.newline) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_print)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ASSERT_EQ: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.assert_eq.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.assert_eq.label_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.assert_eq.actual_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u, (uint64_t)inst->operands.assert_eq.expected_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_assert_eq)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CALL_NATIVE:
            case ORUS_JIT_IR_OP_CALL_FOREIGN: {
                if (inst->operands.call_native.spill_count > 0u) {
                    if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                        !orus_jit_a64_emit_mov_imm64_buffer(&code, 1u,
                                                            (uint64_t)inst->operands.call_native.spill_base) ||
                        !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                            (uint64_t)inst->operands.call_native.spill_count) ||
                        !orus_jit_a64_emit_mov_imm64_buffer(
                            &code, 16u,
                            orus_jit_function_ptr_bits(&orus_jit_native_flush_typed_range)) ||
                        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u)) {
                        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                    }
                }
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.call_native.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.call_native.first_arg_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)inst->operands.call_native.arg_count) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u,
                        (uint64_t)inst->operands.call_native.native_index) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        (uint64_t)(uintptr_t)(inst->opcode ==
                                                     ORUS_JIT_IR_OP_CALL_FOREIGN
                                                 ? &orus_jit_native_call_foreign
                                                 : &orus_jit_native_call_native)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_GET_ITER: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u,
                        (uint64_t)inst->operands.get_iter.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.get_iter.iterable_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_get_iter)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_ITER_NEXT: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u,
                        (uint64_t)inst->operands.iter_next.value_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.iter_next.iterator_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u,
                        (uint64_t)inst->operands.iter_next.has_value_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_iter_next)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_RANGE: {
                const uint16_t* args = inst->operands.range.arg_regs;
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u,
                        (uint64_t)inst->operands.range.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.range.arg_count) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u, (uint64_t)(uintptr_t)args) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_range)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I32_TO_I64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i32_to_i64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_U64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_u64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_I32: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_i32)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I32_TO_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i32_to_f64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_I64_TO_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_i64_to_f64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_I32: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_i32)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_I64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_i64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_F64_TO_U32: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_f64_to_u32)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_U32_TO_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(1u, 31u, 1u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 2u, (uint64_t)inst->operands.unary.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u, (uint64_t)inst->operands.unary.src_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_convert_u32_to_f64)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_SAFEPOINT: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_LDR_X(0u, 31u, 0u)) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_safepoint)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_B(0u)) ||
                    !orus_jit_a64_patch_list_append_branch(
                        &branch_patches, ORUS_JIT_A64_BRANCH_PATCH_KIND_B,
                        code.count - 1u, block->program.loop_start_offset)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                loop_back_patch_pending = true;
                goto finalize_block;
            }
            case ORUS_JIT_IR_OP_RETURN: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_B(0u)) ||
                    !orus_jit_a64_patch_list_append(&return_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                goto finalize_block;
            }
            default:
                A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
    }

finalize_block:;
    if (loop_back_patch_pending) {
        size_t loop_header_index = orus_jit_program_find_index(
            &block->program, block->program.loop_start_offset);
        if (loop_header_index == SIZE_MAX) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
    }

    bool loop_back_patch_resolved = false;
    for (size_t i = 0; i < branch_patches.count; ++i) {
        const OrusJitA64BranchPatch* patch = &branch_patches.data[i];
        if (!inst_offsets) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_index = orus_jit_program_find_index(
            &block->program, patch->target_bytecode);
        if (target_index == SIZE_MAX || target_index >= block->program.count) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_code_index = inst_offsets[target_index];
        if (target_code_index == SIZE_MAX) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        int64_t diff = (int64_t)target_code_index -
                       (int64_t)(patch->code_index + 1u);
        switch (patch->kind) {
            case ORUS_JIT_A64_BRANCH_PATCH_KIND_B:
                if (diff < -(1 << 25) || diff > ((1 << 25) - 1)) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                code.data[patch->code_index] =
                    A64_B((uint32_t)(diff & 0x03FFFFFFu));
                if (loop_back_patch_pending && !loop_back_patch_resolved &&
                    patch->target_bytecode == block->program.loop_start_offset &&
                    target_code_index != loop_entry_index) {
                    LOG_VM_DEBUG(
                        "JIT",
                        "Relocated loop header to bytecode offset 0x%X (code index %zu → %zu)",
                        patch->target_bytecode, loop_entry_index, target_code_index);
                }
                if (loop_back_patch_pending && !loop_back_patch_resolved &&
                    patch->target_bytecode == block->program.loop_start_offset) {
                    loop_back_patch_resolved = true;
                }
                break;
            case ORUS_JIT_A64_BRANCH_PATCH_KIND_CBNZ:
                if (diff < -(1 << 18) || diff > ((1 << 18) - 1)) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                code.data[patch->code_index] =
                    (code.data[patch->code_index] & ~0x00FFFFE0u) |
                    ((uint32_t)(diff & 0x7FFFFu) << 5);
                break;
            default:
                A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
    }

    size_t epilogue_index = code.count;
    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0x9100C3FFu) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xA8C17BF0u) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD65F03C0u)) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    for (size_t i = 0; i < bail_patches.count; ++i) {
        size_t index = bail_patches.data[i];
        int64_t diff = (int64_t)epilogue_index - (int64_t)index - 1;
        if (diff < -(1 << 18) || diff > ((1 << 18) - 1)) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        uint32_t imm = (uint32_t)(diff & 0x7FFFFu);
        code.data[index] = (code.data[index] & ~0x00FFFFE0u) | (imm << 5);
    }

    for (size_t i = 0; i < return_patches.count; ++i) {
        size_t index = return_patches.data[i];
        int64_t diff = (int64_t)epilogue_index - (int64_t)index - 1;
        if (diff < -(1 << 25) || diff > ((1 << 25) - 1)) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        uint32_t imm = (uint32_t)(diff & 0x03FFFFFFu);
        code.data[index] = A64_B(imm);
    }

    size_t encoded_size = code.count * sizeof(uint32_t);
    size_t capacity = 0u;
    size_t page_size = backend->page_size ? backend->page_size : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(encoded_size, page_size, &capacity);
    if (!buffer) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (((uintptr_t)buffer & 0x3u) != 0u) {
        LOG_ERROR("[JIT] Code buffer not 4-byte aligned: %p", buffer);
        orus_jit_release_executable(buffer, capacity);
        A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
    memcpy(buffer, code.data, encoded_size);
    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
#endif

    orus_jit_flush_icache(buffer, encoded_size);
#if defined(__APPLE__) && defined(__aarch64__)
    __asm__ __volatile__("isb" ::: "memory");
#endif

    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->code_ptr = buffer;
    entry->code_size = encoded_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_linear_a64";

    block->code_ptr = buffer;
    block->code_capacity = capacity;
    orus_jit_debug_publish_disassembly(&block->program,
                                       ORUS_JIT_BACKEND_TARGET_AARCH64,
                                       buffer,
                                       encoded_size);

    orus_jit_a64_code_buffer_release(&code);
    orus_jit_a64_patch_list_release(&bail_patches);
    orus_jit_a64_patch_list_release(&return_patches);
    orus_jit_a64_branch_patch_list_release(&branch_patches);
    free(inst_offsets);
#undef A64_RETURN
    return JIT_BACKEND_OK;
}

static JITBackendStatus
orus_jit_backend_compile_ir_arm64(struct OrusJitBackend* backend,
                                  const OrusJitIRProgram* program,
                                  JITEntry* entry) {
    if (!backend || !program || !entry || program->count == 0) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }

    OrusJitA64CodeBuffer code;
    orus_jit_a64_code_buffer_init(&code);
    OrusJitA64PatchList bail_patches;
    orus_jit_a64_patch_list_init(&bail_patches);

#define ARM64_RETURN(status)                                                      \
    do {                                                                         \
        orus_jit_a64_code_buffer_release(&code);                                  \
        orus_jit_a64_patch_list_release(&bail_patches);                           \
        return (status);                                                         \
    } while (0)

    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xA9BF7BF0u) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0x910003FDu)) {
        ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_ADD_I64:
            case ORUS_JIT_IR_OP_SUB_I64:
            case ORUS_JIT_IR_OP_MUL_I64:
            case ORUS_JIT_IR_OP_ADD_U64:
            case ORUS_JIT_IR_OP_SUB_U64:
            case ORUS_JIT_IR_OP_MUL_U64:
            case ORUS_JIT_IR_OP_ADD_F64:
            case ORUS_JIT_IR_OP_SUB_F64:
            case ORUS_JIT_IR_OP_MUL_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xAA1F03E1u) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                        (uint64_t)inst->opcode) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 3u,
                                                        (uint64_t)inst->value_kind) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u,
                        (uint64_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u,
                        (uint64_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 6u,
                        (uint64_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_linear_arithmetic)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LT_I64:
            case ORUS_JIT_IR_OP_LE_I64:
            case ORUS_JIT_IR_OP_GT_I64:
            case ORUS_JIT_IR_OP_GE_I64:
            case ORUS_JIT_IR_OP_EQ_I64:
            case ORUS_JIT_IR_OP_NE_I64:
            case ORUS_JIT_IR_OP_LT_U64:
            case ORUS_JIT_IR_OP_LE_U64:
            case ORUS_JIT_IR_OP_GT_U64:
            case ORUS_JIT_IR_OP_GE_U64:
            case ORUS_JIT_IR_OP_EQ_U64:
            case ORUS_JIT_IR_OP_NE_U64:
            case ORUS_JIT_IR_OP_LT_F64:
            case ORUS_JIT_IR_OP_LE_F64:
            case ORUS_JIT_IR_OP_GT_F64:
            case ORUS_JIT_IR_OP_GE_F64:
            case ORUS_JIT_IR_OP_EQ_F64:
            case ORUS_JIT_IR_OP_NE_F64: {
                if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xAA1F03E1u) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(&code, 2u,
                                                        (uint64_t)inst->opcode) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 3u,
                        (uint64_t)inst->operands.arithmetic.dst_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 4u,
                        (uint64_t)inst->operands.arithmetic.lhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 5u,
                        (uint64_t)inst->operands.arithmetic.rhs_reg) ||
                    !orus_jit_a64_emit_mov_imm64_buffer(
                        &code, 16u,
                        orus_jit_function_ptr_bits(&orus_jit_native_compare_op)) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_RETURN:
                break;
            default:
                ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
    }

    size_t branch_index = code.count;
    if (!orus_jit_a64_code_buffer_emit_u32(&code, A64_B(0u))) {
        ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    size_t bail_index = code.count;
    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xAA1F03E1u) ||
        !orus_jit_a64_emit_mov_imm64_buffer(
            &code, 16u,
            orus_jit_function_ptr_bits(&orus_jit_native_type_bailout)) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u)) {
        ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    size_t epilogue_index = code.count;
    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xA8C17BF0u) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD65F03C0u)) {
        ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    for (size_t i = 0; i < bail_patches.count; ++i) {
        size_t index = bail_patches.data[i];
        int64_t diff = (int64_t)bail_index - (int64_t)index - 1;
        if (diff < -(1 << 18) || diff > ((1 << 18) - 1)) {
            ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        uint32_t imm = (uint32_t)(diff & 0x7FFFFu);
        code.data[index] = A64_CBZ_W(0u, imm);
    }

    int64_t branch_diff = (int64_t)epilogue_index - (int64_t)branch_index - 1;
    if (branch_diff < -(1 << 25) || branch_diff > ((1 << 25) - 1)) {
        ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
    code.data[branch_index] = A64_B((uint32_t)(branch_diff & 0x03FFFFFFu));

    size_t encoded_size = code.count * sizeof(uint32_t);
    size_t capacity = 0u;
    size_t page_size = backend->page_size ? backend->page_size : orus_jit_detect_page_size();
    void* buffer = orus_jit_alloc_executable(encoded_size, page_size, &capacity);
    if (!buffer) {
        ARM64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (!orus_jit_set_write_protection(false)) {
        orus_jit_release_executable(buffer, capacity);
        ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
    memcpy(buffer, code.data, encoded_size);
    if (!orus_jit_set_write_protection(true)) {
        orus_jit_release_executable(buffer, capacity);
        ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        ARM64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
#endif

    orus_jit_flush_icache(buffer, encoded_size);

    entry->code_ptr = buffer;
    entry->code_size = encoded_size;
    entry->code_capacity = capacity;
    entry->entry_point = orus_jit_make_entry_point(buffer);
    entry->debug_name = "orus_jit_ir_stub_arm64";

    orus_jit_a64_code_buffer_release(&code);
    orus_jit_a64_patch_list_release(&bail_patches);
#undef ARM64_RETURN
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
    OrusJitIRProgram program;
    orus_jit_ir_program_init(&program);
    if (!orus_jit_ir_program_reserve(&program, 1u)) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }
    program.count = 1u;
    program.instructions[0].opcode = ORUS_JIT_IR_OP_RETURN;
    JITBackendStatus status = orus_jit_backend_compile_ir(backend, &program, out_entry);
    orus_jit_ir_program_reset(&program);
    return status;
}

JITBackendStatus
orus_jit_backend_compile_ir(struct OrusJitBackend* backend,
                            const OrusJitIRProgram* program,
                            JITEntry* out_entry) {
    if (!backend || !program || !out_entry || program->count == 0 ||
        !program->instructions) {
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
    if (!backend->available) {
        memset(out_entry, 0, sizeof(*out_entry));
        return JIT_BACKEND_UNSUPPORTED;
    }

    OrusJitNativeBlock* block = orus_jit_native_block_create(program);
    if (!block) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

#if ORUS_JIT_HAS_DYNASM_X86
#if defined(__x86_64__) || defined(_M_X64)
    if (orus_jit_should_force_dynasm()) {
        orus_jit_native_block_destroy(block);
        return orus_jit_backend_compile_ir_x86(backend, program, out_entry);
    }
#endif
#endif

    JITBackendStatus status = JIT_BACKEND_ASSEMBLY_ERROR;

#if defined(__x86_64__) || defined(_M_X64)
    if (orus_jit_linear_emitter_enabled() && !orus_jit_should_force_helper_stub()) {
        status = orus_jit_backend_emit_linear_x86(backend, block, out_entry);
        if (status == JIT_BACKEND_OK) {
            orus_jit_native_block_register(block);
            return JIT_BACKEND_OK;
        }
        if (status == JIT_BACKEND_OUT_OF_MEMORY) {
            orus_jit_native_block_destroy(block);
            return status;
        }
        status = JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif
#if defined(__aarch64__)
    if (orus_jit_linear_emitter_enabled() && !orus_jit_should_force_helper_stub()) {
        status = orus_jit_backend_emit_linear_a64(backend, block, out_entry);
        if (status == JIT_BACKEND_OK) {
            orus_jit_native_block_register(block);
            return JIT_BACKEND_OK;
        }
        if (status == JIT_BACKEND_OUT_OF_MEMORY) {
            orus_jit_native_block_destroy(block);
            return status;
        }
        status = JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif

    status = orus_jit_backend_emit_helper_stub(backend, block, out_entry);
    if (status != JIT_BACKEND_OK) {
        orus_jit_native_block_destroy(block);
#if ORUS_JIT_HAS_DYNASM_X86
#if defined(__x86_64__) || defined(_M_X64)
        return orus_jit_backend_compile_ir_x86(backend, program, out_entry);
#endif
#endif
#if defined(__aarch64__)
        return orus_jit_backend_compile_ir_arm64(backend, program, out_entry);
#else
        memset(out_entry, 0, sizeof(*out_entry));
        return status;
#endif
    }
    orus_jit_native_block_register(block);
    return JIT_BACKEND_OK;
}

void
orus_jit_backend_release_entry(struct OrusJitBackend* backend, JITEntry* entry) {
    (void)backend;
    if (!entry || !entry->code_ptr) {
        return;
    }
    OrusJitNativeBlock* prev = NULL;
    OrusJitNativeBlock* block =
        orus_jit_native_block_find(entry->code_ptr, &prev);
    if (block) {
        if (prev) {
            prev->next = block->next;
        } else {
            g_native_blocks = block->next;
        }
        orus_jit_native_block_destroy(block);
    }
    orus_jit_release_executable(entry->code_ptr, entry->code_capacity);
    entry->code_ptr = NULL;
    entry->entry_point = NULL;
    entry->code_capacity = 0;
    entry->code_size = 0;
    entry->debug_name = NULL;
}

static void
orus_jit_parity_record(const OrusJitIRInstruction* inst,
                       OrusJitParityReport* report) {
    if (!inst || !report) {
        return;
    }

    report->total_instructions++;

    switch (inst->opcode) {
        case ORUS_JIT_IR_OP_LOAD_I32_CONST:
        case ORUS_JIT_IR_OP_LOAD_I64_CONST:
        case ORUS_JIT_IR_OP_LOAD_U32_CONST:
        case ORUS_JIT_IR_OP_LOAD_U64_CONST:
        case ORUS_JIT_IR_OP_LOAD_F64_CONST:
        case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
        case ORUS_JIT_IR_OP_LOAD_VALUE_CONST:
        case ORUS_JIT_IR_OP_MOVE_I32:
        case ORUS_JIT_IR_OP_MOVE_I64:
        case ORUS_JIT_IR_OP_MOVE_U32:
        case ORUS_JIT_IR_OP_MOVE_U64:
        case ORUS_JIT_IR_OP_MOVE_F64:
        case ORUS_JIT_IR_OP_MOVE_BOOL:
        case ORUS_JIT_IR_OP_MOVE_STRING:
        case ORUS_JIT_IR_OP_MOVE_VALUE:
        case ORUS_JIT_IR_OP_RANGE:
        case ORUS_JIT_IR_OP_MAKE_ARRAY:
        case ORUS_JIT_IR_OP_ARRAY_PUSH:
        case ORUS_JIT_IR_OP_ARRAY_POP:
        case ORUS_JIT_IR_OP_ENUM_NEW:
            report->memory_ops++;
            break;

        case ORUS_JIT_IR_OP_ADD_I32:
        case ORUS_JIT_IR_OP_ADD_I64:
        case ORUS_JIT_IR_OP_ADD_U32:
        case ORUS_JIT_IR_OP_ADD_U64:
        case ORUS_JIT_IR_OP_ADD_F64:
        case ORUS_JIT_IR_OP_SUB_I32:
        case ORUS_JIT_IR_OP_SUB_I64:
        case ORUS_JIT_IR_OP_SUB_U32:
        case ORUS_JIT_IR_OP_SUB_U64:
        case ORUS_JIT_IR_OP_SUB_F64:
        case ORUS_JIT_IR_OP_MUL_I32:
        case ORUS_JIT_IR_OP_MUL_I64:
        case ORUS_JIT_IR_OP_MUL_U32:
        case ORUS_JIT_IR_OP_MUL_U64:
        case ORUS_JIT_IR_OP_MUL_F64:
        case ORUS_JIT_IR_OP_DIV_I32:
        case ORUS_JIT_IR_OP_DIV_I64:
        case ORUS_JIT_IR_OP_DIV_U32:
        case ORUS_JIT_IR_OP_DIV_U64:
        case ORUS_JIT_IR_OP_DIV_F64:
        case ORUS_JIT_IR_OP_MOD_I32:
        case ORUS_JIT_IR_OP_MOD_I64:
        case ORUS_JIT_IR_OP_MOD_U32:
        case ORUS_JIT_IR_OP_MOD_U64:
        case ORUS_JIT_IR_OP_MOD_F64:
            report->arithmetic_ops++;
            break;

        case ORUS_JIT_IR_OP_LT_I32:
        case ORUS_JIT_IR_OP_LE_I32:
        case ORUS_JIT_IR_OP_GT_I32:
        case ORUS_JIT_IR_OP_GE_I32:
        case ORUS_JIT_IR_OP_LT_I64:
        case ORUS_JIT_IR_OP_LE_I64:
        case ORUS_JIT_IR_OP_GT_I64:
        case ORUS_JIT_IR_OP_GE_I64:
        case ORUS_JIT_IR_OP_LT_U32:
        case ORUS_JIT_IR_OP_LE_U32:
        case ORUS_JIT_IR_OP_GT_U32:
        case ORUS_JIT_IR_OP_GE_U32:
        case ORUS_JIT_IR_OP_LT_U64:
        case ORUS_JIT_IR_OP_LE_U64:
        case ORUS_JIT_IR_OP_GT_U64:
        case ORUS_JIT_IR_OP_GE_U64:
        case ORUS_JIT_IR_OP_LT_F64:
        case ORUS_JIT_IR_OP_LE_F64:
        case ORUS_JIT_IR_OP_GT_F64:
        case ORUS_JIT_IR_OP_GE_F64:
        case ORUS_JIT_IR_OP_EQ_I32:
        case ORUS_JIT_IR_OP_NE_I32:
        case ORUS_JIT_IR_OP_EQ_I64:
        case ORUS_JIT_IR_OP_NE_I64:
        case ORUS_JIT_IR_OP_EQ_U32:
        case ORUS_JIT_IR_OP_NE_U32:
        case ORUS_JIT_IR_OP_EQ_U64:
        case ORUS_JIT_IR_OP_NE_U64:
        case ORUS_JIT_IR_OP_EQ_F64:
        case ORUS_JIT_IR_OP_NE_F64:
        case ORUS_JIT_IR_OP_EQ_BOOL:
        case ORUS_JIT_IR_OP_NE_BOOL:
            report->comparison_ops++;
            break;

        case ORUS_JIT_IR_OP_SAFEPOINT:
            report->safepoints++;
            break;

        case ORUS_JIT_IR_OP_I32_TO_I64:
        case ORUS_JIT_IR_OP_U32_TO_U64:
        case ORUS_JIT_IR_OP_U32_TO_I32:
        case ORUS_JIT_IR_OP_I32_TO_F64:
        case ORUS_JIT_IR_OP_I64_TO_F64:
        case ORUS_JIT_IR_OP_F64_TO_I32:
        case ORUS_JIT_IR_OP_F64_TO_I64:
        case ORUS_JIT_IR_OP_F64_TO_U32:
        case ORUS_JIT_IR_OP_U32_TO_F64:
        case ORUS_JIT_IR_OP_I32_TO_U32:
        case ORUS_JIT_IR_OP_I64_TO_U32:
        case ORUS_JIT_IR_OP_I32_TO_U64:
        case ORUS_JIT_IR_OP_I64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_I32:
        case ORUS_JIT_IR_OP_U64_TO_I64:
        case ORUS_JIT_IR_OP_U64_TO_U32:
        case ORUS_JIT_IR_OP_F64_TO_U64:
        case ORUS_JIT_IR_OP_U64_TO_F64:
            report->conversion_ops++;
            break;

        case ORUS_JIT_IR_OP_CONCAT_STRING:
        case ORUS_JIT_IR_OP_TO_STRING:
        case ORUS_JIT_IR_OP_TYPE_OF:
        case ORUS_JIT_IR_OP_IS_TYPE:
        case ORUS_JIT_IR_OP_TIME_STAMP:
        case ORUS_JIT_IR_OP_PRINT:
        case ORUS_JIT_IR_OP_ASSERT_EQ:
        case ORUS_JIT_IR_OP_CALL_NATIVE:
        case ORUS_JIT_IR_OP_CALL_FOREIGN:
        case ORUS_JIT_IR_OP_GET_ITER:
        case ORUS_JIT_IR_OP_ITER_NEXT:
            report->helper_ops++;
            break;

        default:
            break;
    }

    if (inst->value_kind < ORUS_JIT_VALUE_KIND_COUNT) {
        report->value_kind_mask |= (1u << inst->value_kind);
    }
}

static bool
orus_jit_backend_target_supports(OrusJitBackendTarget target,
                                 OrusJitIROpcode opcode,
                                 OrusJitValueKind kind) {
    (void)target;
    (void)opcode;
    return kind < ORUS_JIT_VALUE_KIND_COUNT;
}

JITBackendStatus
orus_jit_backend_collect_parity(const OrusJitIRProgram* program,
                                OrusJitBackendTarget target,
                                OrusJitParityReport* report) {
    if (!program || !report) {
        return JIT_BACKEND_UNSUPPORTED;
    }
    if ((unsigned)target >= ORUS_JIT_BACKEND_TARGET_COUNT) {
        return JIT_BACKEND_UNSUPPORTED;
    }

    memset(report, 0, sizeof(*report));

    if (!program->instructions || program->count == 0) {
        return JIT_BACKEND_OK;
    }

    for (size_t i = 0; i < program->count; ++i) {
        const OrusJitIRInstruction* inst = &program->instructions[i];
        if (!orus_jit_backend_target_supports(target, inst->opcode,
                                              inst->value_kind)) {
            return JIT_BACKEND_UNSUPPORTED;
        }
        orus_jit_parity_record(inst, report);
    }

    return JIT_BACKEND_OK;
}

static void
orus_jit_enter_stub(struct VM* vm, const JITEntry* entry) {
    if (!vm || !entry || !entry->entry_point) {
        return;
    }
    OrusJitNativeBlock* block = orus_jit_native_block_find(entry->code_ptr, NULL);
    TypedRegisterWindow* active_window = orus_jit_native_active_window(vm);
    OrusJitNativeFrame frame = {
        .block = block,
        .prev = vm->jit_native_frame_top,
        .active_window = active_window,
        .window_version = vm->typed_regs.window_version,
        .slow_path_requested = false,
        .canary = ORUS_JIT_NATIVE_FRAME_CANARY,
    };
    OrusJitNativeFrame* previous_frame = vm->jit_native_frame_top;
    bool previous_pending = vm->jit_native_slow_path_pending;
    vm->jit_native_frame_top = &frame;
    vm->jit_native_dispatch_count++;
    vm->jit_native_slow_path_pending = false;
    if (block) {
        orus_jit_debug_record_loop_entry(vm,
                                         block->program.function_index,
                                         block->program.loop_index);
    }
    entry->entry_point(vm);
    orus_jit_native_verify_frame(&frame);
    orus_jit_native_verify_frame(previous_frame);
    if (block && frame.slow_path_requested) {
        orus_jit_debug_record_loop_slow_path(vm,
                                             block->program.function_index,
                                             block->program.loop_index);
    }
    orus_jit_native_propagate_runtime_error(vm, &frame);
    if (frame.active_window) {
        vm->typed_regs.active_window = frame.active_window;
        vm->typed_regs.window_version = frame.window_version;
    }
    vm->jit_native_frame_top = previous_frame;
    vm->jit_native_slow_path_pending = previous_pending || frame.slow_path_requested;
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

void
orus_jit_backend_set_linear_emitter_enabled(bool enabled) {
    g_orus_jit_linear_emitter_override = enabled ? 1 : 0;
}

void
orus_jit_backend_clear_linear_emitter_override(void) {
    g_orus_jit_linear_emitter_override = -1;
}
