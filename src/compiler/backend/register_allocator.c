#include "compiler/register_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

MultiPassRegisterAllocator* init_mp_register_allocator(void) {
    MultiPassRegisterAllocator* allocator = malloc(sizeof(MultiPassRegisterAllocator));
    if (!allocator) return NULL;
    
    // Initialize all registers as free
    memset(allocator->global_regs, false, sizeof(allocator->global_regs));
    memset(allocator->frame_regs, false, sizeof(allocator->frame_regs)); 
    memset(allocator->temp_regs, false, sizeof(allocator->temp_regs));
    memset(allocator->module_regs, false, sizeof(allocator->module_regs));
    
    // Initialize allocation pointers
    allocator->next_global = MP_GLOBAL_REG_START;
    allocator->next_frame = MP_FRAME_REG_START;
    allocator->next_temp = MP_TEMP_REG_START;
    allocator->next_module = MP_MODULE_REG_START;
    
    // Initialize temp stack for register reuse
    allocator->temp_stack_top = -1;
    
    return allocator;
}

void free_mp_register_allocator(MultiPassRegisterAllocator* allocator) {
    if (allocator) {
        free(allocator);
    }
}

int mp_allocate_global_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free global register
    for (int i = 0; i < 64; i++) {
        if (!allocator->global_regs[i]) {
            allocator->global_regs[i] = true;
            return MP_GLOBAL_REG_START + i;
        }
    }
    
    // No free global registers
    printf("[REGISTER_ALLOCATOR] Warning: No free global registers\n");
    return -1;
}

int mp_allocate_frame_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free frame register
    for (int i = 0; i < 128; i++) {
        if (!allocator->frame_regs[i]) {
            allocator->frame_regs[i] = true;
            return MP_FRAME_REG_START + i;
        }
    }
    
    // No free frame registers
    printf("[REGISTER_ALLOCATOR] Warning: No free frame registers\n");
    return -1;
}

int mp_allocate_temp_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Try to reuse from stack first (for register recycling)
    if (allocator->temp_stack_top >= 0) {
        int reused_reg = allocator->temp_stack[allocator->temp_stack_top--];
        printf("[REGISTER_ALLOCATOR] Reusing temp register R%d\n", reused_reg);
        return reused_reg;
    }
    
    // Find next free temp register
    for (int i = 0; i < 48; i++) {
        if (!allocator->temp_regs[i]) {
            allocator->temp_regs[i] = true;
            int reg = MP_TEMP_REG_START + i;
            printf("[REGISTER_ALLOCATOR] Allocated temp register R%d\n", reg);
            return reg;
        }
    }
    
    // No free temp registers
    printf("[REGISTER_ALLOCATOR] Error: No free temp registers (register spill needed)\n");
    return -1;
}

int mp_allocate_module_register(MultiPassRegisterAllocator* allocator) {
    if (!allocator) return -1;
    
    // Find next free module register
    for (int i = 0; i < 16; i++) {
        if (!allocator->module_regs[i]) {
            allocator->module_regs[i] = true;
            return MP_MODULE_REG_START + i;
        }
    }
    
    // No free module registers
    printf("[REGISTER_ALLOCATOR] Warning: No free module registers\n");
    return -1;
}

void mp_free_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) return;
    
    // Determine register type and free accordingly
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) {
        int index = reg - MP_GLOBAL_REG_START;
        allocator->global_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed global register R%d\n", reg);
    }
    else if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) {
        int index = reg - MP_FRAME_REG_START;
        allocator->frame_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed frame register R%d\n", reg);
    }
    else if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
        mp_free_temp_register(allocator, reg);
    }
    else if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) {
        int index = reg - MP_MODULE_REG_START;
        allocator->module_regs[index] = false;
        printf("[REGISTER_ALLOCATOR] Freed module register R%d\n", reg);
    }
    else {
        printf("[REGISTER_ALLOCATOR] Warning: Invalid register R%d cannot be freed\n", reg);
    }
}

void mp_free_temp_register(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator || reg < MP_TEMP_REG_START || reg > MP_TEMP_REG_END) {
        printf("[REGISTER_ALLOCATOR] Warning: Invalid temp register R%d\n", reg);
        return;
    }
    
    int index = reg - MP_TEMP_REG_START;
    allocator->temp_regs[index] = false;
    
    // Add to reuse stack (LIFO for better cache locality)
    if (allocator->temp_stack_top < 47) {
        allocator->temp_stack[++allocator->temp_stack_top] = reg;
        printf("[REGISTER_ALLOCATOR] Freed temp register R%d (added to reuse stack)\n", reg);
    } else {
        printf("[REGISTER_ALLOCATOR] Freed temp register R%d (reuse stack full)\n", reg);
    }
}

bool mp_is_register_free(MultiPassRegisterAllocator* allocator, int reg) {
    if (!allocator) return false;
    
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) {
        return !allocator->global_regs[reg - MP_GLOBAL_REG_START];
    }
    else if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) {
        return !allocator->frame_regs[reg - MP_FRAME_REG_START];
    }
    else if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) {
        return !allocator->temp_regs[reg - MP_TEMP_REG_START];
    }
    else if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) {
        return !allocator->module_regs[reg - MP_MODULE_REG_START];
    }
    
    return false;  // Invalid register
}

const char* mp_register_type_name(int reg) {
    if (reg >= MP_GLOBAL_REG_START && reg <= MP_GLOBAL_REG_END) return "GLOBAL";
    if (reg >= MP_FRAME_REG_START && reg <= MP_FRAME_REG_END) return "FRAME";  
    if (reg >= MP_TEMP_REG_START && reg <= MP_TEMP_REG_END) return "TEMP";
    if (reg >= MP_MODULE_REG_START && reg <= MP_MODULE_REG_END) return "MODULE";
    return "INVALID";
}