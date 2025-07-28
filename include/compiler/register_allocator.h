#ifndef REGISTER_ALLOCATOR_H
#define REGISTER_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>

// Multi-pass compiler register ranges (custom for our compiler)
#define MP_GLOBAL_REG_START    0     // R0-R63: Global variables
#define MP_GLOBAL_REG_END      63
#define MP_FRAME_REG_START     64    // R64-R191: Function locals/params  
#define MP_FRAME_REG_END       191
#define MP_TEMP_REG_START      192   // R192-R239: Expression temps
#define MP_TEMP_REG_END        239
#define MP_MODULE_REG_START    240   // R240-R255: Module scope
#define MP_MODULE_REG_END      255

typedef struct MultiPassRegisterAllocator {
    // Register usage tracking
    bool global_regs[64];      // R0-R63 usage
    bool frame_regs[128];      // R64-R191 usage  
    bool temp_regs[48];        // R192-R239 usage
    bool module_regs[16];      // R240-R255 usage
    
    // Allocation state
    int next_global;           // Next available global register
    int next_frame;            // Next available frame register
    int next_temp;             // Next available temp register
    int next_module;           // Next available module register
    
    // Stack for temp register reuse
    int temp_stack[48];        // Reusable temp registers
    int temp_stack_top;        // Top of temp stack
} MultiPassRegisterAllocator;

// Core allocation functions
MultiPassRegisterAllocator* init_mp_register_allocator(void);
void free_mp_register_allocator(MultiPassRegisterAllocator* allocator);

// Register allocation by type
int mp_allocate_global_register(MultiPassRegisterAllocator* allocator);
int mp_allocate_frame_register(MultiPassRegisterAllocator* allocator);  
int mp_allocate_temp_register(MultiPassRegisterAllocator* allocator);
int mp_allocate_module_register(MultiPassRegisterAllocator* allocator);

// Register deallocation
void mp_free_register(MultiPassRegisterAllocator* allocator, int reg);
void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg);

// Utilities
bool mp_is_register_free(MultiPassRegisterAllocator* allocator, int reg);
const char* mp_register_type_name(int reg);

#endif // REGISTER_ALLOCATOR_H