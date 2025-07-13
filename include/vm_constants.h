// vm_constants.h - Shared VM configuration constants
#ifndef VM_CONSTANTS_H
#define VM_CONSTANTS_H

// Call stack limits
#define VM_MAX_CALL_FRAMES 256
#define VM_MAX_REGISTERS 256
#define VM_MAX_UPVALUES 256

// String operation thresholds
#define VM_SMALL_STRING_BUFFER 1024
#define VM_LARGE_STRING_THRESHOLD 4096

// Performance tuning
#define VM_DISPATCH_TABLE_SIZE (OP_HALT + 1)
#define VM_TYPED_REGISTER_COUNT 64

// Error handling
#define VM_MAX_ERROR_MESSAGE_LENGTH 256
#define VM_MAX_STACK_TRACE_DEPTH 32

#endif // VM_CONSTANTS_H
