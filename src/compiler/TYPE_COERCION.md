# Type Coercion System in Orus Compiler

**Version**: 1.0  
**Last Updated**: August 2025  
**Component**: Compiler Backend - Code Generation  

## 📋 Overview

The Orus compiler implements an **automatic type coercion system** that handles mixed-type arithmetic operations by inserting explicit cast instructions at compile-time. This system works in conjunction with the existing typed register architecture to provide both type safety and developer convenience.

## 🎯 Design Goals

1. **Type Safety**: Prevent runtime type mismatches while maintaining strict typing
2. **Developer Experience**: Allow natural mixed-type arithmetic without explicit casts
3. **Performance**: Zero runtime overhead - all coercion happens at compile-time
4. **Predictability**: Clear, consistent promotion rules based on type hierarchy

## 🏗️ Architecture Overview

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Source Code   │───▶│ Type Coercion   │───▶│ Typed Register  │
│  (mixed types)  │    │    System       │    │    System       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
        │                        │                        │
        │                        ▼                        ▼
        │              Insert cast opcodes      Type-specific opcodes
        │                                                  │
        └─────────────────────────────────────────────────▼
                              VM Execution
                           (typed operations)
```

## 📁 Implementation Location

| Component | File Path | Function |
|-----------|-----------|----------|
| **Main Logic** | `src/compiler/backend/codegen/codegen.c` | `compile_binary_op()` |
| **Cast Opcodes** | `src/compiler/backend/codegen/codegen.c` | `get_cast_opcode()` |
| **Type Definitions** | `include/type/type.h` | Type enums and macros |
| **VM Cast Handlers** | `src/vm/handlers/vm_cast_handlers.c` | Cast operation implementations |

## 🔄 Type Promotion Hierarchy

The coercion system follows a **widening conversion** hierarchy to prevent data loss:

```
i32 ──┐
      ├──▶ u32 ──┐
      │          ├──▶ i64 ──┐
      │          │          ├──▶ u64 ──▶ f64
      │          │          │
      └──────────┼──────────┘
                 │
                 └──▶ i64
```

### Promotion Rules Matrix

| Left Type | Right Type | Promoted To | Rationale |
|-----------|------------|-------------|-----------|
| `i32` | `u32` | `u32` | Avoid sign issues with small values |
| `i32` | `i64` | `i64` | Preserve range of larger type |
| `i32` | `u64` | `u64` | Preserve range of larger type |
| `i32` | `f64` | `f64` | Preserve floating-point precision |
| `u32` | `i64` | `i64` | Preserve range of larger type |
| `u32` | `u64` | `u64` | Natural unsigned promotion |
| `u32` | `f64` | `f64` | Preserve floating-point precision |
| `i64` | `u64` | `u64` | Preserve range (with overflow risk) |
| `i64` | `f64` | `f64` | Preserve floating-point precision |
| `u64` | `f64` | `f64` | Preserve floating-point precision |

## 🔧 Implementation Details

### Core Coercion Logic

```c
// Location: src/compiler/backend/codegen/codegen.c:compile_binary_op()

if (left_type->kind != right_type->kind) {
    printf("[CODEGEN] Type mismatch detected: %d vs %d, applying coercion\n", 
           left_type->kind, right_type->kind);
    
    TypeKind promoted_type;
    
    // Apply promotion rules
    if ((left_type->kind == TYPE_I32 && right_type->kind == TYPE_U32) ||
        (left_type->kind == TYPE_U32 && right_type->kind == TYPE_I32)) {
        promoted_type = TYPE_U32;  // Rule: i32 + u32 → u32
    }
    else if (left_type->kind == TYPE_I64 || right_type->kind == TYPE_I64) {
        promoted_type = TYPE_I64;  // Rule: any + i64 → i64
    }
    else if (left_type->kind == TYPE_U64 || right_type->kind == TYPE_U64) {
        promoted_type = TYPE_U64;  // Rule: any + u64 → u64
    }
    else if (left_type->kind == TYPE_F64 || right_type->kind == TYPE_F64) {
        promoted_type = TYPE_F64;  // Rule: any + f64 → f64
    }
    else {
        promoted_type = TYPE_I64;  // Default fallback
    }
    
    // Insert cast instructions for mismatched operands
    [cast insertion logic...]
}
```

### Cast Opcode Selection

```c
// Location: src/compiler/backend/codegen/codegen.c:get_cast_opcode()

