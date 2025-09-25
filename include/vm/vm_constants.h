// vm_constants.h - Shared VM configuration constants
#ifndef VM_CONSTANTS_H
#define VM_CONSTANTS_H

// Call stack limits
#define VM_MAX_CALL_FRAMES 256
#define VM_MAX_REGISTERS 256
#define VM_MAX_UPVALUES 256

// Phase 1: Register file architecture constants
#define GLOBAL_REGISTERS 64     // R0-R63: Fast-access globals
#define FRAME_REGISTERS 128     // R64-R191: Function locals and parameters
#define TEMP_REGISTERS 48       // R192-R239: Scratch space for expression temps
#define MODULE_REGISTERS 16     // R240-R255: Module scope registers

// Compiler limits - generous but practical limit
#define MAX_LOCAL_VARIABLES 32768  // Maximum local variables per compilation unit (half of uint16_t range)

// Register ID layout (as per roadmap)
#define GLOBAL_REG_START 0
#define FRAME_REG_START 64
#define TEMP_REG_START 192
#define MODULE_REG_START 240      // Module registers
#define SPILL_REG_START 256       // Registers beyond the primary window spill

// String operation thresholds
#define VM_SMALL_STRING_BUFFER 1024
#define VM_LARGE_STRING_THRESHOLD 4096

// Performance tuning
#define VM_DISPATCH_TABLE_SIZE (OP_HALT + 1)
#define VM_TYPED_REGISTER_COUNT 256

// Error handling
#define VM_MAX_ERROR_MESSAGE_LENGTH 256
#define VM_MAX_STACK_TRACE_DEPTH 32

// Generic constants
#define ARENA_ALIGNMENT 8
#define HASHMAP_INITIAL_CAPACITY 16
#define DJB2_INITIAL_HASH 5381
#define DJB2_SHIFT 5

#define BINARY_BUFFER_SIZE 65
#define BINARY_BUFFER_LAST_INDEX 64

#define HISTORY_SAVE_LIMIT 500
#define COMMAND_TIMING_PREFIX_LEN 8
#define COMMAND_MEMORY_PREFIX_LEN 8
#define COMMAND_LOAD_PREFIX_LEN 6

#define FNV_OFFSET_BASIS 14695981039346656037ull
#define FNV_PRIME 1099511628211ull

#define ERROR_BUFFER_SIZE VM_MAX_ERROR_MESSAGE_LENGTH
#define PARSER_ARENA_SIZE (1 << 16)
#define NANOSECONDS_PER_MILLISECOND 1000000LL
#define NANOSECONDS_PER_SECOND 1000000000LL

#endif // VM_CONSTANTS_H
