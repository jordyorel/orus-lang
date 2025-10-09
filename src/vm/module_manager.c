//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: src/vm/module_manager.c
//  Author: Jordy Orel KONDA
//  Copyright (c) 2025 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Manages loading, caching, and lifecycle of VM modules at runtime.


// module_manager.c - Module Register System Implementation
#include "vm/module_manager.h"
#include "vm/vm_constants.h"
#include "runtime/memory.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define MODULE_REGISTRY_INITIAL_CAPACITY 16

static char* duplicate_string(const char* src) {
    if (!src) {
        return NULL;
    }
    size_t len = strlen(src);
    char* copy = (char*)malloc(len + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, src, len + 1);
    return copy;
}

void module_free_export_type(Type* type);


Type* module_clone_export_type(const Type* source) {
    if (!source) {
        return NULL;
    }

    if (source->kind == TYPE_STRUCT || source->kind == TYPE_ENUM) {
        return (Type*)source;
    }

    Type* copy = (Type*)malloc(sizeof(Type));
    if (!copy) {
        return NULL;
    }
    memset(copy, 0, sizeof(Type));
    copy->kind = source->kind;
    copy->ext = NULL;

    switch (source->kind) {
        case TYPE_ARRAY:
            copy->info.array.elementType = module_clone_export_type(source->info.array.elementType);
            break;
        case TYPE_FUNCTION: {
            copy->info.function.arity = source->info.function.arity;
            if (copy->info.function.arity > 0) {
                copy->info.function.paramTypes = (Type**)malloc(sizeof(Type*) * (size_t)copy->info.function.arity);
                if (!copy->info.function.paramTypes) {
                    free(copy);
                    return NULL;
                }
                for (int i = 0; i < copy->info.function.arity; i++) {
                    copy->info.function.paramTypes[i] = module_clone_export_type(source->info.function.paramTypes ?
                                                                                  source->info.function.paramTypes[i] : NULL);
                }
            } else {
                copy->info.function.paramTypes = NULL;
            }
            copy->info.function.returnType = module_clone_export_type(source->info.function.returnType);
            break;
        }
        case TYPE_GENERIC:
            copy->info.generic.paramCount = source->info.generic.paramCount;
            copy->info.generic.name = duplicate_string(source->info.generic.name);
            if (copy->info.generic.paramCount > 0) {
                copy->info.generic.params = (Type**)malloc(sizeof(Type*) * (size_t)copy->info.generic.paramCount);
                if (!copy->info.generic.params) {
                    free(copy->info.generic.name);
                    free(copy);
                    return NULL;
                }
                for (int i = 0; i < copy->info.generic.paramCount; i++) {
                    copy->info.generic.params[i] = module_clone_export_type(source->info.generic.params ?
                                                                             source->info.generic.params[i] : NULL);
                }
            } else {
                copy->info.generic.params = NULL;
            }
            break;
        case TYPE_INSTANCE:
            copy->info.instance.argCount = source->info.instance.argCount;
            copy->info.instance.base = module_clone_export_type(source->info.instance.base);
            if (copy->info.instance.argCount > 0) {
                copy->info.instance.args = (Type**)malloc(sizeof(Type*) * (size_t)copy->info.instance.argCount);
                if (!copy->info.instance.args) {
                    module_free_export_type(copy->info.instance.base);
                    free(copy);
                    return NULL;
                }
                for (int i = 0; i < copy->info.instance.argCount; i++) {
                    copy->info.instance.args[i] = module_clone_export_type(source->info.instance.args ?
                                                                           source->info.instance.args[i] : NULL);
                }
            } else {
                copy->info.instance.args = NULL;
            }
            break;
        case TYPE_VAR:
            copy->info.var.var = NULL;
            break;
        default:
            break;
    }

    return copy;
}

