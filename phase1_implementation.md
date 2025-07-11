# Phase 1 Implementation: Type-Aware Expression Descriptors

## Step 1: Design the TypedExpDesc System

### Core Data Structure
```c
// Type-aware expression descriptor
typedef struct TypedExpDesc {
    ExpKind kind;
    ValueType type;           // Static type of the expression
    bool isConstant;          // Can be evaluated at compile time
    
    union {
        struct {
            int info;         // register or constant index
            ValueType regType; // Type stored in register
            bool isTemporary; // Can be freed after use
        } s;
        struct {
            Value value;      // Compile-time constant value
            int constIndex;   // Index in constant table (-1 if not cached)
        } constant;
    } u;
    
    // Control flow patches (from Lua)
    int t;  // patch list of 'exit when true'
    int f;  // patch list of 'exit when false'
} TypedExpDesc;
```

### Key Design Principles
1. **Type Safety First**: Every expression has a known type
2. **Backward Compatibility**: Can coexist with existing system
3. **Performance**: Enables better optimizations
4. **Simplicity**: Cleaner than current ad-hoc approach

## Step 2: Implement Core Functions

### Expression Descriptor Management
```c
// Initialize expression descriptor
static void init_typed_exp(TypedExpDesc* e, ExpKind kind, ValueType type, int info) {
    e->kind = kind;
    e->type = type;
    e->isConstant = (kind == EXP_K);
    e->u.s.info = info;
    e->u.s.regType = type;
    e->u.s.isTemporary = false;
    e->t = e->f = NO_JUMP;
}

// Discharge expression to specific register with type checking
static void discharge_typed_reg(Compiler* compiler, TypedExpDesc* e, int reg) {
    switch (e->kind) {
        case EXP_K: {
            // Load constant with type information
            emitConstant(compiler, reg, e->u.constant.value);
            setRegisterType(compiler, reg, e->type);
            break;
        }
        case EXP_LOCAL: {
            // Move from local with type preservation
            if (e->u.s.info != reg) {
                emitByte(compiler, OP_MOVE);
                emitByte(compiler, reg);
                emitByte(compiler, e->u.s.info);
            }
            setRegisterType(compiler, reg, e->type);
            break;
        }
        case EXP_TRUE:
        case EXP_FALSE: {
            // Load boolean constant
            emitByte(compiler, e->kind == EXP_TRUE ? OP_LOAD_TRUE : OP_LOAD_FALSE);
            emitByte(compiler, reg);
            setRegisterType(compiler, reg, VAL_BOOL);
            break;
        }
        case EXP_NIL: {
            emitByte(compiler, OP_LOAD_NIL);
            emitByte(compiler, reg);
            setRegisterType(compiler, reg, VAL_NIL);
            break;
        }
        default:
            break;
    }
    e->kind = EXP_TEMP;
    e->u.s.info = reg;
    e->u.s.regType = e->type;
}

// Type-aware constant folding
static bool try_constant_fold(TypedExpDesc* left, TypedExpDesc* right, 
                             const char* op, TypedExpDesc* result) {
    if (left->kind != EXP_K || right->kind != EXP_K) {
        return false;
    }
    
    Value leftVal = left->u.constant.value;
    Value rightVal = right->u.constant.value;
    
    // Type-safe arithmetic with promotion
    if (IS_I32(leftVal) && IS_I32(rightVal)) {
        int32_t a = AS_I32(leftVal);
        int32_t b = AS_I32(rightVal);
        
        if (strcmp(op, "+") == 0) {
            result->u.constant.value = I32_VAL(a + b);
            result->type = VAL_I32;
            result->kind = EXP_K;
            return true;
        }
        // Add other operations...
    }
    
    // Handle type promotion (i32 + f64 -> f64)
    if ((IS_I32(leftVal) || IS_F64(leftVal)) && 
        (IS_I32(rightVal) || IS_F64(rightVal))) {
        double a = IS_I32(leftVal) ? AS_I32(leftVal) : AS_F64(leftVal);
        double b = IS_I32(rightVal) ? AS_I32(rightVal) : AS_F64(rightVal);
        
        if (strcmp(op, "+") == 0) {
            result->u.constant.value = F64_VAL(a + b);
            result->type = VAL_F64;
            result->kind = EXP_K;
            return true;
        }
        // Add other operations...
    }
    
    return false;
}
```

## Step 3: Implement Type-Aware Compilation

