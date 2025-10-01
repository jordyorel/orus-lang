/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: include/vm/vm_dispatch.h
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Declares primary dispatch loop utilities for executing bytecode
 *              programs.
 */

#ifndef vm_dispatch_h
#define vm_dispatch_h

// ✅ Auto-detect computed goto support
#ifndef USE_COMPUTED_GOTO
  #if defined(__GNUC__) || defined(__clang__)
    #define USE_COMPUTED_GOTO 1
  #else
    #define USE_COMPUTED_GOTO 0
  #endif
#endif

#include "vm.h"
#include "vm_control_flow.h"
#include "public/common.h"
#include "vm/vm_profiling.h"

// Forward declarations
extern VM vm;

// Common dispatch function prototype
InterpretResult vm_run_dispatch(void);

// Timer utility shared with dispatch implementations
double get_time_vm(void);

// Deferred runtime error reporting control
void vm_set_error_report_pending(bool pending);
bool vm_get_error_report_pending(void);
void vm_report_unhandled_error(void);

// Dispatch table for computed goto (when enabled)
#if USE_COMPUTED_GOTO
extern void* vm_dispatch_table[OP_HALT + 1];
#endif

static inline void vm_update_source_location(size_t offset) {
    if (!vm.chunk || offset >= (size_t)vm.chunk->count) {
        vm.currentLine = -1;
        vm.currentColumn = -1;
        return;
    }
    vm.currentLine = vm.chunk->lines ? vm.chunk->lines[offset] : -1;
    vm.currentColumn = vm.chunk->columns ? vm.chunk->columns[offset] : -1;
}

static inline bool vm_handle_pending_error(void) {
    if (!IS_ERROR(vm.lastError)) {
        return true;
    }

    if (vm.tryFrameCount > 0) {
        TryFrame frame = vm.tryFrames[--vm.tryFrameCount];
        vm_unwind_to_stack_depth(frame.stackDepth);
        vm.ip = frame.handler;
        if (frame.catchRegister != TRY_CATCH_REGISTER_NONE) {
            vm_set_register_safe(frame.catchRegister, vm.lastError);
        }
        vm_set_error_report_pending(false);
        vm.lastError = BOOL_VAL(false);
        return true;
    }

    return false;
}

// Common macros used by both dispatch implementations
#define READ_BYTE() (*vm.ip++)
#define READ_SHORT() \
    (vm.ip += 2, (uint16_t)((vm.ip[-2] << 8) | vm.ip[-1]))
#define READ_CONSTANT(index) (vm.chunk->constants.values[index])
// Note: RETURN macro is defined in vm.c to handle timing

// Error handling function - implemented in vm.c
void runtimeError(ErrorType type, SrcLocation location, const char* format, ...);

#ifdef VM_ENABLE_PROFILING
#  define PROFILE_INC(op) (vm.profile.instruction_counts[(op)]++)
#else
#  define PROFILE_INC(op) ((void)0)
#endif

// Dispatch macros - defined differently for each implementation
#if USE_COMPUTED_GOTO
    // Fast dispatch macros for computed goto
    #ifdef ORUS_DEBUG
        #define DISPATCH() \
            do { \
                if (IS_ERROR(vm.lastError)) { \
                    if (!vm_handle_pending_error()) { \
                        RETURN(INTERPRET_RUNTIME_ERROR); \
                    } \
                } \
                if (vm.trace) { \
                    printf("        "); \
                    for (int i = 0; i < 8; i++) { \
                        printf("[ R%d: ", i); \
                        Value debug_val = vm_get_register_safe((uint16_t)i); \
                        printValue(debug_val); \
                        printf(" ]"); \
                    } \
                    printf("\\n"); \
                    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code)); \
                } \
                vm.instruction_count++; \
                instruction = READ_BYTE(); \
                vm_update_source_location((size_t)((vm.ip - vm.chunk->code) - 1)); \
                PROFILE_INC(instruction); \
                if (instruction > OP_HALT || vm_dispatch_table[instruction] == NULL) { \
                    goto LABEL_UNKNOWN; \
                } \
                goto *vm_dispatch_table[instruction]; \
            } while (0)
        #define DISPATCH_TYPED() DISPATCH()
    #else
        #define DISPATCH() do { \
            uint8_t inst = *vm.ip++; \
            vm_update_source_location((size_t)((vm.ip - vm.chunk->code) - 1)); \
            PROFILE_INC(inst); \
            if (g_profiling.isActive && instruction_start_time > 0) { \
                uint64_t cycles = getTimestamp() - instruction_start_time; \
                profileInstruction(inst, cycles); \
            } \
            if (g_profiling.isActive) { \
                instruction_start_time = getTimestamp(); \
            } \
            if (inst > OP_HALT || vm_dispatch_table[inst] == NULL) { \
                printf("[DISPATCH_ERROR] Invalid opcode %d (0x%02X) or NULL dispatch entry!\n", inst, inst); \
                fflush(stdout); \
                goto LABEL_UNKNOWN; \
            } \
            goto *vm_dispatch_table[inst]; \
        } while(0)
        #define DISPATCH_TYPED() DISPATCH()
        
        // Check for runtime errors after handler execution
        #define CHECK_RUNTIME_ERROR() do { \
            if (IS_ERROR(vm.lastError)) { \
                goto HANDLE_RUNTIME_ERROR; \
            } \
        } while (0)
    #endif
#else
    // Switch-based dispatch doesn't use these macros
    #define DISPATCH() break
    #define DISPATCH_TYPED() break
#endif

#endif