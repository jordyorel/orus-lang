#ifndef CODEGEN_INTERNAL_H
#define CODEGEN_INTERNAL_H

#include "compiler/compiler.h"
#include "compiler/scope_stack.h"
#include "compiler/symbol_table.h"
#include "compiler/typed_ast.h"
#include "type/type.h"
#include <stdint.h>

bool repl_mode_active(void);
void set_location_from_node(CompilerContext* ctx, TypedASTNode* node);
ScopeFrame* get_scope_frame_by_index(CompilerContext* ctx, int index);

int lookup_variable(CompilerContext* ctx, const char* name);
Symbol* register_variable(CompilerContext* ctx, SymbolTable* scope,
                          const char* name, int reg, Type* type,
                          bool is_mutable, bool declared_mutable,
                          SrcLocation location, bool is_initialized);

void emit_load_constant(CompilerContext* ctx, int reg, Value constant);
void emit_typed_instruction(CompilerContext* ctx, uint8_t opcode, int dst, int src1, int src2);
void emit_move(CompilerContext* ctx, int dst, int src);

int compile_assignment_internal(CompilerContext* ctx, TypedASTNode* assign, bool as_expression);
int compile_array_assignment(CompilerContext* ctx, TypedASTNode* assign, bool as_expression);
int compile_member_assignment(CompilerContext* ctx, TypedASTNode* assign, bool as_expression);

char* create_method_symbol_name(const char* struct_name, const char* method_name);
int resolve_struct_field_index(Type* struct_type, const char* field_name);
int resolve_variable_or_upvalue(CompilerContext* ctx, const char* name, bool* is_upvalue, int* upvalue_index);
bool evaluate_constant_i32(TypedASTNode* node, int32_t* out_value);
void ensure_i32_typed_register(CompilerContext* ctx, int reg, const TypedASTNode* source);

#endif // CODEGEN_INTERNAL_H
