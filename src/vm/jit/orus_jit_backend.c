// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_backend.c
// Description: DynASM-backed JIT backend bootstrap providing minimal native
//              entry compilation for the Orus VM tiering roadmap.

#include "vm/jit_backend.h"
#include "vm/jit_ir.h"
#include "vm/jit_layout.h"
#include "vm/vm_comparison.h"
#include "vm/vm_profiling.h"
#include "vm/vm_tiering.h"
#include "vm/vm_string_ops.h"
#include "vm/register_file.h"
#include "runtime/builtins.h"

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

typedef struct OrusJitNativeBlock {
    OrusJitIRProgram program;
    void* code_ptr;
    size_t code_capacity;
    struct OrusJitNativeBlock* next;
} OrusJitNativeBlock;

static OrusJitNativeBlock* g_native_blocks = NULL;

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
    int flags = MAP_PRIVATE;
#ifdef MAP_ANONYMOUS
    flags |= MAP_ANONYMOUS;
#elif defined(MAP_ANON)
    flags |= MAP_ANON;
#endif
#if ORUS_JIT_USE_APPLE_JIT
    flags |= MAP_JIT;
#endif

    void* buffer = mmap(NULL, capacity, prot, flags, -1, 0);
    if (buffer == MAP_FAILED) {
#if ORUS_JIT_USE_APPLE_JIT
        const int error_code = errno;
        if (error_code == EPERM || error_code == ENOTSUP) {
            LOG_WARN("[JIT] mmap(MAP_JIT) failed with %s. macOS requires the com.apple.security.cs.allow-jit entitlement to enable native tier execution. The build tries to sign targets automatically; rerun scripts/macos/sign-with-jit.sh if codesign was unavailable during build.",
                     strerror(error_code));
        }
#endif
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
    if (result != 0) {
#if ORUS_JIT_USE_APPLE_JIT
        const int protect_errno = errno;
        if (protect_errno == EPERM || protect_errno == ENOTSUP) {
            LOG_WARN("[JIT] mprotect(PROT_EXEC) rejected with %s. Grant the com.apple.security.cs.allow-jit entitlement to execute native stubs on macOS. Builds attempt to sign automatically; run scripts/macos/sign-with-jit.sh manually if codesign was skipped.",
                     strerror(protect_errno));
        }
#endif
        orus_jit_set_write_protection(true);
        return false;
    }

    orus_jit_set_write_protection(true);
    return true;
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

static void orus_jit_execute_block(struct VM* vm_instance,
                                   const OrusJitNativeBlock* block);

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

    orus_jit_set_write_protection(false);
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

    orus_jit_set_write_protection(true);

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        return JIT_BACKEND_ASSEMBLY_ERROR;
    }
#endif

    orus_jit_flush_icache(buffer, stub_size);

    block->code_ptr = buffer;
    block->code_capacity = capacity;

    entry->entry_point = (JITEntryPoint)buffer;
    entry->code_ptr = buffer;
    entry->code_size = stub_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_helper_stub";
    return JIT_BACKEND_OK;
#elif defined(__aarch64__)
    size_t capacity = 0;
    size_t stub_size = 0;
    void* buffer = orus_jit_alloc_executable(64u, backend->page_size, &capacity);
    if (!buffer) {
        return JIT_BACKEND_OUT_OF_MEMORY;
    }

    orus_jit_set_write_protection(false);

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
            (uint64_t)(uintptr_t)&orus_jit_execute_block);
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

    orus_jit_set_write_protection(true);

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

    entry->entry_point = (JITEntryPoint)buffer;
    entry->code_ptr = buffer;
    entry->code_size = stub_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_helper_stub";
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
            if (function) {
                vm_default_deopt_stub(function);
                return;
            }
        }
    }

    vm_handle_type_error_deopt();
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

static void
orus_jit_native_safepoint(struct VM* vm_instance) {
    if (!vm_instance) {
        return;
    }
    GC_SAFEPOINT(vm_instance);
    PROF_SAFEPOINT(vm_instance);
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
    if (vm_typed_reg_in_range(reg)) {
        *out = vm_instance->typed_regs.bool_regs[reg];
        return true;
    }
    Value value = vm_get_register_safe(reg);
    if (!IS_BOOL(value)) {
        return false;
    }
    *out = AS_BOOL(value);
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

static bool
orus_jit_native_concat_string(struct VM* vm_instance,
                              OrusJitNativeBlock* block,
                              uint16_t dst,
                              uint16_t lhs,
                              uint16_t rhs) {
    if (!vm_instance) {
        return false;
    }

    Value left = vm_get_register_safe(lhs);
    Value right = vm_get_register_safe(rhs);
    if (!IS_STRING(left) || !IS_STRING(right)) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    ObjString* result = rope_concat_strings(AS_STRING(left), AS_STRING(right));
    if (!result) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst, STRING_VAL(result));
    return true;
}

