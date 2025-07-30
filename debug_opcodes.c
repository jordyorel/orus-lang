#include "include/vm/vm.h"
#include <stdio.h>

int main() {
    printf("OP_LOAD_I64_CONST = %d (0x%02X)\n", OP_LOAD_I64_CONST, OP_LOAD_I64_CONST);
    printf("OP_MOVE = %d (0x%02X)\n", OP_MOVE, OP_MOVE);
    printf("OP_PRINT_R = %d (0x%02X)\n", OP_PRINT_R, OP_PRINT_R);
    
    // I64 Arithmetic operators (the critical ones)
    printf("OP_ADD_I64_TYPED = %d (0x%02X)\n", OP_ADD_I64_TYPED, OP_ADD_I64_TYPED);
    printf("OP_SUB_I64_TYPED = %d (0x%02X)\n", OP_SUB_I64_TYPED, OP_SUB_I64_TYPED);
    printf("OP_MUL_I64_TYPED = %d (0x%02X)\n", OP_MUL_I64_TYPED, OP_MUL_I64_TYPED);
    
    // For comparison - I32 arithmetic
    printf("OP_ADD_I32_TYPED = %d (0x%02X)\n", OP_ADD_I32_TYPED, OP_ADD_I32_TYPED);
    
    printf("OP_HALT = %d (0x%02X)\n", OP_HALT, OP_HALT);
    
    printf("\nWhat we see in trace: 0x83 = %d\n", 0x83);
    printf("What codegen thinks it's emitting: OP_ADD_I64_TYPED = %d\n", OP_ADD_I64_TYPED);
    
    return 0;
}