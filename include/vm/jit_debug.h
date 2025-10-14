// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/vm/jit_debug.h
// Description: Debug instrumentation controls for capturing JIT artifacts and
//              loop telemetry during native execution.

#ifndef ORUS_VM_JIT_DEBUG_H
#define ORUS_VM_JIT_DEBUG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/jit_backend.h"

struct VM;

typedef struct {
    bool capture_disassembly;
    bool capture_guard_traces;
    bool loop_telemetry_enabled;
} OrusJitDebugConfig;

#define ORUS_JIT_DEBUG_CONFIG_INIT                                             \
    {                                                                          \
        .capture_disassembly = false, .capture_guard_traces = false,           \
        .loop_telemetry_enabled = false                                        \
    }

typedef struct {
    const char* buffer;
    size_t length;
    OrusJitBackendTarget target;
    uint16_t function_index;
    uint16_t loop_index;
    size_t code_size;
} OrusJitDebugDisassembly;

typedef struct {
    uint64_t timestamp;
    uint16_t function_index;
    uint16_t loop_index;
    uint32_t instruction_index;
    char reason[64];
} OrusJitGuardTraceEvent;

typedef struct {
    uint16_t function_index;
    uint16_t loop_index;
    uint64_t entries;
    uint64_t guard_exits;
    uint64_t slow_paths;
    uint64_t last_timestamp;
    bool enabled;
} OrusJitLoopTelemetry;

#define ORUS_JIT_DEBUG_INVALID_INSTRUCTION_INDEX UINT32_MAX

void orus_jit_debug_reset(void);
void orus_jit_debug_set_config(const OrusJitDebugConfig* config);
OrusJitDebugConfig orus_jit_debug_get_config(void);

void orus_jit_debug_publish_disassembly(const OrusJitIRProgram* program,
                                        OrusJitBackendTarget target,
                                        const void* code_ptr,
                                        size_t code_size);

bool orus_jit_debug_last_disassembly(OrusJitDebugDisassembly* out);

void orus_jit_debug_record_guard_exit(const struct VM* vm,
                                      uint16_t function_index,
                                      uint16_t loop_index,
                                      const char* reason,
                                      uint32_t instruction_index);

size_t orus_jit_debug_guard_trace_count(void);
size_t orus_jit_debug_copy_guard_traces(OrusJitGuardTraceEvent* out,
                                        size_t max_events);

void orus_jit_debug_set_loop_enabled(uint16_t loop_index, bool enabled);
void orus_jit_debug_clear_loop_overrides(void);

void orus_jit_debug_record_loop_entry(const struct VM* vm,
                                      uint16_t function_index,
                                      uint16_t loop_index);
void orus_jit_debug_record_loop_guard_exit(const struct VM* vm,
                                           uint16_t function_index,
                                           uint16_t loop_index);
void orus_jit_debug_record_loop_slow_path(const struct VM* vm,
                                          uint16_t function_index,
                                          uint16_t loop_index);

size_t orus_jit_debug_collect_loop_telemetry(OrusJitLoopTelemetry* out,
                                             size_t max_entries);

#endif // ORUS_VM_JIT_DEBUG_H