static bool
orus_jit_native_to_string(struct VM* vm_instance,
                          OrusJitNativeBlock* block,
                          uint16_t dst,
                          uint16_t src) {
    if (!vm_instance) {
        return false;
    }

    Value val = vm_get_register_safe(src);
    if (IS_STRING(val)) {
        vm_set_register_safe(dst, val);
        return true;
    }

    char buffer[64];
    if (IS_I32(val)) {
        snprintf(buffer, sizeof(buffer), "%d", AS_I32(val));
    } else if (IS_I64(val)) {
        snprintf(buffer, sizeof(buffer), "%lld", (long long)AS_I64(val));
    } else if (IS_U32(val)) {
        snprintf(buffer, sizeof(buffer), "%u", AS_U32(val));
    } else if (IS_U64(val)) {
        snprintf(buffer, sizeof(buffer), "%llu",
                 (unsigned long long)AS_U64(val));
    } else if (IS_F64(val)) {
        snprintf(buffer, sizeof(buffer), "%g", AS_F64(val));
    } else if (IS_BOOL(val)) {
        snprintf(buffer, sizeof(buffer), "%s", AS_BOOL(val) ? "true" : "false");
    } else {
        snprintf(buffer, sizeof(buffer), "nil");
    }

    ObjString* result = allocateString(buffer, (int)strlen(buffer));
    if (!result) {
        jit_bailout_and_deopt(vm_instance, block);
        return false;
    }

    vm_set_register_safe(dst, STRING_VAL(result));
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

    register_file_reconcile_active_window();

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
    return true;
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

#if defined(__aarch64__)
static bool orus_jit_opcode_is_add(OrusJitIROpcode opcode);
static bool orus_jit_opcode_is_sub(OrusJitIROpcode opcode);
static bool orus_jit_opcode_is_mul(OrusJitIROpcode opcode);

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
    orus_jit_native_safepoint(vm_instance);
    return true;
}

#endif // defined(__aarch64__)

static const uint8_t MOV_RDI_R12[] = {0x4C, 0x89, 0xE7};
static const uint8_t MOV_RSI_RBX_BYTES[] = {0x48, 0x89, 0xDE};
static const uint8_t CALL_RAX[] = {0xFF, 0xD0};

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

