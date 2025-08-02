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
#include "public/common.h"
#include "vm/vm_profiling.h"

// Forward declarations
extern VM vm;

// Common dispatch function prototype
InterpretResult vm_run_dispatch(void);

// Timer utility shared with dispatch implementations
double get_time_vm(void);

// Dispatch table for computed goto (when enabled)
#if USE_COMPUTED_GOTO
extern void* vm_dispatch_table[OP_HALT + 1];
#endif

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
#ifdef USE_COMPUTED_GOTO
    // Fast dispatch macros for computed goto
    #ifdef ORUS_DEBUG
        #define DISPATCH() \
            do { \
                if (IS_ERROR(vm.lastError)) { \
                    if (vm.tryFrameCount > 0) { \
                        TryFrame frame = vm.tryFrames[--vm.tryFrameCount]; \
                        vm.ip = frame.handler; \
                        vm.globals[frame.varIndex] = vm.lastError; \
                        vm.lastError = BOOL_VAL(false); \
                    } else { \
                        RETURN(INTERPRET_RUNTIME_ERROR); \
                    } \
                } \
                if (vm.trace) { \
                    printf("        "); \
                    for (int i = 0; i < 8; i++) { \
                        printf("[ R%d: ", i); \
                        printValue(vm.registers[i]); \
                        printf(" ]"); \
                    } \
                    printf("\\n"); \
                    disassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code)); \
                } \
                vm.instruction_count++; \
                instruction = READ_BYTE(); \
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
            /* printf("[INSTRUCTION_TRACE] Executing instruction: 0x%02X (%d)\n", inst, inst); */ \
            /* fflush(stdout); */ \
            PROFILE_INC(inst); \
            if (g_profiling.isActive && instruction_start_time > 0) { \
                uint64_t cycles = getTimestamp() - instruction_start_time; \
                profileInstruction(inst, cycles); \
            } \
            if (g_profiling.isActive) { \
                instruction_start_time = getTimestamp(); \
            } \
            goto *vm_dispatch_table[inst]; \
        } while(0)
        #define DISPATCH_TYPED() DISPATCH()
        
        // Check for runtime errors after handler execution
        #define CHECK_RUNTIME_ERROR() do { \
            if (IS_ERROR(vm.lastError)) { \
                if (vm.tryFrameCount > 0) { \
                    TryFrame frame = vm.tryFrames[--vm.tryFrameCount]; \
                    vm.ip = frame.handler; \
                    vm.globals[frame.varIndex] = vm.lastError; \
                    vm.lastError = BOOL_VAL(false); \
                } else { \
                    RETURN(INTERPRET_RUNTIME_ERROR); \
                } \
            } \
        } while (0)
    #endif
#else
    // Switch-based dispatch doesn't use these macros
    #define DISPATCH() break
    #define DISPATCH_TYPED() break
#endif

#endif