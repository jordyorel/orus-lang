#ifndef CODEGEN_MODULES_H
#define CODEGEN_MODULES_H

#include "compiler/compiler.h"
#include "vm/module_manager.h"
#include "type/type.h"

void record_module_export(CompilerContext* ctx, const char* name, ModuleExportKind kind, Type* type);
void set_module_export_metadata(CompilerContext* ctx, const char* name, int reg, Type* type);
void set_module_export_function_index(CompilerContext* ctx, const char* name, int function_index);
bool finalize_import_symbol(CompilerContext* ctx, const char* module_name, const char* symbol_name,
                            const char* alias_name, ModuleExportKind kind, uint16_t register_index,
                            Type* exported_type, SrcLocation location);
bool import_symbol_by_name(CompilerContext* ctx, ModuleManager* manager, const char* module_name,
                           const char* symbol_name, const char* alias_name, SrcLocation location);

#endif // CODEGEN_MODULES_H
