// vm_core.c - VM initialization and core management
#include "vm_internal.h"
#include "runtime/builtins.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"
#include "vm/register_file.h"
#include "type/type.h"
#include <string.h>

VM vm; // Global VM instance

void initVM(void) {
    initTypeSystem();
    initMemory();
    init_string_table(&globalStringTable);

    // Phase 1: Initialize new register file architecture
    init_register_file(&vm.register_file);

    // Legacy register initialization (for backward compatibility)
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = NIL_VAL;
    }

    memset(&vm.typed_regs, 0, sizeof(TypedRegisters));
    for (int i = 0; i < 32; i++) {
        vm.typed_regs.heap_regs[i] = NIL_VAL;
    }
    for (int i = 0; i < 256; i++) {
        vm.typed_regs.reg_types[i] = REG_TYPE_NONE;
    }

    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.globals[i] = NIL_VAL;
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
        vm.mutableGlobals[i] = false;
        vm.variableNames[i].name = NULL;
        vm.variableNames[i].length = 0;
    }

    vm.variableCount = 0;
    vm.functionCount = 0;
    vm.frameCount = 0;
    vm.tryFrameCount = 0;
    vm.lastError = NIL_VAL;
    vm.instruction_count = 0;
    vm.astRoot = NULL;
    vm.filePath = NULL;
    vm.currentLine = 0;
    vm.currentColumn = 1;
    vm.moduleCount = 0;
    vm.nativeFunctionCount = 0;
    vm.gcCount = 0;
    vm.lastExecutionTime = 0.0;
    memset(&vm.profile, 0, sizeof(VMProfile));

    vm.openUpvalues = NULL;

    const char* envTrace = getenv("ORUS_TRACE");
    vm.trace = envTrace && envTrace[0] != '\0';

    vm.chunk = NULL;
    vm.ip = NULL;
}

void freeVM(void) {
    // Phase 1: Free register file resources
    free_register_file(&vm.register_file);
    
    // Free global string table
    free_string_table(&globalStringTable);
    
    freeObjects();
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.variableNames[i].name = NULL;
        vm.globalTypes[i] = NULL;
        vm.publicGlobals[i] = false;
        vm.mutableGlobals[i] = false;
    }
    vm.astRoot = NULL;
    vm.chunk = NULL;
    vm.ip = NULL;
}