#if defined(__aarch64__)
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
        default:
            jit_bailout_and_deopt(vm_instance, block);
            return -1;
    }

    return should_branch ? 1 : 0;
}
#endif // defined(__aarch64__)

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
        GC_SAFEPOINT(vm_instance);
        const OrusJitIRInstruction* inst = &instructions[ip];
        switch (inst->opcode) {
            case ORUS_JIT_IR_OP_LOAD_I32_CONST:
            case ORUS_JIT_IR_OP_LOAD_I64_CONST:
            case ORUS_JIT_IR_OP_LOAD_U32_CONST:
            case ORUS_JIT_IR_OP_LOAD_U64_CONST:
            case ORUS_JIT_IR_OP_LOAD_F64_CONST:
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
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
                PROF_SAFEPOINT(vm_instance);
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
                orus_jit_native_concat_string(vm_instance, (OrusJitNativeBlock*)block,
                                              inst->operands.arithmetic.dst_reg,
                                              inst->operands.arithmetic.lhs_reg,
                                              inst->operands.arithmetic.rhs_reg);
                break;
            case ORUS_JIT_IR_OP_TO_STRING:
                orus_jit_native_to_string(vm_instance, (OrusJitNativeBlock*)block,
                                          inst->operands.unary.dst_reg,
                                          inst->operands.unary.src_reg);
                break;
            case ORUS_JIT_IR_OP_TIME_STAMP:
                orus_jit_native_time_stamp(vm_instance, (OrusJitNativeBlock*)block,
                                           inst->operands.time_stamp.dst_reg);
                break;
            case ORUS_JIT_IR_OP_ARRAY_PUSH:
                if (!orus_jit_native_array_push(vm_instance,
                                                (OrusJitNativeBlock*)block,
                                                inst->operands.array_push.array_reg,
                                                inst->operands.array_push.value_reg)) {
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
                if (!orus_jit_native_call_native(vm_instance,
                                                 (OrusJitNativeBlock*)block,
                                                 inst->operands.call_native.dst_reg,
                                                 inst->operands.call_native.first_arg_reg,
                                                 inst->operands.call_native.arg_count,
                                                 inst->operands.call_native.native_index)) {
                    return;
                }
                break;
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
            case ORUS_JIT_IR_OP_JUMP_SHORT: {
                uint32_t fallthrough = inst->bytecode_offset + 2u;
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
                    uint32_t fallthrough = inst->bytecode_offset + 3u;
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
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
    return true;
#else
    return false;
#endif
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
static bool
orus_jit_should_force_helper_stub(void) {
    const char* value = getenv("ORUS_JIT_FORCE_HELPER_STUB");
    return value && value[0] != '\0';
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
orus_jit_emit_safepoint_call(OrusJitCodeBuffer* buffer) {
    if (!orus_jit_code_buffer_emit_u8(buffer, 0x48) ||
        !orus_jit_code_buffer_emit_u8(buffer, 0xB8) ||
        !orus_jit_code_buffer_emit_u64(buffer,
                                       (uint64_t)(uintptr_t)&orus_jit_native_safepoint) ||
        !orus_jit_code_buffer_emit_bytes(buffer, (const uint8_t[]){0xFF, 0xD0}, 2u)) {
        return false;
    }
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_load_string_const) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_move_string) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_move_value) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_concat_string) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_compare_op) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_to_string) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_time_stamp) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_array_push) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_print) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_assert_eq) ||
                    !orus_jit_code_buffer_emit_bytes(&code, CALL_RAX,
                                                     sizeof(CALL_RAX))) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CALL_NATIVE: {
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_call_native) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_get_iter) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_iter_next) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_range) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_convert_i32_to_i64) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_convert_u32_to_u64) ||
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
                        &code, (uint64_t)(uintptr_t)&orus_jit_native_convert_u32_to_i32) ||
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
                if (!orus_jit_emit_safepoint_call(&code)) {
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
                        inst->bytecode_offset + 2u +
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
                        inst->bytecode_offset + 3u +
                            inst->operands.jump_if_not_short.offset)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK: {
                if (!orus_jit_code_buffer_emit_u8(&code, 0xE9)) {
                    RETURN_WITH(JIT_BACKEND_OUT_OF_MEMORY);
                }
                size_t disp_offset = code.size;
                int64_t rel = (int64_t)loop_entry_offset -
                              ((int64_t)disp_offset + 4);
                int32_t disp = (int32_t)rel;
                if (!orus_jit_code_buffer_emit_u32(&code, (uint32_t)disp)) {
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
        size_t target_index = orus_jit_program_find_index(
            &block->program, patch->target_bytecode);
        if (target_index == SIZE_MAX || !inst_offsets) {
            RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_code = inst_offsets[target_index];
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
            &code, (uint64_t)(uintptr_t)&orus_jit_native_type_bailout) ||
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

    orus_jit_set_write_protection(false);
    memcpy(buffer, code.data, code.size);
    orus_jit_set_write_protection(true);

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        RETURN_WITH(JIT_BACKEND_ASSEMBLY_ERROR);
    }
#endif

    orus_jit_flush_icache(buffer, code.size);

    entry->entry_point = (JITEntryPoint)buffer;
    entry->code_ptr = buffer;
    entry->code_size = code.size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_linear_x86";

    block->code_ptr = buffer;
    block->code_capacity = capacity;

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
                if (inst->value_kind != ORUS_JIT_VALUE_BOOL) {
                    return JIT_BACKEND_ASSEMBLY_ERROR;
                }
                break;
            case ORUS_JIT_IR_OP_LOAD_STRING_CONST:
            case ORUS_JIT_IR_OP_MOVE_STRING:
            case ORUS_JIT_IR_OP_CONCAT_STRING:
            case ORUS_JIT_IR_OP_TO_STRING:
                if (inst->value_kind != ORUS_JIT_VALUE_STRING) {
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
            case ORUS_JIT_IR_OP_SAFEPOINT:
            case ORUS_JIT_IR_OP_LOOP_BACK:
            case ORUS_JIT_IR_OP_RETURN:
            case ORUS_JIT_IR_OP_JUMP_SHORT:
            case ORUS_JIT_IR_OP_JUMP_BACK_SHORT:
            case ORUS_JIT_IR_OP_JUMP_IF_NOT_SHORT:
            case ORUS_JIT_IR_OP_ARRAY_PUSH:
            case ORUS_JIT_IR_OP_PRINT:
            case ORUS_JIT_IR_OP_CALL_NATIVE:
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
    }

    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0xA9BF7BF0u) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0x910003FDu) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xD10083FFu) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xF90003E0u)) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    if (!orus_jit_a64_emit_mov_imm64_buffer(&code, 1u,
                                            (uint64_t)(uintptr_t)block) ||
        !orus_jit_a64_code_buffer_emit_u32(&code, 0xF90007E1u)) {
        A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
    }

    size_t loop_entry_index = code.count;

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
                        (uint64_t)(uintptr_t)&orus_jit_native_linear_load) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_linear_move) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_move_string) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_move_value) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_linear_arithmetic) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_compare_op) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_concat_string) ||
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
                        inst->bytecode_offset + 2u +
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
                        inst->bytecode_offset + 3u +
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
                        (uint64_t)(uintptr_t)&orus_jit_native_fused_loop_step) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_to_string) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_time_stamp) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_array_push) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_print) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_assert_eq) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_CALL_NATIVE: {
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
                        (uint64_t)(uintptr_t)&orus_jit_native_call_native) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_get_iter) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_iter_next) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_range) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_convert_i32_to_i64) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_convert_u32_to_u64) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_convert_u32_to_i32) ||
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
                        (uint64_t)(uintptr_t)&orus_jit_native_linear_safepoint) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, 0xD63F0200u) ||
                    !orus_jit_a64_code_buffer_emit_u32(&code, A64_CBZ_W(0u, 0u)) ||
                    !orus_jit_a64_patch_list_append(&bail_patches, code.count - 1u)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
                break;
            }
            case ORUS_JIT_IR_OP_LOOP_BACK: {
                int64_t diff = (int64_t)loop_entry_index - (int64_t)code.count - 1;
                if (diff < -(1 << 25) || diff > ((1 << 25) - 1)) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                uint32_t branch = A64_B((uint32_t)(diff & 0x03FFFFFFu));
                if (!orus_jit_a64_code_buffer_emit_u32(&code, branch)) {
                    A64_RETURN(JIT_BACKEND_OUT_OF_MEMORY);
                }
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
    for (size_t i = 0; i < branch_patches.count; ++i) {
        const OrusJitA64BranchPatch* patch = &branch_patches.data[i];
        if (!inst_offsets) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_index = orus_jit_program_find_index(
            &block->program, patch->target_bytecode);
        if (target_index == SIZE_MAX) {
            A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
        }
        size_t target_code_index = inst_offsets[target_index];
        int64_t diff = (int64_t)target_code_index -
                       (int64_t)(patch->code_index + 1u);
        switch (patch->kind) {
            case ORUS_JIT_A64_BRANCH_PATCH_KIND_B:
                if (diff < -(1 << 25) || diff > ((1 << 25) - 1)) {
                    A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
                }
                code.data[patch->code_index] =
                    A64_B((uint32_t)(diff & 0x03FFFFFFu));
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
    if (!orus_jit_a64_code_buffer_emit_u32(&code, 0x910083FFu) ||
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

    orus_jit_set_write_protection(false);
    memcpy(buffer, code.data, encoded_size);
    orus_jit_set_write_protection(true);

#if !defined(_WIN32)
    if (!orus_jit_make_executable(buffer, capacity)) {
        orus_jit_release_executable(buffer, capacity);
        A64_RETURN(JIT_BACKEND_ASSEMBLY_ERROR);
    }
#endif

    orus_jit_flush_icache(buffer, encoded_size);

    entry->entry_point = (JITEntryPoint)buffer;
    entry->code_ptr = buffer;
    entry->code_size = encoded_size;
    entry->code_capacity = capacity;
    entry->debug_name = "orus_jit_linear_a64";

    block->code_ptr = buffer;
    block->code_capacity = capacity;

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
        cursor[i] = 0xD65F03C0u;
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

    JITBackendStatus status = JIT_BACKEND_ASSEMBLY_ERROR;

#if defined(__x86_64__) || defined(_M_X64)
    if (!orus_jit_should_force_helper_stub()) {
        status = orus_jit_backend_emit_linear_x86(backend, block, out_entry);
        if (status == JIT_BACKEND_OK) {
            orus_jit_native_block_register(block);
            return JIT_BACKEND_OK;
        }
        if (status == JIT_BACKEND_OUT_OF_MEMORY) {
            orus_jit_native_block_destroy(block);
            return status;
        }
    }
#endif
#if defined(__aarch64__)
    if (!orus_jit_should_force_helper_stub()) {
        status = orus_jit_backend_emit_linear_a64(backend, block, out_entry);
        if (status == JIT_BACKEND_OK) {
            orus_jit_native_block_register(block);
            return JIT_BACKEND_OK;
        }
        if (status == JIT_BACKEND_OUT_OF_MEMORY) {
            orus_jit_native_block_destroy(block);
            return status;
        }
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
