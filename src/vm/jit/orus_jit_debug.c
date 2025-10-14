// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/jit/orus_jit_debug.c
// Description: Debug instrumentation controls for capturing DynASM disassembly,
//              guard exit traces, and per-loop telemetry.

#include "vm/jit_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm/jit_ir_debug.h"
#include "vm/vm.h"

#define ORUS_JIT_DEBUG_GUARD_TRACE_CAPACITY 128u

typedef struct {
    char* data;
    size_t length;
    size_t capacity;
} OrusJitDebugBuffer;

static struct {
    OrusJitDebugConfig config;

    char* disassembly_buffer;
    size_t disassembly_length;
    OrusJitDebugDisassembly disassembly_meta;

    OrusJitGuardTraceEvent guard_traces[ORUS_JIT_DEBUG_GUARD_TRACE_CAPACITY];
    size_t guard_trace_head;
    size_t guard_trace_count;

    OrusJitLoopTelemetry loop_telemetry[VM_MAX_PROFILED_LOOPS];
    bool loop_overrides[VM_MAX_PROFILED_LOOPS];
    bool loop_override_active;
} g_orus_jit_debug = {
    .config = ORUS_JIT_DEBUG_CONFIG_INIT,
    .disassembly_buffer = NULL,
    .disassembly_length = 0u,
    .disassembly_meta = {0},
    .guard_trace_head = 0u,
    .guard_trace_count = 0u,
    .loop_override_active = false,
};

static void
orus_jit_debug_buffer_reset(OrusJitDebugBuffer* buffer) {
    if (!buffer) {
        return;
    }
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = 0u;
    buffer->capacity = 0u;
}

static bool
orus_jit_debug_buffer_append(OrusJitDebugBuffer* buffer, const char* fmt, ...) {
    if (!buffer || !fmt) {
        return false;
    }

    va_list args;
    va_start(args, fmt);
    int required = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (required < 0) {
        return false;
    }

    size_t needed = buffer->length + (size_t)required + 1u;
    if (needed > buffer->capacity) {
        size_t new_capacity = buffer->capacity ? buffer->capacity : 256u;
        while (new_capacity < needed) {
            new_capacity *= 2u;
        }
        char* resized = (char*)realloc(buffer->data, new_capacity);
        if (!resized) {
            return false;
        }
        buffer->data = resized;
        buffer->capacity = new_capacity;
    }

    va_start(args, fmt);
    int written = vsnprintf(buffer->data + buffer->length,
                            buffer->capacity - buffer->length,
                            fmt,
                            args);
    va_end(args);

    if (written < 0) {
        return false;
    }

    buffer->length += (size_t)written;
    buffer->data[buffer->length] = '\0';
    return true;
}

static inline uint64_t
orus_jit_debug_timestamp(const struct VM* vm) {
    return vm ? vm->ticks : 0u;
}

static inline bool
orus_jit_debug_loop_is_enabled(uint16_t loop_index) {
    if (!g_orus_jit_debug.config.loop_telemetry_enabled) {
        return false;
    }
    if (loop_index == UINT16_MAX) {
        return false;
    }
    if (!g_orus_jit_debug.loop_override_active) {
        return true;
    }
    return g_orus_jit_debug.loop_overrides[loop_index];
}

static OrusJitLoopTelemetry*
orus_jit_debug_loop_slot(uint16_t loop_index) {
    if (loop_index == UINT16_MAX) {
        return NULL;
    }
    return &g_orus_jit_debug.loop_telemetry[loop_index];
}

static void
orus_jit_debug_update_loop_metadata(OrusJitLoopTelemetry* telemetry,
                                    uint16_t function_index,
                                    uint16_t loop_index,
                                    uint64_t timestamp) {
    if (!telemetry) {
        return;
    }
    telemetry->function_index = function_index;
    telemetry->loop_index = loop_index;
    telemetry->last_timestamp = timestamp;
    telemetry->enabled = true;
}

void
orus_jit_debug_reset(void) {
    OrusJitDebugBuffer buffer = {
        .data = g_orus_jit_debug.disassembly_buffer,
        .length = g_orus_jit_debug.disassembly_length,
        .capacity = g_orus_jit_debug.disassembly_buffer ?
                     g_orus_jit_debug.disassembly_length + 1u : 0u,
    };
    orus_jit_debug_buffer_reset(&buffer);
    g_orus_jit_debug.disassembly_buffer = NULL;
    g_orus_jit_debug.disassembly_length = 0u;
    memset(&g_orus_jit_debug.disassembly_meta, 0, sizeof(g_orus_jit_debug.disassembly_meta));

    memset(g_orus_jit_debug.guard_traces,
           0,
           sizeof(g_orus_jit_debug.guard_traces));
    g_orus_jit_debug.guard_trace_head = 0u;
    g_orus_jit_debug.guard_trace_count = 0u;

    memset(g_orus_jit_debug.loop_telemetry,
           0,
           sizeof(g_orus_jit_debug.loop_telemetry));
    memset(g_orus_jit_debug.loop_overrides,
           0,
           sizeof(g_orus_jit_debug.loop_overrides));
    g_orus_jit_debug.loop_override_active = false;

    g_orus_jit_debug.config = (OrusJitDebugConfig)ORUS_JIT_DEBUG_CONFIG_INIT;
}

