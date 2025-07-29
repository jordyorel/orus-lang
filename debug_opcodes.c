#include "include/vm/vm.h"
#include <stdio.h>

int main() {
    printf("OP_LOAD_I32_CONST = %d (0x%02X)\n", OP_LOAD_I32_CONST, OP_LOAD_I32_CONST);
    printf("OP_PRINT_R = %d (0x%02X)\n", OP_PRINT_R, OP_PRINT_R);
    printf("OP_HALT = %d (0x%02X)\n", OP_HALT, OP_HALT);
    return 0;
}