void module_free_export_type(Type* type) {
    if (!type) {
        return;
    }

    if (type->kind == TYPE_STRUCT || type->kind == TYPE_ENUM) {
        return;
    }

    switch (type->kind) {
        case TYPE_ARRAY:
            module_free_export_type(type->info.array.elementType);
            break;
        case TYPE_FUNCTION:
            if (type->info.function.paramTypes) {
                for (int i = 0; i < type->info.function.arity; i++) {
                    module_free_export_type(type->info.function.paramTypes[i]);
                }
                free(type->info.function.paramTypes);
            }
            module_free_export_type(type->info.function.returnType);
            break;
        case TYPE_GENERIC:
            if (type->info.generic.params) {
                for (int i = 0; i < type->info.generic.paramCount; i++) {
                    module_free_export_type(type->info.generic.params[i]);
                }
                free(type->info.generic.params);
            }
            free(type->info.generic.name);
            break;
        case TYPE_INSTANCE:
            module_free_export_type(type->info.instance.base);
            if (type->info.instance.args) {
                for (int i = 0; i < type->info.instance.argCount; i++) {
                    module_free_export_type(type->info.instance.args[i]);
                }
                free(type->info.instance.args);
            }
            break;
        default:
            break;
    }

    free(type);
}

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
                if (current->exports.exported_intrinsics) {
                    free(current->exports.exported_intrinsics[i]);
                }
                if (current->exports.exported_types && i < current->exports.export_count) {
                    module_free_export_type(current->exports.exported_types[i]);
                }
            }
            free(current->exports.exported_names);
        }
        free(current->exports.exported_intrinsics);
        free(current->exports.exported_registers);
        free(current->exports.exported_kinds);
        if (current->exports.exported_types) {
            free(current->exports.exported_types);
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
    module->exports.exported_kinds = NULL;
    module->exports.exported_types = NULL;
    module->exports.exported_intrinsics = NULL;
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

bool module_manager_alias_module(ModuleManager* manager, const char* existing_module_name,
                                 const char* legacy_module_name) {
    if (!manager || !existing_module_name || !legacy_module_name) {
        return false;
    }

    RegisterModule* module = find_module(manager, existing_module_name);
    if (!module) {
        return false;
    }

    if (find_module(manager, legacy_module_name)) {
        return false;
    }

    if (manager->registry.count >= manager->registry.capacity) {
        uint16_t new_capacity = (uint16_t)(manager->registry.capacity * 2);
        char** new_names = (char**)malloc(new_capacity * sizeof(char*));
        RegisterModule** new_modules = (RegisterModule**)malloc(new_capacity * sizeof(RegisterModule*));
        if (!new_names || !new_modules) {
            free(new_names);
            free(new_modules);
            return false;
        }

        for (uint16_t i = 0; i < manager->registry.count; ++i) {
            new_names[i] = manager->registry.names[i];
            new_modules[i] = manager->registry.modules[i];
        }

        free(manager->registry.names);
        free(manager->registry.modules);
        manager->registry.names = new_names;
        manager->registry.modules = new_modules;
        manager->registry.capacity = new_capacity;
    }

    char* alias_copy = duplicate_string(legacy_module_name);
    if (!alias_copy) {
        return false;
    }

    manager->registry.names[manager->registry.count] = alias_copy;
    manager->registry.modules[manager->registry.count] = module;
    manager->registry.count++;

    return true;
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
bool register_module_export(RegisterModule* module, const char* name, ModuleExportKind kind, int register_index,
                            Type* type, const char* intrinsic_symbol) {
    if (!module || !name) {
        return false;
    }

    uint16_t stored_register = MODULE_EXPORT_NO_REGISTER;
    if (register_index >= 0 && register_index < MODULE_EXPORT_NO_REGISTER) {
        stored_register = (uint16_t)register_index;
    }

    Type* adopted_type = type;
    char* intrinsic_copy = NULL;
    if (intrinsic_symbol) {
        intrinsic_copy = duplicate_string(intrinsic_symbol);
        if (!intrinsic_copy) {
            return false;
        }
    }

    // Update existing entry if it already exists
    for (uint16_t i = 0; i < module->exports.export_count; i++) {
        if (module->exports.exported_names[i] && strcmp(module->exports.exported_names[i], name) == 0) {
            module->exports.exported_registers[i] = stored_register;
            module->exports.exported_kinds[i] = kind;
            if (module->exports.exported_types) {
                module_free_export_type(module->exports.exported_types[i]);
                module->exports.exported_types[i] = adopted_type;
            } else if (adopted_type) {
                module->exports.exported_types = (Type**)calloc(module->exports.export_count, sizeof(Type*));
                if (!module->exports.exported_types) {
                    free(intrinsic_copy);
                    return false;
                }
                module->exports.exported_types[i] = adopted_type;
            }
            if (module->exports.exported_intrinsics) {
                free(module->exports.exported_intrinsics[i]);
                module->exports.exported_intrinsics[i] = intrinsic_copy;
            } else if (intrinsic_copy) {
                module->exports.exported_intrinsics = (char**)calloc(module->exports.export_count, sizeof(char*));
                if (!module->exports.exported_intrinsics) {
                    free(intrinsic_copy);
                    return false;
                }
                module->exports.exported_intrinsics[i] = intrinsic_copy;
            } else {
                free(intrinsic_copy);
            }
            return true;
        }
    }

    uint16_t new_count = module->exports.export_count + 1;

    char** names = (char**)realloc(module->exports.exported_names, new_count * sizeof(char*));
    if (!names) {
        free(intrinsic_copy);
        return false;
    }
    module->exports.exported_names = names;

    uint16_t* registers = (uint16_t*)realloc(module->exports.exported_registers, new_count * sizeof(uint16_t));
    if (!registers) {
        free(intrinsic_copy);
        return false;
    }
    module->exports.exported_registers = registers;

    ModuleExportKind* kinds = (ModuleExportKind*)realloc(module->exports.exported_kinds, new_count * sizeof(ModuleExportKind));
    if (!kinds) {
        free(intrinsic_copy);
        return false;
    }
    module->exports.exported_kinds = kinds;

    Type** types = (Type**)realloc(module->exports.exported_types, new_count * sizeof(Type*));
    if (!types) {
        free(intrinsic_copy);
        return false;
    }
    module->exports.exported_types = types;

    char** intrinsics = (char**)realloc(module->exports.exported_intrinsics, new_count * sizeof(char*));
    if (!intrinsics) {
        free(intrinsic_copy);
        return false;
    }
    module->exports.exported_intrinsics = intrinsics;

    char* copy = (char*)malloc(strlen(name) + 1);
    if (!copy) {
        free(intrinsic_copy);
        return false;
    }
    strcpy(copy, name);

    module->exports.exported_names[module->exports.export_count] = copy;
    module->exports.exported_registers[module->exports.export_count] = stored_register;
    module->exports.exported_kinds[module->exports.export_count] = kind;
    module->exports.exported_types[module->exports.export_count] = adopted_type;
    module->exports.exported_intrinsics[module->exports.export_count] = intrinsic_copy;
    module->exports.export_count = new_count;

    return true;
}

bool module_manager_resolve_export(ModuleManager* manager, const char* module_name, const char* symbol_name,
                                   ModuleExportKind* out_kind, uint16_t* out_register, Type** out_type) {
    if (!manager || !module_name || !symbol_name) {
        return false;
    }

    RegisterModule* module = find_module(manager, module_name);
    if (!module) {
        return false;
    }

    for (uint16_t i = 0; i < module->exports.export_count; i++) {
        if (module->exports.exported_names[i] &&
            strcmp(module->exports.exported_names[i], symbol_name) == 0) {
            if (out_kind) {
                *out_kind = module->exports.exported_kinds[i];
            }
            if (out_register) {
                *out_register = module->exports.exported_registers[i];
            }
            if (out_type) {
                if (module->exports.exported_types && i < module->exports.export_count) {
                    *out_type = module->exports.exported_types[i];
                } else {
                    *out_type = NULL;
                }
            }
            return true;
        }
    }

    return false;
}

// Phase 3: Import variable into module
bool import_variable(RegisterModule* dest_module, const char* var_name, RegisterModule* src_module) {
    if (!dest_module || !var_name || !src_module) return false;
    
    // Find exported variable in source module
    uint16_t src_reg_id = 0;
    bool found = false;
    for (uint16_t i = 0; i < src_module->exports.export_count; i++) {
        if (strcmp(src_module->exports.exported_names[i], var_name) == 0) {
            uint16_t candidate = src_module->exports.exported_registers[i];
            if (candidate == MODULE_EXPORT_NO_REGISTER) {
                continue;
            }
            src_reg_id = candidate;
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

uint16_t resolve_import(ModuleManager* manager, const char* module_name, const char* var_name) {
    ModuleExportKind kind = MODULE_EXPORT_KIND_GLOBAL;
    uint16_t reg = MODULE_EXPORT_NO_REGISTER;
    if (!module_manager_resolve_export(manager, module_name, var_name, &kind, &reg, NULL)) {
        return MODULE_EXPORT_NO_REGISTER;
    }
    return reg;
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