void
orus_jit_debug_set_config(const OrusJitDebugConfig* config) {
    if (!config) {
        g_orus_jit_debug.config = (OrusJitDebugConfig)ORUS_JIT_DEBUG_CONFIG_INIT;
        return;
    }
    g_orus_jit_debug.config = *config;
}

OrusJitDebugConfig
orus_jit_debug_get_config(void) {
    return g_orus_jit_debug.config;
}

void
orus_jit_debug_publish_disassembly(const OrusJitIRProgram* program,
                                    OrusJitBackendTarget target,
                                    const void* code_ptr,
                                    size_t code_size) {
    if (!g_orus_jit_debug.config.capture_disassembly || !program) {
        return;
    }

    OrusJitDebugBuffer buffer = {0};
    bool ok = true;
    ok &= orus_jit_debug_buffer_append(
        &buffer,
        "# JIT disassembly\n"
        "function=%u loop=%u target=%d code_size=%zu\n\n",
        (unsigned)program->function_index,
        (unsigned)program->loop_index,
        (int)target,
        code_size);

    if (program->instructions && program->count > 0u) {
        for (size_t i = 0; ok && i < program->count; ++i) {
            char inst_buffer[256];
            size_t len = orus_jit_ir_format_instruction(&program->instructions[i],
                                                        inst_buffer,
                                                        sizeof(inst_buffer));
            (void)len;
            ok &= orus_jit_debug_buffer_append(&buffer,
                                                "%04zu: %s\n",
                                                i,
                                                inst_buffer);
        }
    }

    if (code_ptr && code_size > 0u) {
        const unsigned char* bytes = (const unsigned char*)code_ptr;
        ok &= orus_jit_debug_buffer_append(&buffer,
                                            "\n# Machine code (%zu bytes)\n",
                                            code_size);
        for (size_t i = 0; ok && i < code_size; ++i) {
            if ((i % 16u) == 0u) {
                ok &= orus_jit_debug_buffer_append(&buffer, "%04zu:", i);
            }
            ok &= orus_jit_debug_buffer_append(&buffer,
                                                " %02X",
                                                (unsigned)bytes[i]);
            if ((i % 16u) == 15u || i + 1u == code_size) {
                ok &= orus_jit_debug_buffer_append(&buffer, "\n");
            }
        }
    }

    if (!ok) {
        orus_jit_debug_buffer_reset(&buffer);
        return;
    }

    free(g_orus_jit_debug.disassembly_buffer);
    g_orus_jit_debug.disassembly_buffer = buffer.data;
    g_orus_jit_debug.disassembly_length = buffer.length;
    g_orus_jit_debug.disassembly_meta = (OrusJitDebugDisassembly){
        .buffer = g_orus_jit_debug.disassembly_buffer,
        .length = g_orus_jit_debug.disassembly_length,
        .target = target,
        .function_index = program->function_index,
        .loop_index = program->loop_index,
        .code_size = code_size,
    };
}

bool
orus_jit_debug_last_disassembly(OrusJitDebugDisassembly* out) {
    if (!out || !g_orus_jit_debug.disassembly_buffer) {
        return false;
    }
    *out = g_orus_jit_debug.disassembly_meta;
    out->buffer = g_orus_jit_debug.disassembly_buffer;
    out->length = g_orus_jit_debug.disassembly_length;
    return true;
}

void
orus_jit_debug_record_guard_exit(const struct VM* vm,
                                  uint16_t function_index,
                                  uint16_t loop_index,
                                  const char* reason,
                                  uint32_t instruction_index) {
    if (!g_orus_jit_debug.config.capture_guard_traces) {
        return;
    }

    size_t slot_index = g_orus_jit_debug.guard_trace_head %
                        ORUS_JIT_DEBUG_GUARD_TRACE_CAPACITY;
    OrusJitGuardTraceEvent* event =
        &g_orus_jit_debug.guard_traces[slot_index];

    event->timestamp = orus_jit_debug_timestamp(vm);
    event->function_index = function_index;
    event->loop_index = loop_index;
    event->instruction_index = instruction_index;
    if (reason && reason[0] != '\0') {
        strncpy(event->reason, reason, sizeof(event->reason) - 1u);
        event->reason[sizeof(event->reason) - 1u] = '\0';
    } else {
        event->reason[0] = '\0';
    }

    g_orus_jit_debug.guard_trace_head++;
    if (g_orus_jit_debug.guard_trace_count <
        ORUS_JIT_DEBUG_GUARD_TRACE_CAPACITY) {
        g_orus_jit_debug.guard_trace_count++;
    }

    if (orus_jit_debug_loop_is_enabled(loop_index)) {
        OrusJitLoopTelemetry* telemetry =
            orus_jit_debug_loop_slot(loop_index);
        orus_jit_debug_update_loop_metadata(telemetry,
                                            function_index,
                                            loop_index,
                                            event->timestamp);
        telemetry->guard_exits++;
    }
}

