// module_manager.c - Module Register System Implementation
#include "vm/module_manager.h"
#include "vm/vm_constants.h"
#include "runtime/memory.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MODULE_REGISTRY_INITIAL_CAPACITY 16

// Phase 3: Create module manager
ModuleManager* create_module_manager(void) {
    ModuleManager* manager = (ModuleManager*)malloc(sizeof(ModuleManager));
    if (!manager) return NULL;
    
    manager->modules = NULL;
    manager->current_module = NULL;
    manager->next_module_id = 1; // ID 0 reserved for global scope
    manager->module_count = 0;
    
    // Initialize registry
    manager->registry.capacity = MODULE_REGISTRY_INITIAL_CAPACITY;
    manager->registry.names = (char**)calloc(manager->registry.capacity, sizeof(char*));
    manager->registry.modules = (RegisterModule**)calloc(manager->registry.capacity, sizeof(RegisterModule*));
    manager->registry.count = 0;
    
    return manager;
}

// Phase 3: Free module manager
void free_module_manager(ModuleManager* manager) {
    if (!manager) return;
    
    // Free all modules
    RegisterModule* current = manager->modules;
    while (current) {
        RegisterModule* next = current->next;
        
        // Free module exports
        if (current->exports.exported_names) {
            for (uint16_t i = 0; i < current->exports.export_count; i++) {
                free(current->exports.exported_names[i]);
            }
            free(current->exports.exported_names);
            free(current->exports.exported_registers);
        }
        
        // Free module imports
        if (current->imports.imported_names) {
            for (uint16_t i = 0; i < current->imports.import_count; i++) {
                free(current->imports.imported_names[i]);
            }
            free(current->imports.imported_names);
            free(current->imports.imported_registers);
            free(current->imports.source_modules);
        }
        
        free(current->module_name);
        free(current);
        current = next;
    }
    
    // Free registry
    if (manager->registry.names) {
        for (uint16_t i = 0; i < manager->registry.count; i++) {
            free(manager->registry.names[i]);
        }
        free(manager->registry.names);
        free(manager->registry.modules);
    }
    
    free(manager);
}

// Phase 3: Load a new module
RegisterModule* load_module(ModuleManager* manager, const char* module_name) {
    if (!manager || !module_name) return NULL;
    
    // Check if module already exists
    RegisterModule* existing = find_module(manager, module_name);
    if (existing) return existing;
    
    // Create new module
    RegisterModule* module = (RegisterModule*)malloc(sizeof(RegisterModule));
    if (!module) return NULL;
    
    // Initialize module
    for (int i = 0; i < MODULE_REGISTERS; i++) {
        module->registers[i] = BOOL_VAL(false);
    }
    
    module->module_name = (char*)malloc(strlen(module_name) + 1);
    strcpy(module->module_name, module_name);
    module->register_count = 0;
    module->module_id = manager->next_module_id++;
    module->is_loaded = true;
    
    // Initialize exports
    module->exports.exported_names = NULL;
    module->exports.exported_registers = NULL;
    module->exports.export_count = 0;
    
    // Initialize imports
    module->imports.imported_names = NULL;
    module->imports.imported_registers = NULL;
    module->imports.source_modules = NULL;
    module->imports.import_count = 0;
    
    module->next = manager->modules;
    module->load_time = 0; // TODO: Add timestamp
    module->memory_usage = sizeof(RegisterModule);
    
    // Add to module list
    manager->modules = module;
    manager->module_count++;
    
    // Add to registry (resize if needed)
    if (manager->registry.count >= manager->registry.capacity) {
        manager->registry.capacity *= 2;
        manager->registry.names = (char**)realloc(manager->registry.names, 
                                                 manager->registry.capacity * sizeof(char*));
        manager->registry.modules = (RegisterModule**)realloc(manager->registry.modules,
                                                     manager->registry.capacity * sizeof(RegisterModule*));
    }
    
    manager->registry.names[manager->registry.count] = (char*)malloc(strlen(module_name) + 1);
    strcpy(manager->registry.names[manager->registry.count], module_name);
    manager->registry.modules[manager->registry.count] = module;
    manager->registry.count++;
    
    return module;
}

// Phase 3: Find module by name
RegisterModule* find_module(ModuleManager* manager, const char* module_name) {
    if (!manager || !module_name) return NULL;
    
    // Fast lookup in registry
    for (uint16_t i = 0; i < manager->registry.count; i++) {
        if (strcmp(manager->registry.names[i], module_name) == 0) {
            return manager->registry.modules[i];
        }
    }
    
    return NULL;
}

