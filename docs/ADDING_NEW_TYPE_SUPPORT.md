# Adding New Type Support to Orus

This guide explains how to add support for new types in the Orus programming language, covering all components from the VM to code generation.

## Overview
Orus currently supports:
- 32-bit: `i32`, `u32`
- 64-bit: `i64`, `u64`, `f64`

To add new types (e.g., 16-bit integers `i16`/`u16`), follow these steps:

---

## 1. Virtual Machine (VM) Changes

### 1.1 Update Type Definitions
Modify `include/vm/vm.h`:
```c
// ValueType enum
typedef enum {
    // ... existing types ...
    VAL_I16,  // New 16-bit signed integer
    VAL_U16,  // New 16-bit unsigned integer
} ValueType;

// TypeKind enum
typedef enum {
    // ... existing types ...
    TYPE_I16,
    TYPE_U16,
} TypeKind;
```

### 1.2 Extend Value Representation
Update the Value union in `include/vm/vm.h`:
```c
typedef struct {
    ValueType type;
    union {
        // ... existing types ...
        int16_t i16;    // 16-bit signed
        uint16_t u16;   // 16-bit unsigned
    } as;
} Value;
```

### 1.3 Add New Opcodes
Extend the OpCode enum with type-specific operations:
```c
typedef enum {
    // ... existing opcodes ...
    OP_ADD_I16_R,
    OP_SUB_I16_R,
    OP_MUL_I16_R,
    OP_DIV_I16_R,
    OP_MOD_I16_R,
    OP_LT_I16_R,
    OP_LE_I16_R,
    // ... and other necessary operations ...
} OpCode;
```

### 1.4 Implement Type Handlers
Create new handler files in `src/vm/handlers/`:
- `vm_i16_handlers.c`
- `vm_u16_handlers.c`

Implement required operations:
```c
// Example addition handler for i16
void op_add_i16_r(VM* vm) {
    uint8_t dst = READ_BYTE();
    uint8_t src1 = READ_BYTE();
    uint8_t src2 = READ_BYTE();
    
    int16_t a = vm->registers[src1].as.i16;
    int16_t b = vm->registers[src2].as.i16;
    vm->registers[dst] = I16_VAL(a + b);
}
```

### 1.5 Update Type Conversion
Extend type conversion functions in `src/vm/operations/vm_typed_ops.c`:
```c
Value convert_value(Value value, ValueType target_type) {
    switch(value.type) {
        // ... existing conversions ...
        case VAL_I32:
            switch(target_type) {
                case VAL_I16: return I16_VAL((int16_t)value.as.i32);
                // ...
            }
        // ...
    }
}
```

---

## 2. Lexer Updates
Modify `src/compiler/frontend/lexer.c`:

### 2.1 Add New Keywords
```c
static Keyword keywords[] = {
    // ... existing keywords ...
    {"i16", TOK_TYPE_I16},
    {"u16", TOK_TYPE_U16},
    // ...
};
```

### 2.2 Recognize New Literals
Update number literal recognition:
```c
Token lex_number(Lexer* lexer) {
    // ... existing number parsing ...
    if (/* fits in 16 bits */) {
        return make_token(lexer, TOK_INT16);
    }
    // ...
}
```

---

## 3. Parser Modifications
Update `src/compiler/frontend/parser.c`:

### 3.1 Add Type AST Nodes
```c
typedef enum {
    // ... existing node types ...
    AST_TYPE_I16,
    AST_TYPE_U16,
} ASTNodeType;
```

### 3.2 Handle Type Declarations
```c
static ASTNode* parse_type(Parser* parser) {
    if (match(parser, TOK_TYPE_I16)) {
        return create_ast_node(AST_TYPE_I16);
    }
    if (match(parser, TOK_TYPE_U16)) {
        return create_ast_node(AST_TYPE_U16);
    }
    // ...
}
```

---

## 4. Typed AST Visualizer
Update `src/compiler/backend/typed_ast_visualizer.c`:

### 4.1 Add Type Visualization
```c
void visualize_type(Type* type, int indent) {
    switch(type->kind) {
        // ... existing cases ...
        case TYPE_I16:
            printf("%*sType: i16\n", indent, "");
            break;
        case TYPE_U16:
            printf("%*sType: u16\n", indent, "");
            break;
    }
}
```

---

## 5. Optimizer Enhancements
Update `src/compiler/backend/optimizer.c`:

### 5.1 Add Type-Specific Folding
```c
Value constant_fold_binary_op(ASTNode* node) {
    // ... existing folding logic ...
    case AST_ADD:
        if (left_type == TYPE_I16 && right_type == TYPE_I16) {
            return I16_VAL(left_value.as.i16 + right_value.as.i16);
        }
    // ...
}
```

### 5.2 Register Allocation
Update register allocation for new types in `include/compiler/register_allocator.h`:
```c
typedef struct {
    // ... existing fields ...
    bool is_i16 : 1;  // Track i16 usage
    bool is_u16 : 1;  // Track u16 usage
} RegisterMetadata;
```

---

## 6. Code Generation
Update `src/compiler/backend/codegen.c`:

### 6.1 Type-Aware Code Generation
```c
void gen_expr(CodeGen* gen, ASTNode* node) {
    switch(node->type) {
        // ... existing cases ...
        case AST_LITERAL:
            if (node->literal_type == TYPE_I16) {
                emit_opcode(gen, OP_LOAD_I16_CONST);
                emit_register(gen, alloc_register(gen));
                emit_i16(gen, node->value.as.i16);
            }
            // ...
    }
}
```

### 6.2 Function Prolog/Epilog
Handle new types in function calls:
```c
void gen_function_prolog(CodeGen* gen, FunctionDef* func) {
    for (int i = 0; i < func->param_count; i++) {
        if (func->param_types[i] == TYPE_I16) {
            // Special handling for i16 parameters
        }
    }
}
```

---

## Testing New Types
1. Create test files in `tests/types/`:
   - `tests/types/i16/`
   - `tests/types/u16/`
2. Add test cases covering:
   - Declaration and initialization
   - Arithmetic operations
   - Type conversions
   - Function parameters and return values

Example test (`tests/types/i16/basic.orus`):
```orus
i16 a = 3000
i16 b = 2000
return a + b  // Should be 5000
```

3. Update test runner scripts to include new tests

---

## Best Practices
1. **Consistent Naming**: Use `i16`/`u16` prefix for all new handlers and operations
2. **Range Checking**: Implement proper overflow/underflow checks
3. **Type Promotion**: Define clear promotion rules for mixed-type operations
4. **Benchmarking**: Profile performance of new type operations
5. **Documentation**: Update language specification with new type details
