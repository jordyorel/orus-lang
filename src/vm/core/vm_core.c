//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/vm/core/vm_core.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2022 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Implements the central VM execution loop and core runtime primitives.
//  

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
    initTypeSystem();

    initMemory();

    if (globalStringTable.interned == NULL) {
        init_string_table(&globalStringTable);
    } else if (globalStringTable.threshold == 0) {
        // The table may have been pre-initialized by the caller (e.g. main.c)
        // to guarantee cleanup on early exits. Avoid reinitializing it here to
        // prevent leaking the previously allocated hashmap backing store.
        globalStringTable.threshold = 32;
    }

    init_register_file(&vm.register_file);

    // Legacy register initialization (for backward compatibility)
    for (int i = 0; i < REGISTER_COUNT; i++) {
        vm.registers[i] = BOOL_VAL(false); // Default value instead of NIL_VAL
    }

    memset(&vm.typed_regs, 0, sizeof(TypedRegisters));
    memset(&vm.typed_regs.root_window, 0, sizeof(TypedRegisterWindow));
    vm.typed_regs.root_window.generation = 0;
    typed_window_reset_live_mask(&vm.typed_regs.root_window);
    for (int i = 0; i < TYPED_REGISTER_WINDOW_SIZE; i++) {
        vm.typed_regs.root_window.reg_types[i] = REG_TYPE_NONE;
        vm.typed_regs.root_window.dirty[i] = false;
    }
    vm.typed_regs.root_window.next = NULL;
    vm.typed_regs.active_window = &vm.typed_regs.root_window;
    vm.typed_regs.free_windows = NULL;
    vm.typed_regs.window_version = 0;
    vm.typed_regs.active_depth = 0;
    vm.typed_regs.i32_regs = vm.typed_regs.root_window.i32_regs;
    vm.typed_regs.i64_regs = vm.typed_regs.root_window.i64_regs;
    vm.typed_regs.u32_regs = vm.typed_regs.root_window.u32_regs;
    vm.typed_regs.u64_regs = vm.typed_regs.root_window.u64_regs;
    vm.typed_regs.f64_regs = vm.typed_regs.root_window.f64_regs;
    vm.typed_regs.bool_regs = vm.typed_regs.root_window.bool_regs;
    vm.typed_regs.heap_regs = vm.typed_regs.root_window.heap_regs;
    vm.typed_regs.dirty = vm.typed_regs.root_window.dirty;
    vm.typed_regs.dirty_mask = vm.typed_regs.root_window.dirty_mask;
    vm.typed_regs.reg_types = vm.typed_regs.root_window.reg_types;
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
    for (int i = 0; i < MAX_NATIVES; ++i) {
        vm.nativeFunctions[i].name = NULL;
        vm.nativeFunctions[i].function = NULL;
        vm.nativeFunctions[i].arity = 0;
        vm.nativeFunctions[i].returnType = NULL;
    }
    vm.gcCount = 0;
    vm.lastExecutionTime = 0.0;

    vm.openUpvalues = NULL;

    const char* envTrace = getenv("ORUS_TRACE");
    vm.trace = envTrace && envTrace[0] != '\0';

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