// Phase 3: Switch active module context
bool switch_to_module(ModuleManager* manager, const char* module_name) {
    if (!manager || !module_name) return false;
    
    RegisterModule* module = find_module(manager, module_name);
    if (!module) return false;
    
    manager->current_module = module;
    return true;
}

// Phase 3: Allocate register in specific module
uint16_t allocate_module_register(ModuleManager* manager, const char* module_name) {
    if (!manager || !module_name) return 0;
    
    RegisterModule* module = find_module(manager, module_name);
    if (!module) return 0;
    
    if (module->register_count >= MODULE_REGISTERS) {
        return 0; // Module registers exhausted
    }
    
    uint16_t reg_id = MODULE_REG_START + (module->module_id * MODULE_REGISTERS) + module->register_count;
    module->register_count++;
    
    return reg_id;
}

// Phase 3: Export variable from module
bool export_variable(RegisterModule* module, const char* var_name, uint16_t reg_id) {
    if (!module || !var_name) return false;
    
    // Resize export arrays if needed
    module->exports.exported_names = (char**)realloc(module->exports.exported_names,
                                                    (module->exports.export_count + 1) * sizeof(char*));
    module->exports.exported_registers = (uint16_t*)realloc(module->exports.exported_registers,
                                                           (module->exports.export_count + 1) * sizeof(uint16_t));
    
    // Add export
    module->exports.exported_names[module->exports.export_count] = (char*)malloc(strlen(var_name) + 1);
    strcpy(module->exports.exported_names[module->exports.export_count], var_name);
    module->exports.exported_registers[module->exports.export_count] = reg_id;
    module->exports.export_count++;
    
    return true;
}

// Phase 3: Import variable into module
bool import_variable(RegisterModule* dest_module, const char* var_name, RegisterModule* src_module) {
    if (!dest_module || !var_name || !src_module) return false;
    
    // Find exported variable in source module
    uint16_t src_reg_id = 0;
    bool found = false;
    for (uint16_t i = 0; i < src_module->exports.export_count; i++) {
        if (strcmp(src_module->exports.exported_names[i], var_name) == 0) {
            src_reg_id = src_module->exports.exported_registers[i];
            found = true;
            break;
        }
    }
    
    if (!found) return false;
    
    // Resize import arrays if needed
    dest_module->imports.imported_names = (char**)realloc(dest_module->imports.imported_names,
                                                         (dest_module->imports.import_count + 1) * sizeof(char*));
    dest_module->imports.imported_registers = (uint16_t*)realloc(dest_module->imports.imported_registers,
                                                                (dest_module->imports.import_count + 1) * sizeof(uint16_t));
    dest_module->imports.source_modules = (uint8_t*)realloc(dest_module->imports.source_modules,
                                                           (dest_module->imports.import_count + 1) * sizeof(uint8_t));
    
    // Add import
    dest_module->imports.imported_names[dest_module->imports.import_count] = (char*)malloc(strlen(var_name) + 1);
    strcpy(dest_module->imports.imported_names[dest_module->imports.import_count], var_name);
    dest_module->imports.imported_registers[dest_module->imports.import_count] = src_reg_id;
    dest_module->imports.source_modules[dest_module->imports.import_count] = src_module->module_id;
    dest_module->imports.import_count++;
    
    return true;
}

// Phase 3: Get module register access
Value* get_module_register(ModuleManager* manager, uint8_t module_id, uint16_t reg_offset) {
    if (!manager) return NULL;
    
    // Find module by ID
    RegisterModule* current = manager->modules;
    while (current) {
        if (current->module_id == module_id) {
            if (reg_offset < MODULE_REGISTERS) {
                return &current->registers[reg_offset];
            }
            break;
        }
        current = current->next;
    }
    
    return NULL;
}

// Phase 3: Set module register value
void set_module_register(ModuleManager* manager, uint8_t module_id, uint16_t reg_offset, Value value) {
    Value* target = get_module_register(manager, module_id, reg_offset);
    if (target) {
        *target = value;
    }
}

// Phase 3: Get module statistics
void get_module_stats(ModuleManager* manager, uint16_t* loaded_modules, uint16_t* total_registers) {
    if (!manager) return;
    
    if (loaded_modules) *loaded_modules = manager->module_count;
    
    if (total_registers) {
        uint16_t total = 0;
        RegisterModule* current = manager->modules;
        while (current) {
            total += current->register_count;
            current = current->next;
        }
        *total_registers = total;
    }
}