size_t
orus_jit_debug_guard_trace_count(void) {
    return g_orus_jit_debug.guard_trace_count;
}

size_t
orus_jit_debug_copy_guard_traces(OrusJitGuardTraceEvent* out,
                                 size_t max_events) {
    if (!out || max_events == 0u) {
        return 0u;
    }

    size_t available = g_orus_jit_debug.guard_trace_count;
    if (available == 0u) {
        return 0u;
    }
    size_t to_copy = available < max_events ? available : max_events;
    size_t start_index = (g_orus_jit_debug.guard_trace_head >= available)
                             ? (g_orus_jit_debug.guard_trace_head - available)
                             : 0u;

    for (size_t i = 0; i < to_copy; ++i) {
        size_t index = (start_index + i) % ORUS_JIT_DEBUG_GUARD_TRACE_CAPACITY;
        out[i] = g_orus_jit_debug.guard_traces[index];
    }
    return to_copy;
}

void
orus_jit_debug_set_loop_enabled(uint16_t loop_index, bool enabled) {
    if (loop_index == UINT16_MAX) {
        return;
    }
    g_orus_jit_debug.loop_override_active = true;
    g_orus_jit_debug.loop_overrides[loop_index] = enabled;
    if (!enabled) {
        g_orus_jit_debug.loop_telemetry[loop_index].enabled = false;
    }
}

void
orus_jit_debug_clear_loop_overrides(void) {
    memset(g_orus_jit_debug.loop_overrides,
           0,
           sizeof(g_orus_jit_debug.loop_overrides));
    g_orus_jit_debug.loop_override_active = false;
}

void
orus_jit_debug_record_loop_entry(const struct VM* vm,
                                  uint16_t function_index,
                                  uint16_t loop_index) {
    if (!orus_jit_debug_loop_is_enabled(loop_index)) {
        return;
    }

    OrusJitLoopTelemetry* telemetry =
        orus_jit_debug_loop_slot(loop_index);
    if (!telemetry) {
        return;
    }

    uint64_t timestamp = orus_jit_debug_timestamp(vm);
    orus_jit_debug_update_loop_metadata(telemetry,
                                        function_index,
                                        loop_index,
                                        timestamp);
    telemetry->entries++;
}

void
orus_jit_debug_record_loop_guard_exit(const struct VM* vm,
                                       uint16_t function_index,
                                       uint16_t loop_index) {
    if (!orus_jit_debug_loop_is_enabled(loop_index)) {
        return;
    }
    OrusJitLoopTelemetry* telemetry =
        orus_jit_debug_loop_slot(loop_index);
    if (!telemetry) {
        return;
    }
    uint64_t timestamp = orus_jit_debug_timestamp(vm);
    orus_jit_debug_update_loop_metadata(telemetry,
                                        function_index,
                                        loop_index,
                                        timestamp);
    telemetry->guard_exits++;
}

void
orus_jit_debug_record_loop_slow_path(const struct VM* vm,
                                      uint16_t function_index,
                                      uint16_t loop_index) {
    if (!orus_jit_debug_loop_is_enabled(loop_index)) {
        return;
    }
    OrusJitLoopTelemetry* telemetry =
        orus_jit_debug_loop_slot(loop_index);
    if (!telemetry) {
        return;
    }
    uint64_t timestamp = orus_jit_debug_timestamp(vm);
    orus_jit_debug_update_loop_metadata(telemetry,
                                        function_index,
                                        loop_index,
                                        timestamp);
    telemetry->slow_paths++;
}

size_t
orus_jit_debug_collect_loop_telemetry(OrusJitLoopTelemetry* out,
                                       size_t max_entries) {
    if (!out || max_entries == 0u) {
        return 0u;
    }

    size_t copied = 0u;
    for (size_t i = 0; i < VM_MAX_PROFILED_LOOPS && copied < max_entries; ++i) {
        const OrusJitLoopTelemetry* telemetry =
            &g_orus_jit_debug.loop_telemetry[i];
        if (!telemetry->enabled || (telemetry->entries == 0u &&
                                    telemetry->guard_exits == 0u &&
                                    telemetry->slow_paths == 0u)) {
            continue;
        }
        out[copied++] = *telemetry;
    }
    return copied;
}
