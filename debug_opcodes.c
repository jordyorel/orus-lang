#include "include/vm/vm.h"
#include <stdio.h>

int main() {
    printf("OP_LOAD_I32_CONST = %d (0x%02X)\n", OP_LOAD_I32_CONST, OP_LOAD_I32_CONST);
    printf("OP_MOVE = %d (0x%02X)\n", OP_MOVE, OP_MOVE);
    printf("OP_PRINT_R = %d (0x%02X)\n", OP_PRINT_R, OP_PRINT_R);
    
    // Arithmetic operators
    printf("OP_ADD_I32_TYPED = %d (0x%02X)\n", OP_ADD_I32_TYPED, OP_ADD_I32_TYPED);
    printf("OP_SUB_I32_TYPED = %d (0x%02X)\n", OP_SUB_I32_TYPED, OP_SUB_I32_TYPED);
    printf("OP_MUL_I32_TYPED = %d (0x%02X)\n", OP_MUL_I32_TYPED, OP_MUL_I32_TYPED);
    printf("OP_DIV_I32_TYPED = %d (0x%02X)\n", OP_DIV_I32_TYPED, OP_DIV_I32_TYPED);
    printf("OP_MOD_I32_TYPED = %d (0x%02X)\n", OP_MOD_I32_TYPED, OP_MOD_I32_TYPED);
    
    // Comparison operators
    printf("OP_LT_I32_R = %d (0x%02X)\n", OP_LT_I32_R, OP_LT_I32_R);
    printf("OP_GT_I32_R = %d (0x%02X)\n", OP_GT_I32_R, OP_GT_I32_R);
    printf("OP_EQ_R = %d (0x%02X)\n", OP_EQ_R, OP_EQ_R);
    
    printf("OP_HALT = %d (0x%02X)\n", OP_HALT, OP_HALT);
    return 0;
}