uint8_t get_cast_opcode(TypeKind from_type, TypeKind to_type) {
    // Integer to Integer Conversions
    if (from_type == TYPE_I32 && to_type == TYPE_I64) return OP_I32_TO_I64_R;
    if (from_type == TYPE_I32 && to_type == TYPE_U32) return OP_I32_TO_U32_R;
    if (from_type == TYPE_I32 && to_type == TYPE_U64) return OP_I32_TO_U64_R;
    
    if (from_type == TYPE_U32 && to_type == TYPE_I32) return OP_U32_TO_I32_R;
    if (from_type == TYPE_U32 && to_type == TYPE_I64) return OP_U32_TO_I64_R;
    if (from_type == TYPE_U32 && to_type == TYPE_U64) return OP_U32_TO_U64_R;
    
    // Integer to Float Conversions
    if (from_type == TYPE_I32 && to_type == TYPE_F64) return OP_I32_TO_F64_R;
    if (from_type == TYPE_U32 && to_type == TYPE_F64) return OP_U32_TO_F64_R;
    if (from_type == TYPE_I64 && to_type == TYPE_F64) return OP_I64_TO_F64_R;
    if (from_type == TYPE_U64 && to_type == TYPE_F64) return OP_U64_TO_F64_R;
    
    // Float to Integer Conversions (potentially lossy)
    if (from_type == TYPE_F64 && to_type == TYPE_I32) return OP_F64_TO_I32_R;
    if (from_type == TYPE_F64 && to_type == TYPE_U32) return OP_F64_TO_U32_R;
    if (from_type == TYPE_F64 && to_type == TYPE_I64) return OP_F64_TO_I64_R;
    if (from_type == TYPE_F64 && to_type == TYPE_U64) return OP_F64_TO_U64_R;
    
    return OP_HALT; // Invalid conversion
}
```

### Cast Instruction Emission

```c
// Cast left operand if needed
if (left_type->kind != promoted_type) {
    int cast_reg = compiler_alloc_temp(ctx->allocator);
    uint8_t cast_opcode = get_cast_opcode(left_type->kind, promoted_type);
    
    if (cast_opcode == OP_HALT) {
        printf("[CODEGEN] Error: Invalid cast from %d to %d\n", 
               left_type->kind, promoted_type);
        return -1;
    }
    
    emit_bytecode_3(ctx->bytecode, cast_opcode, cast_reg, left_reg);
    printf("[CODEGEN] Casting left operand from %d to %d (R%d -> R%d)\n",
           left_type->kind, promoted_type, left_reg, cast_reg);
    left_reg = cast_reg;
}

// Cast right operand if needed
if (right_type->kind != promoted_type) {
    int cast_reg = compiler_alloc_temp(ctx->allocator);
    uint8_t cast_opcode = get_cast_opcode(right_type->kind, promoted_type);
    
    if (cast_opcode == OP_HALT) {
        printf("[CODEGEN] Error: Invalid cast from %d to %d\n", 
               right_type->kind, promoted_type);
        return -1;
    }
    
    emit_bytecode_3(ctx->bytecode, cast_opcode, cast_reg, right_reg);
    printf("[CODEGEN] Casting right operand from %d to %d (R%d -> R%d)\n",
           right_type->kind, promoted_type, right_reg, cast_reg);
    right_reg = cast_reg;
}
```

## 🎭 Compilation Example

### Source Code
```rust
val_u32: u32 = 50
val_i32: i32 = 2
result = val_u32 * val_i32  // u32 * i32 - mixed types!
```

### Compilation Steps

#### Step 1: Type Analysis
```c
left_type = TYPE_U32   // val_u32
right_type = TYPE_I32  // val_i32 literal
```

#### Step 2: Coercion Decision
```c
// Rule: i32 + u32 → u32
promoted_type = TYPE_U32
```

#### Step 3: Cast Insertion
```c
// Left operand: u32 → u32 (no cast needed)
// Right operand: i32 → u32 (cast needed)

