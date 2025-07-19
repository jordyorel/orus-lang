// module_manager.h - Phase 3: Module Register System
#ifndef MODULE_MANAGER_H
#define MODULE_MANAGER_H

#include "vm/vm.h"
#include <stdint.h>
#include <stdbool.h>

// Phase 3: Register module structure with dedicated registers
typedef struct RegisterModule {
    Value registers[MODULE_REGISTERS];    // Module-local registers
    char* module_name;                    // Module identifier
    uint16_t register_count;              // Number of registers in use
    uint8_t module_id;                    // Unique module identifier
    bool is_loaded;                       // Module load status
    
    // Import/Export management
    struct {
        char** exported_names;            // Names of exported variables
        uint16_t* exported_registers;     // Register IDs of exports
        uint16_t export_count;            // Number of exports
    } exports;
    
    struct {
        char** imported_names;            // Names of imported variables
        uint16_t* imported_registers;     // Register IDs of imports
        uint8_t* source_modules;          // Source module IDs
        uint16_t import_count;            // Number of imports
    } imports;
    
    // Module metadata
    struct RegisterModule* next;          // Linked list for management
    uint64_t load_time;                   // When module was loaded
    size_t memory_usage;                  // Memory footprint
} RegisterModule;

// Phase 3: Module Manager
typedef struct ModuleManager {
    RegisterModule* modules;                      // Linked list of loaded modules
    RegisterModule* current_module;               // Currently active module
    uint8_t next_module_id;               // Next available module ID
    uint16_t module_count;                // Number of loaded modules
    
    // Module registry for fast lookup
    struct {
        char** names;                     // Module names
        RegisterModule** modules;                 // Module pointers
        uint16_t capacity;                // Registry capacity
        uint16_t count;                   // Number of registered modules
    } registry;
} ModuleManager;

// Phase 3: Module lifecycle functions
ModuleManager* create_module_manager(void);
void free_module_manager(ModuleManager* manager);

// Phase 3: Module operations
RegisterModule* load_module(ModuleManager* manager, const char* module_name);
void unload_module(ModuleManager* manager, const char* module_name);
RegisterModule* find_module(ModuleManager* manager, const char* module_name);
bool switch_to_module(ModuleManager* manager, const char* module_name);

// Phase 3: Module register allocation
uint16_t allocate_module_register(ModuleManager* manager, const char* module_name);
bool free_module_register(ModuleManager* manager, const char* module_name, uint16_t reg_id);

// Phase 3: Import/Export functionality
bool export_variable(RegisterModule* module, const char* var_name, uint16_t reg_id);
bool import_variable(RegisterModule* dest_module, const char* var_name, RegisterModule* src_module);
uint16_t resolve_import(ModuleManager* manager, const char* module_name, const char* var_name);

// Phase 3: Module register access
Value* get_module_register(ModuleManager* manager, uint8_t module_id, uint16_t reg_offset);
void set_module_register(ModuleManager* manager, uint8_t module_id, uint16_t reg_offset, Value value);

// Phase 3: Module statistics
void get_module_stats(ModuleManager* manager, uint16_t* loaded_modules, uint16_t* total_registers);

#endif // MODULE_MANAGER_H