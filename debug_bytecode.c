// Simple bytecode dumper to verify format
#include <stdio.h>
#include "include/vm/vm.h"

void dumpBytecode(Chunk* chunk) {
    printf("=== BYTECODE DUMP ===\n");
    printf("Instructions: %d\n", chunk->count);
    
    for (int i = 0; i < chunk->count; i++) {
        printf("%04d: %02X", i, chunk->code[i]);
        
        // Try to identify opcodes
        switch (chunk->code[i]) {
            case OP_LOAD_I32_CONST:
                printf(" (OP_LOAD_I32_CONST)");
                if (i + 3 < chunk->count) {
                    printf(" reg=%d, value=%d", chunk->code[i+1], 
                           (chunk->code[i+2] << 8) | chunk->code[i+3]);
                    i += 3;
                }
                break;
            case OP_GT_I32_R:
                printf(" (OP_GT_I32_R)");
                if (i + 3 < chunk->count) {
                    printf(" dst=%d, src1=%d, src2=%d", 
                           chunk->code[i+1], chunk->code[i+2], chunk->code[i+3]);
                    i += 3;
                }
                break;
            case OP_PRINT_R:
                printf(" (OP_PRINT_R)");
                if (i + 1 < chunk->count) {
                    printf(" reg=%d", chunk->code[i+1]);
                    i += 1;
                }
                break;
            case OP_JUMP_IF_NOT_R:
                printf(" (OP_JUMP_IF_NOT_R)");
                if (i + 3 < chunk->count) {
                    printf(" reg=%d, offset=%d", chunk->code[i+1],
                           (chunk->code[i+2] << 8) | chunk->code[i+3]);
                    i += 3;
                }
                break;
            default:
                printf(" (UNKNOWN_%02X)", chunk->code[i]);
                break;
        }
        printf("\n");
    }
    printf("=== END BYTECODE ===\n");
}