emit_bytecode_3(ctx->bytecode, OP_I32_TO_U32_R, R196, R194);
// Bytecode: [OP_I32_TO_U32_R] [R196] [R194]
```

#### Step 4: Typed Arithmetic
```c
// Now both operands are u32, emit typed operation
emit_bytecode_4(ctx->bytecode, OP_MUL_U32_TYPED, R195, R66, R196);
// Bytecode: [OP_MUL_U32_TYPED] [R195] [R66] [R196]
```

### Generated Bytecode
```assembly
OP_LOAD_CONST    R192, #1       ; Load 50 (u32)
OP_MOVE          R66,  R192     ; Store in val_u32
OP_LOAD_I32_CONST R194, #2      ; Load 2 (i32)
OP_I32_TO_U32_R  R196, R194     ; Cast i32 → u32
OP_MUL_U32_TYPED R195, R66, R196 ; u32 * u32
OP_MOVE          R67,  R195     ; Store result
```

### Debug Output
```
[CODEGEN] Type mismatch detected: 3 vs 1, applying coercion
[CODEGEN] Promoting to type: 3
[CODEGEN] Casting right operand from 1 to 3 (R194 -> R196)
[CODEGEN] Handling REG_TYPE_U32 arithmetic operation: *
[CODEGEN] select_optimal_opcode returned: 143 (OP_MUL_U32_TYPED)
[CODEGEN] Emitted *_TYPED R195, R66, R196
```

## 🚀 Runtime Execution

### VM Cast Handler Example
```c
// Location: src/vm/handlers/vm_cast_handlers.c

void handle_i32_to_u32_cast(void) {
    uint8_t dst = READ_BYTE();
    uint8_t src = READ_BYTE();
    
    if (!IS_I32(vm.registers[src])) {
        runtimeError(ERROR_TYPE, CURRENT_LOCATION(), 
                    "Cast source must be i32");
        return;
    }
    
    int32_t i32_val = AS_I32(vm.registers[src]);
    
    // Check for negative values (unsafe conversion)
    if (i32_val < 0) {
        runtimeError(ERROR_RUNTIME, CURRENT_LOCATION(), 
                    "Cannot cast negative i32 to u32");
        return;
    }
    
    // Safe conversion
    vm.registers[dst] = U32_VAL((uint32_t)i32_val);
}
```

### Typed Arithmetic Handler
```c
// Location: src/vm/handlers/vm_arithmetic_handlers.c

void handle_mul_u32_typed(void) {
    uint8_t dst = READ_BYTE();
    uint8_t left = READ_BYTE();
    uint8_t right = READ_BYTE();
    
    // Type checking with typed registers
    if (!IS_U32(vm.registers[left]) || !IS_U32(vm.registers[right])) {
        runtimeError(ERROR_TYPE, (SrcLocation){NULL,0,0}, 
                    "Operands must be u32");
        return;
    }
    
    // Perform typed arithmetic
    uint32_t result = AS_U32(vm.registers[left]) * AS_U32(vm.registers[right]);
    vm.registers[dst] = U32_VAL(result);  // Store as typed value
}
```

## 🔒 Safety Guarantees

### Compile-Time Safety
1. **Type Compatibility**: All operands are coerced to compatible types before operation
2. **Cast Validation**: Invalid cast combinations are rejected with `OP_HALT`
3. **Register Safety**: Temporary registers are properly allocated for cast results

### Runtime Safety
1. **Type Verification**: VM handlers verify operand types before operation
2. **Range Checking**: Cast handlers check for unsafe conversions (e.g., negative to unsigned)
3. **Overflow Detection**: Arithmetic handlers can detect overflow conditions

## 📊 Performance Characteristics

### Compile-Time Overhead
- **Minimal**: O(1) type comparison per binary operation
- **Cast Analysis**: O(1) lookup in cast opcode table
- **Register Allocation**: O(1) temporary register allocation

### Runtime Overhead
- **Zero for Same-Type Operations**: No coercion needed
- **Single Cast Instruction**: For mixed-type operations  
- **Optimized Handlers**: Type-specific VM handlers for maximum performance

### Memory Usage
- **Temporary Registers**: One additional register per cast operation
- **Bytecode Size**: +3 bytes per cast instruction
- **No Heap Allocation**: All operations use stack-based registers

## 🧪 Testing and Validation

### Test Coverage
All type coercion scenarios are covered in:
- `tests/arithmetic/comprehensive_arithmetic_test.orus`
- Individual type-specific tests in `tests/types/`

### Example Test Cases
```rust
// Mixed integer arithmetic
u32_val: u32 = 50
result1 = u32_val * 2        // u32 * i32 → u32

