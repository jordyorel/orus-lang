#include "include/vm/vm.h"
#include <stdio.h>

int main() {
    printf("OP_LOAD_CONST = %d (0x%02X)\n", OP_LOAD_CONST, OP_LOAD_CONST);
    printf("OP_JUMP = %d (0x%02X)\n", OP_JUMP, OP_JUMP);
    printf("OP_JUMP_IF_R = %d (0x%02X)\n", OP_JUMP_IF_R, OP_JUMP_IF_R);
    printf("OP_JUMP_IF_NOT_R = %d (0x%02X)\n", OP_JUMP_IF_NOT_R, OP_JUMP_IF_NOT_R);
    printf("OP_HALT = %d (0x%02X)\n", OP_HALT, OP_HALT);
    printf("OP_GT_I32_R = %d (0x%02X)\n", OP_GT_I32_R, OP_GT_I32_R);
    printf("OP_PRINT_R = %d (0x%02X)\n", OP_PRINT_R, OP_PRINT_R);
    return 0;
}