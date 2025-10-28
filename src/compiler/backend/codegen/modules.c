#include "compiler/codegen/modules.h"
#include "compiler/codegen/codegen_internal.h"
#include "compiler/register_allocator.h"
#include "errors/features/variable_errors.h"
#include "internal/error_reporting.h"
#include "internal/strutil.h"
#include "type/type.h"
#include "vm/module_manager.h"
#include <stdlib.h>
#include <string.h>

static ModuleExportEntry* find_module_export_entry(const CompilerContext* ctx, const char* name) {
    if (!ctx || !name) {
        return NULL;
    }
    for (int i = 0; i < ctx->module_export_count; i++) {
        if (ctx->module_exports[i].name && strcmp(ctx->module_exports[i].name, name) == 0) {
            return &ctx->module_exports[i];
        }
    }
    return NULL;
}

void record_module_export(CompilerContext* ctx, const char* name, ModuleExportKind kind, Type* type) {
    if (!ctx || !ctx->is_module || !name) {
        return;
    }

    ModuleExportEntry* existing = find_module_export_entry(ctx, name);
    if (existing) {
        if (type && !existing->type) {
            Type* cloned = module_clone_export_type(type);
            if (cloned) {
                existing->type = cloned;
            }
        }
        return;
    }

    if (ctx->module_export_count >= ctx->module_export_capacity) {
        int new_cap = ctx->module_export_capacity == 0 ? 4 : ctx->module_export_capacity * 2;
        ModuleExportEntry* new_exports = realloc(ctx->module_exports, sizeof(ModuleExportEntry) * new_cap);
        if (!new_exports) {
            return;
        }
        ctx->module_exports = new_exports;
        ctx->module_export_capacity = new_cap;
    }

    char* copy = orus_strdup(name);
    if (!copy) {
        return;
    }

    ctx->module_exports[ctx->module_export_count].name = copy;
    ctx->module_exports[ctx->module_export_count].kind = kind;
    ctx->module_exports[ctx->module_export_count].register_index = -1;
    Type* cloned_type = NULL;
    if (type) {
        cloned_type = module_clone_export_type(type);
    }

    ctx->module_exports[ctx->module_export_count].type = cloned_type;
    ctx->module_exports[ctx->module_export_count].function_index = -1;
    ctx->module_export_count++;
}

void set_module_export_metadata(CompilerContext* ctx, const char* name, int reg, Type* type) {
    if (!ctx || !ctx->is_module || !name || reg < 0) {
        return;
    }

    ModuleExportEntry* entry = find_module_export_entry(ctx, name);
    if (!entry) {
        return;
    }

    entry->register_index = reg;
    if (type && !entry->type) {
        Type* cloned = module_clone_export_type(type);
        if (cloned) {
            entry->type = cloned;
        }
    }
}

void set_module_export_function_index(CompilerContext* ctx, const char* name, int function_index) {
    if (!ctx || !ctx->is_module || !name || function_index < 0) {
        return;
    }

    ModuleExportEntry* entry = find_module_export_entry(ctx, name);
    if (!entry) {
        return;
    }

    entry->function_index = function_index;
}

static bool module_import_exists(const CompilerContext* ctx, const char* module_name, const char* symbol_name) {
    if (!ctx) {
        return false;
    }

    for (int i = 0; i < ctx->module_import_count; i++) {
        ModuleImportEntry* entry = &ctx->module_imports[i];
        bool module_match = (!module_name && !entry->module_name) ||
                            (module_name && entry->module_name && strcmp(module_name, entry->module_name) == 0);
        bool symbol_match = (!symbol_name && !entry->symbol_name) ||
                            (symbol_name && entry->symbol_name && strcmp(symbol_name, entry->symbol_name) == 0);
        if (module_match && symbol_match) {
            return true;
        }
    }

    return false;
}

