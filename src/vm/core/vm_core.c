/*
 * Orus Language Project
 * ---------------------------------------------------------------------------
 * File: src/vm/core/vm_core.c
 * Author: Jordy Orel KONDA
 * Copyright (c) 2025 Jordy Orel KONDA
 * License: MIT License (see LICENSE file in the project root)
 * Description: Implements the central VM execution loop and core runtime primitives.
 */

// vm_core.c - VM initialization and core management
#include "vm_internal.h"
#include "runtime/builtins.h"
#include "runtime/memory.h"
#include "vm/vm_string_ops.h"
#include "vm/register_file.h"
#include "type/type.h"
#include <string.h>
#include <stdlib.h>

VM vm; // Global VM instance

void initVM(void) {
    // printf("[VM_CORE_TRACE] initVM() starting\n");
    // fflush(stdout);
    
    // printf("[VM_CORE_TRACE] About to call initTypeSystem()\n");
    // fflush(stdout);
    initTypeSystem();
    // printf("[VM_CORE_TRACE] initTypeSystem() completed\n");
    // fflush(stdout);
    
    // printf("[VM_CORE_TRACE] About to call initMemory()\n");
    // fflush(stdout);
    initMemory();
    // printf("[VM_CORE_TRACE] initMemory() completed\n");
    // fflush(stdout);
    
    // printf("[VM_CORE_TRACE] About to call init_string_table()\n");
    // fflush(stdout);
    init_string_table(&globalStringTable);
    // printf("[VM_CORE_TRACE] init_string_table() completed\n");
    // fflush(stdout);

    // Phase 1: Initialize new register file architecture
    init_register_file(&vm.register_file);

    // Legacy register initialization (for backward compatibility)
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
    }

    memset(&vm.typed_regs, 0, sizeof(TypedRegisters));
    for (int i = 0; i < 32; i++) {
        vm.typed_regs.heap_regs[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
    }
    for (int i = 0; i < 256; i++) {
        vm.typed_regs.reg_types[i] = REG_TYPE_NONE;
    }
    memset(vm.typed_iterators, 0, sizeof(vm.typed_iterators));

    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.globals[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
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
    vm.lastError = BOOL_VAL(false); // Default value instead of NIL_VAL
    vm_set_error_report_pending(false);
    vm.instruction_count = 0;
    vm.astRoot = NULL;
    vm.filePath = NULL;
    vm.currentLine = 0;
    vm.currentColumn = 1;
    vm.moduleCount = 0;
    vm.loadingModuleCount = 0;
    for (int i = 0; i < UINT8_COUNT; i++) {
        vm.loadingModules[i] = NULL;
    }
    vm.nativeFunctionCount = 0;
    vm.gcCount = 0;
    vm.lastExecutionTime = 0.0;
    memset(&vm.profile, 0, sizeof(VMProfile));
    vm_reset_loop_trace();

    vm.openUpvalues = NULL;

    const char* envTrace = getenv("ORUS_TRACE");
    vm.trace = envTrace && envTrace[0] != '\0';

    vm.config.trace_typed_fallbacks = false;
#ifdef VM_TRACE_TYPED_FALLBACKS
    vm.config.trace_typed_fallbacks = true;
#endif
    vm.config.enable_bool_branch_fastpath = true;
    vm.config.disable_inc_typed_fastpath = false;
    vm.config.force_boxed_iterators = false;
    vm.config.enable_licm_typed_metadata = false;

    const char* disable_bool_fast_env = getenv("ORUS_DISABLE_BOOL_BRANCH_FASTPATH");
    if (disable_bool_fast_env && disable_bool_fast_env[0] && disable_bool_fast_env[0] != '0') {
        vm.config.enable_bool_branch_fastpath = false;
    }

    const char* bool_fast_env = getenv("ORUS_EXPERIMENT_BOOL_BRANCH_FASTPATH");
    if (bool_fast_env && bool_fast_env[0] && bool_fast_env[0] != '0') {
        vm.config.enable_bool_branch_fastpath = true;
    }

    const char* disable_inc_env = getenv("ORUS_DISABLE_INC_TYPED_FASTPATH");
    if (disable_inc_env && disable_inc_env[0] && disable_inc_env[0] != '0') {
        vm.config.disable_inc_typed_fastpath = true;
    }

    const char* force_boxed_env = getenv("ORUS_FORCE_BOXED_ITERATORS");
    if (force_boxed_env && force_boxed_env[0] && force_boxed_env[0] != '0') {
        vm.config.force_boxed_iterators = true;
    }

    const char* licm_typed_env = getenv("ORUS_ENABLE_LICM_TYPED_GUARDS");
    if (licm_typed_env && licm_typed_env[0] && licm_typed_env[0] != '0') {
        vm.config.enable_licm_typed_metadata = true;
    }

    vm.chunk = NULL;
    vm.ip = NULL;
    vm.isShuttingDown = false;  // Initialize shutdown flag
    
    // Initialize register file frame pointers
    vm.register_file.current_frame = NULL;
    vm.register_file.frame_stack = NULL;
}

void freeVM(void) {
    // Set shutdown flag to indicate VM is in cleanup phase
    vm.isShuttingDown = true;
    
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