// Cross-size promotions  
i32_val: i32 = 100
i64_val: i64 = 200
result2 = i32_val + i64_val  // i32 + i64 → i64

// Float promotions
int_val: i32 = 42
float_val: f64 = 3.14
result3 = int_val * float_val // i32 * f64 → f64
```

## ✅ **Explicit Cast System (Already Implemented)**

The Orus compiler **already supports explicit cast syntax** using the `as` keyword, which works seamlessly with the automatic coercion system.

### **Explicit Cast Syntax**
```rust
// Override automatic coercion with explicit casts
val_i32: i32 = 42
val_u64: u64 = (val_i32 as u64)  // Explicit i32 → u64 cast

// Cast literals directly
literal_f64: f64 = (100 as f64)   // Explicit i32 literal → f64

// Control type promotion in expressions
result = (50 as u32) * (2 as u32) // Force u32 * u32 (no auto coercion)
```

### **Explicit vs Automatic Coercion Priority**

| Scenario | Behavior | System Used |
|----------|----------|-------------|
| `u32_val * i32_val` | Automatic coercion: i32 → u32 | Auto Coercion |
| `(u32_val as i32) * i32_val` | No coercion needed | Explicit Cast |
| `u32_val * (i32_val as u32)` | No coercion needed | Explicit Cast |
| `(u32_val as f64) * i32_val` | Automatic coercion: i32 → f64 | Both Systems |

### **Implementation Details**

#### AST Structure (Already Exists)
```c
// Location: include/compiler/ast.h
struct {
    ASTNode* expression;           // Expression to cast
    ASTNode* targetType;           // Target type  
    bool parenthesized;            // Whether the cast was parenthesized
} cast;
```

#### Compilation Flow (Already Working)
```c
// Location: src/compiler/backend/codegen/codegen.c
case NODE_CAST: {
    // Compile inner expression
    int source_reg = compile_expression(ctx, expr->typed.cast.expression);
    
    // Get types
    Type* source_type = expr->typed.cast.expression->resolvedType;
    Type* target_type = expr->resolvedType;
    
    // Emit explicit cast instruction
    uint8_t cast_opcode = get_cast_opcode(source_type->kind, target_type->kind);
    int target_reg = compiler_alloc_temp(ctx->allocator);
    emit_instruction_to_buffer(ctx->bytecode, cast_opcode, target_reg, source_reg, 0);
    
    return target_reg;
}
```

### **Real Example: Explicit Cast in Action**

#### Source Code
```rust
val_i32: i32 = 50
explicit_result = (val_i32 as u32) * 2  // Explicit cast
auto_result = val_i32 * 2                // Automatic coercion
```

#### Generated Bytecode Comparison

**Explicit Cast Version:**
```assembly
OP_LOAD_I32_CONST R192, #1      ; Load 50 (i32)
OP_MOVE          R64,  R192     ; Store val_i32
OP_I32_TO_U32_R  R193, R64      ; Explicit cast: i32 → u32
OP_LOAD_I32_CONST R194, #2      ; Load 2 (i32)  
OP_I32_TO_U32_R  R195, R194     ; Auto coercion: i32 → u32 (for multiplication)
OP_MUL_U32_TYPED R192, R193, R195 ; u32 * u32
```

**Automatic Coercion Version:**
```assembly
OP_LOAD_I32_CONST R192, #1      ; Load 50 (i32)
OP_MOVE          R64,  R192     ; Store val_i32  
OP_LOAD_I32_CONST R194, #2      ; Load 2 (i32)
OP_I32_TO_U32_R  R195, R64      ; Auto coercion: i32 → u32
OP_I32_TO_U32_R  R196, R194     ; Auto coercion: i32 → u32
OP_MUL_U32_TYPED R192, R195, R196 ; u32 * u32
```

### **System Integration**

The explicit cast system **reuses the same infrastructure** as automatic coercion:

1. **Same Cast Opcodes**: `OP_I32_TO_U32_R`, `OP_U64_TO_F64_R`, etc.
2. **Same VM Handlers**: `handle_i32_to_u32_cast()`, etc.
3. **Same Safety Checks**: Runtime validation for invalid casts
4. **Same Type Promotion Logic**: Uses `get_cast_opcode()` function

### **Leveraging Existing Coercion Engine**

To implement enhanced explicit cast features, we can leverage the existing coercion infrastructure:

#### **1. Strict Mode Implementation**
```c
// Add to compile_binary_op()
if (ctx->strict_mode && left_type->kind != right_type->kind) {
    // In strict mode, require explicit casts
    fprintf(stderr, "Error: Implicit type coercion not allowed in strict mode. "
                   "Use explicit cast: (expr as %s)\n", 
                   type_kind_to_string(promoted_type));
    return -1;
}
```

#### **2. Warning System Integration**
```c
// Add to get_cast_opcode()
bool is_potentially_lossy_cast(TypeKind from, TypeKind to) {
    return (from == TYPE_I64 && to == TYPE_I32) ||
           (from == TYPE_U64 && to == TYPE_U32) ||
           (from == TYPE_F64 && to != TYPE_F64);
}