static bool record_module_import(CompilerContext* ctx, const char* module_name, const char* symbol_name,
                                 const char* alias_name, ModuleExportKind kind, uint16_t register_index) {
    if (!ctx || !ctx->is_module) {
        return false;
    }

    if (module_import_exists(ctx, module_name, symbol_name)) {
        return true;
    }

    if (ctx->module_import_count >= ctx->module_import_capacity) {
        int new_cap = ctx->module_import_capacity == 0 ? 4 : ctx->module_import_capacity * 2;
        ModuleImportEntry* expanded = realloc(ctx->module_imports, sizeof(ModuleImportEntry) * new_cap);
        if (!expanded) {
            return false;
        }
        ctx->module_imports = expanded;
        ctx->module_import_capacity = new_cap;
    }

    ModuleImportEntry* entry = &ctx->module_imports[ctx->module_import_count];
    entry->module_name = module_name ? orus_strdup(module_name) : NULL;
    entry->symbol_name = symbol_name ? orus_strdup(symbol_name) : NULL;
    entry->alias_name = alias_name ? orus_strdup(alias_name) : NULL;
    if ((module_name && !entry->module_name) || (symbol_name && !entry->symbol_name)) {
        free(entry->module_name);
        free(entry->symbol_name);
        free(entry->alias_name);
        entry->module_name = NULL;
        entry->symbol_name = NULL;
        entry->alias_name = NULL;
        return false;
    }
    entry->kind = kind;
    entry->register_index = (int)register_index;
    ctx->module_import_count++;
    return true;
}

bool finalize_import_symbol(CompilerContext* ctx, const char* module_name, const char* symbol_name,
                                   const char* alias_name, ModuleExportKind kind, uint16_t register_index,
                                   Type* exported_type, SrcLocation location) {
    if (!ctx || !symbol_name) {
        return false;
    }

    const char* binding_name = alias_name ? alias_name : symbol_name;

    if (kind == MODULE_EXPORT_KIND_STRUCT || kind == MODULE_EXPORT_KIND_ENUM) {
        record_module_import(ctx, module_name, symbol_name, alias_name, kind, MODULE_EXPORT_NO_REGISTER);
        return true;
    }

    if (register_index == MODULE_EXPORT_NO_REGISTER) {
        report_compile_error(E3004_IMPORT_FAILED, location,
                             "module '%s' export '%s' is not a value and cannot be used",
                             module_name ? module_name : "<unknown>", symbol_name);
        ctx->has_compilation_errors = true;
        return false;
    }

    if (kind != MODULE_EXPORT_KIND_GLOBAL && kind != MODULE_EXPORT_KIND_FUNCTION) {
        report_compile_error(E3004_IMPORT_FAILED, location,
                             "module '%s' export '%s' is not a loadable value",
                             module_name ? module_name : "<unknown>", symbol_name);
        ctx->has_compilation_errors = true;
        return false;
    }

    int reg = (int)register_index;
    compiler_reserve_global(ctx->allocator, reg);

    Type* resolved_type = exported_type;
    if (!resolved_type) {
        resolved_type = getPrimitiveType(kind == MODULE_EXPORT_KIND_FUNCTION ? TYPE_FUNCTION : TYPE_ANY);
    }
    bool is_mutable = (kind == MODULE_EXPORT_KIND_GLOBAL);
    if (!register_variable(ctx, ctx->symbols, binding_name, reg, resolved_type,
                           is_mutable, is_mutable, location, true)) {
        ctx->has_compilation_errors = true;
        return false;
    }

    record_module_import(ctx, module_name, symbol_name, alias_name, kind, register_index);
    return true;
}

bool import_symbol_by_name(CompilerContext* ctx, ModuleManager* manager, const char* module_name,
                                  const char* symbol_name, const char* alias_name,
                                  SrcLocation location) {
    if (!manager || !module_name || !symbol_name) {
        return false;
    }

    ModuleExportKind kind = MODULE_EXPORT_KIND_GLOBAL;
    uint16_t register_index = MODULE_EXPORT_NO_REGISTER;
    Type* exported_type = NULL;
    if (!module_manager_resolve_export(manager, module_name, symbol_name, &kind, &register_index,
                                       &exported_type)) {
        report_compile_error(E3004_IMPORT_FAILED, location,
                             "module '%s' does not export '%s'", module_name, symbol_name);
        ctx->has_compilation_errors = true;
        return false;
    }

    return finalize_import_symbol(ctx, module_name, symbol_name, alias_name, kind, register_index,
                                  exported_type, location);
}