### Expression Compilation
```c
static void compile_typed_expr(Compiler* compiler, ASTNode* node, TypedExpDesc* desc) {
    switch (node->type) {
        case NODE_LITERAL: {
            Value v = node->literal.value;
            desc->kind = EXP_K;
            desc->type = v.type;
            desc->u.constant.value = v;
            desc->u.constant.constIndex = -1;
            desc->isConstant = true;
            break;
        }
        
        case NODE_IDENTIFIER: {
            // Find variable with type information
            int localIndex = findLocal(compiler, node->identifier.name);
            if (localIndex >= 0) {
                desc->kind = EXP_LOCAL;
                desc->type = compiler->locals[localIndex].type;
                desc->u.s.info = compiler->locals[localIndex].reg;
                desc->u.s.regType = desc->type;
                desc->isConstant = false;
            } else {
                // Handle global variables or errors
                desc->kind = EXP_VOID;
                desc->type = VAL_NIL;
            }
            break;
        }
        
        case NODE_BINARY: {
            compile_typed_binary(compiler, node, desc);
            break;
        }
        
        case NODE_UNARY: {
            compile_typed_unary(compiler, node, desc);
            break;
        }
        
        default: {
            desc->kind = EXP_VOID;
            desc->type = VAL_NIL;
            break;
        }
    }
}

static void compile_typed_binary(Compiler* compiler, ASTNode* node, TypedExpDesc* desc) {
    TypedExpDesc left, right;
    
    // Compile operands
    compile_typed_expr(compiler, node->binary.left, &left);
    compile_typed_expr(compiler, node->binary.right, &right);
    
    // Try constant folding first
    if (try_constant_fold(&left, &right, node->binary.op, desc)) {
        return;
    }
    
    // Determine result type using existing type inference
    ValueType resultType = inferBinaryOpTypeWithCompiler(
        node->binary.left, node->binary.right, compiler);
    
    // Emit runtime code
    discharge_typed_reg(compiler, &left, allocateRegister(compiler));
    discharge_typed_reg(compiler, &right, allocateRegister(compiler));
    
    int resultReg = allocateRegister(compiler);
    
    // Emit type-specific instruction
    if (strcmp(node->binary.op, "+") == 0) {
        switch (resultType) {
            case VAL_I32: emitByte(compiler, OP_ADD_I32_R); break;
            case VAL_I64: emitByte(compiler, OP_ADD_I64_R); break;
            case VAL_F64: emitByte(compiler, OP_ADD_F64_R); break;
            default: emitByte(compiler, OP_ADD_I32_R); break;
        }
    }
    // Add other operators...
    
    emitByte(compiler, resultReg);
    emitByte(compiler, left.u.s.info);
    emitByte(compiler, right.u.s.info);
    
    // Free temporary registers
    if (left.u.s.isTemporary) freeRegister(compiler, left.u.s.info);
    if (right.u.s.isTemporary) freeRegister(compiler, right.u.s.info);
    
    // Set result descriptor
    desc->kind = EXP_TEMP;
    desc->type = resultType;
    desc->u.s.info = resultReg;
    desc->u.s.regType = resultType;
    desc->u.s.isTemporary = true;
    desc->isConstant = false;
}
```

## Step 4: Integration Strategy

### Gradual Migration
```c
// Phase 1: Add new functions alongside existing ones
int compile_typed_expression_to_register(ASTNode* node, Compiler* compiler) {
    TypedExpDesc desc;
    compile_typed_expr(compiler, node, &desc);
    
    if (desc.kind == EXP_VOID) {
        return -1;
    }
    
    int reg = allocateRegister(compiler);
    discharge_typed_reg(compiler, &desc, reg);
    return reg;
}

// Phase 2: Create wrapper for backward compatibility
int compileExpressionToRegister_new(ASTNode* node, Compiler* compiler) {
    // Use new system but maintain same interface
    return compile_typed_expression_to_register(node, compiler);
}

// Phase 3: Gradually replace calls
// Replace calls to compileExpressionToRegister with compileExpressionToRegister_new
// Then eventually rename to original function name
```

## Step 5: Testing Strategy

### Unit Tests
```c
// Test constant folding
void test_constant_folding() {
    // Test: 5 + 3 should fold to 8
    // Test: 5.0 + 3 should fold to 8.0 and promote to f64
    // Test: Complex expressions with multiple operations
}

// Test type preservation
void test_type_preservation() {
    // Test: i32 + i32 -> i32
    // Test: i32 + f64 -> f64
    // Test: Register types are correctly tracked
}

// Test integration
void test_integration() {
    // Test: Existing programs still compile
    // Test: Performance is maintained
    // Test: Generated bytecode is correct
}
```

### Integration Points
1. **Compiler Interface**: Maintain existing `compileExpressionToRegister` signature
2. **Type System**: Use existing type inference where possible
3. **Register Management**: Extend existing register allocation
4. **Error Handling**: Preserve existing error reporting

## Expected Benefits

### Immediate (Phase 1)
- Better constant folding
- Cleaner expression compilation
- Type-safe register management

### Medium-term (Phase 2-3)
- Simplified compiler architecture
- Better optimization opportunities
- Improved maintainability

### Long-term (Phase 4+)
- Foundation for advanced optimizations
- Easier language feature additions
- Better debugging capabilities

## Risk Mitigation

1. **Backward Compatibility**: Keep existing functions during transition
2. **Performance**: Monitor compilation speed throughout migration
3. **Testing**: Comprehensive test suite for each component
4. **Rollback**: Clear rollback points at each step

This approach allows you to get the benefits of Lua's expression descriptor system while preserving the static typing guarantees that make Orus powerful.