// In compile_binary_op()
if (ctx->warnings_enabled && is_potentially_lossy_cast(from_type, to_type)) {
    fprintf(stderr, "Warning: Potentially lossy conversion from %s to %s\n",
           type_kind_to_string(from_type), type_kind_to_string(to_type));
}
```

#### **3. Cast Chain Optimization**
```c
// Detect cast chains: (expr as T1) as T2
if (expr->type == NODE_CAST && expr->cast.expression->type == NODE_CAST) {
    // Optimize: T0 → T1 → T2 becomes T0 → T2 (if valid)
    TypeKind original_type = get_original_cast_type(expr);
    TypeKind final_type = expr->resolvedType->kind;
    
    if (can_direct_cast(original_type, final_type)) {
        uint8_t direct_opcode = get_cast_opcode(original_type, final_type);
        // Emit single cast instead of chain
    }
}
```

## 🔮 Future Enhancements

### Planned Improvements
1. **Warning System**: Compile-time warnings for potentially lossy conversions
2. **Const Folding Integration**: Fold cast operations with constant operands  
3. **Range Analysis**: Compile-time range checking to avoid runtime cast failures
4. **Strict Mode**: Optional mode requiring explicit casts for all type conversions

### Extension Points
1. **Custom Types**: Framework for user-defined type coercion rules
2. **Vector Types**: SIMD-style coercion for array operations
3. **Conditional Coercion**: Context-aware coercion based on usage patterns
4. **Cast Chain Optimization**: Optimize multiple sequential casts

## 🚨 Common Pitfalls and Solutions

### Pitfall 1: Signed/Unsigned Mixing
```rust
// Problematic
i32_val: i32 = -5
u32_val: u32 = 10
result = i32_val + u32_val  // i32 + u32 → u32, but -5 as u32 is huge!
```

**Solution**: Consider adding compile-time warnings for negative literal coercion to unsigned.

### Pitfall 2: Precision Loss
```rust
// Precision loss
large_int: u64 = 18446744073709551615  // Max u64
float_result: f64 = large_int * 1.0    // May lose precision
```

**Solution**: Future warning system should flag potential precision loss.

### Pitfall 3: Cast Overhead
```rust
// Excessive casting in loops
for i in 0..1000000 {
    u32_val: u32 = get_u32()
    result = u32_val + i  // i32 + u32 cast happens every iteration
}
```

**Solution**: Future optimization could hoist invariant casts outside loops.

## 📚 References and Related Systems

### Related Components
- **Type Inference System**: `src/type/type_inference.c`
- **Typed Register System**: `src/vm/handlers/vm_arithmetic_handlers.c`
- **Register Allocator**: `src/compiler/backend/register_allocator.c`
- **Bytecode Emitter**: `src/compiler/backend/codegen/bytecode_emitter.c`

### Design Influences
- **Rust**: Explicit casting with compile-time safety
- **C++**: Implicit conversions with type promotion rules
- **Java**: Widening primitive conversions
- **LLVM**: Type-based instruction selection

---

**Maintainer**: Orus Compiler Team  
**Contact**: For questions about this system, refer to the implementation in `codegen.c:compile_binary_op()`  
**Last Reviewed**: August 2025