# Orus VM Instruction Set Reference

**Complete instruction set documentation for the Orus register-based virtual machine.**

## Instruction Format

All Orus VM instructions follow a consistent 4-byte format:

```
┌─────────────┬─────────────┬─────────────┬─────────────┐
│   Opcode    │  Target Reg │  Source1    │  Source2    │
│   (8 bit)   │   (8 bit)   │  (8 bit)    │  (8 bit)    │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

**Instruction Categories:**
- **Arithmetic Operations**: 32 opcodes
- **Memory Operations**: 28 opcodes  
- **Control Flow**: 18 opcodes
- **Type Operations**: 24 opcodes
- **Comparison Operations**: 15 opcodes
- **Special Operations**: 18 opcodes

**Total**: 135+ opcodes

---

## Arithmetic Operations (32 opcodes)

### Integer Arithmetic

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x10` | `ADD_I32_R` | `ADD_I32_R dst, src1, src2` | dst = src1 + src2 (32-bit signed) |
| `0x11` | `SUB_I32_R` | `SUB_I32_R dst, src1, src2` | dst = src1 - src2 (32-bit signed) |
| `0x12` | `MUL_I32_R` | `MUL_I32_R dst, src1, src2` | dst = src1 * src2 (32-bit signed) |
| `0x13` | `DIV_I32_R` | `DIV_I32_R dst, src1, src2` | dst = src1 / src2 (32-bit signed) |
| `0x14` | `MOD_I32_R` | `MOD_I32_R dst, src1, src2` | dst = src1 % src2 (32-bit signed) |
| `0x15` | `NEG_I32_R` | `NEG_I32_R dst, src` | dst = -src (32-bit signed) |
| `0x16` | `INC_I32_R` | `INC_I32_R reg` | reg = reg + 1 (32-bit signed) |
| `0x17` | `DEC_I32_R` | `DEC_I32_R reg` | reg = reg - 1 (32-bit signed) |

### 64-bit Integer Arithmetic

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x18` | `ADD_I64_R` | `ADD_I64_R dst, src1, src2` | dst = src1 + src2 (64-bit signed) |
| `0x19` | `SUB_I64_R` | `SUB_I64_R dst, src1, src2` | dst = src1 - src2 (64-bit signed) |
| `0x1A` | `MUL_I64_R` | `MUL_I64_R dst, src1, src2` | dst = src1 * src2 (64-bit signed) |
| `0x1B` | `DIV_I64_R` | `DIV_I64_R dst, src1, src2` | dst = src1 / src2 (64-bit signed) |
| `0x1C` | `MOD_I64_R` | `MOD_I64_R dst, src1, src2` | dst = src1 % src2 (64-bit signed) |
| `0x1D` | `NEG_I64_R` | `NEG_I64_R dst, src` | dst = -src (64-bit signed) |

### Floating-Point Arithmetic

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x20` | `ADD_F32_R` | `ADD_F32_R dst, src1, src2` | dst = src1 + src2 (32-bit float) |
| `0x21` | `SUB_F32_R` | `SUB_F32_R dst, src1, src2` | dst = src1 - src2 (32-bit float) |
| `0x22` | `MUL_F32_R` | `MUL_F32_R dst, src1, src2` | dst = src1 * src2 (32-bit float) |
| `0x23` | `DIV_F32_R` | `DIV_F32_R dst, src1, src2` | dst = src1 / src2 (32-bit float) |
| `0x24` | `NEG_F32_R` | `NEG_F32_R dst, src` | dst = -src (32-bit float) |
| `0x25` | `ABS_F32_R` | `ABS_F32_R dst, src` | dst = abs(src) (32-bit float) |
| `0x26` | `SQRT_F32_R` | `SQRT_F32_R dst, src` | dst = sqrt(src) (32-bit float) |

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x28` | `ADD_F64_R` | `ADD_F64_R dst, src1, src2` | dst = src1 + src2 (64-bit float) |
| `0x29` | `SUB_F64_R` | `SUB_F64_R dst, src1, src2` | dst = src1 - src2 (64-bit float) |
| `0x2A` | `MUL_F64_R` | `MUL_F64_R dst, src1, src2` | dst = src1 * src2 (64-bit float) |
| `0x2B` | `DIV_F64_R` | `DIV_F64_R dst, src1, src2` | dst = src1 / src2 (64-bit float) |
| `0x2C` | `NEG_F64_R` | `NEG_F64_R dst, src` | dst = -src (64-bit float) |
| `0x2D` | `ABS_F64_R` | `ABS_F64_R dst, src` | dst = abs(src) (64-bit float) |
| `0x2E` | `SQRT_F64_R` | `SQRT_F64_R dst, src` | dst = sqrt(src) (64-bit float) |

### Bitwise Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x30` | `AND_R` | `AND_R dst, src1, src2` | dst = src1 & src2 (bitwise AND) |
| `0x31` | `OR_R` | `OR_R dst, src1, src2` | dst = src1 \| src2 (bitwise OR) |
| `0x32` | `XOR_R` | `XOR_R dst, src1, src2` | dst = src1 ^ src2 (bitwise XOR) |
| `0x33` | `NOT_R` | `NOT_R dst, src` | dst = ~src (bitwise NOT) |
| `0x34` | `SHL_R` | `SHL_R dst, src, shift` | dst = src << shift (left shift) |
| `0x35` | `SHR_R` | `SHR_R dst, src, shift` | dst = src >> shift (right shift) |

---

## Memory Operations (28 opcodes)

### Register Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x40` | `MOVE` | `MOVE dst, src` | Copy value from src register to dst register |
| `0x41` | `MOVE_WIDE` | `MOVE_WIDE dst, src` | Copy 64-bit value between registers |
| `0x42` | `SWAP` | `SWAP reg1, reg2` | Exchange values between two registers |
| `0x43` | `CLEAR` | `CLEAR reg` | Set register to zero/nil |

### Constant Loading

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x44` | `LOAD_CONST_I32` | `LOAD_CONST_I32 reg, const_idx` | Load 32-bit integer constant |
| `0x45` | `LOAD_CONST_I64` | `LOAD_CONST_I64 reg, const_idx` | Load 64-bit integer constant |
| `0x46` | `LOAD_CONST_F32` | `LOAD_CONST_F32 reg, const_idx` | Load 32-bit float constant |
| `0x47` | `LOAD_CONST_F64` | `LOAD_CONST_F64 reg, const_idx` | Load 64-bit float constant |
| `0x48` | `LOAD_CONST_STR` | `LOAD_CONST_STR reg, const_idx` | Load string constant |
| `0x49` | `LOAD_NIL` | `LOAD_NIL reg` | Load nil/null value |
| `0x4A` | `LOAD_TRUE` | `LOAD_TRUE reg` | Load boolean true |
| `0x4B` | `LOAD_FALSE` | `LOAD_FALSE reg` | Load boolean false |

### Local Variable Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x50` | `LOAD_LOCAL` | `LOAD_LOCAL reg, local_idx` | Load from local variable slot |
| `0x51` | `STORE_LOCAL` | `STORE_LOCAL local_idx, reg` | Store to local variable slot |
| `0x52` | `LOAD_LOCAL_WIDE` | `LOAD_LOCAL_WIDE reg, local_idx` | Load 64-bit from local slot |
| `0x53` | `STORE_LOCAL_WIDE` | `STORE_LOCAL_WIDE local_idx, reg` | Store 64-bit to local slot |

### Global Variable Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x54` | `LOAD_GLOBAL` | `LOAD_GLOBAL reg, global_idx` | Load from global variable |
| `0x55` | `STORE_GLOBAL` | `STORE_GLOBAL global_idx, reg` | Store to global variable |
| `0x56` | `LOAD_GLOBAL_WIDE` | `LOAD_GLOBAL_WIDE reg, global_idx` | Load 64-bit from global |
| `0x57` | `STORE_GLOBAL_WIDE` | `STORE_GLOBAL_WIDE global_idx, reg` | Store 64-bit to global |

### Array Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x58` | `ARRAY_NEW` | `ARRAY_NEW reg, size_reg` | Create new array of specified size |
| `0x59` | `ARRAY_GET` | `ARRAY_GET dst, arr_reg, idx_reg` | Load array element |
| `0x5A` | `ARRAY_SET` | `ARRAY_SET arr_reg, idx_reg, val_reg` | Store array element |
| `0x5B` | `ARRAY_LEN` | `ARRAY_LEN dst, arr_reg` | Get array length |

### Memory Management

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x5C` | `GC_BARRIER` | `GC_BARRIER reg` | Write barrier for GC |
| `0x5D` | `ALLOC_OBJ` | `ALLOC_OBJ reg, type_id, size` | Allocate object |
| `0x5E` | `FREE_OBJ` | `FREE_OBJ reg` | Explicitly free object |
| `0x5F` | `GC_COLLECT` | `GC_COLLECT` | Force garbage collection |

---

## Control Flow Operations (18 opcodes)

### Unconditional Jumps

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x60` | `JUMP` | `JUMP offset` | Unconditional jump (16-bit offset) |
| `0x61` | `JUMP_LONG` | `JUMP_LONG offset` | Long unconditional jump (32-bit offset) |
| `0x62` | `JUMP_BACK` | `JUMP_BACK offset` | Backward jump for loops |

### Conditional Jumps

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x63` | `JUMP_IF_R` | `JUMP_IF_R reg, offset` | Jump if register is truthy |
| `0x64` | `JUMP_IF_NOT_R` | `JUMP_IF_NOT_R reg, offset` | Jump if register is falsy |
| `0x65` | `JUMP_IF_ZERO` | `JUMP_IF_ZERO reg, offset` | Jump if register equals zero |
| `0x66` | `JUMP_IF_NOT_ZERO` | `JUMP_IF_NOT_ZERO reg, offset` | Jump if register not zero |
| `0x67` | `JUMP_IF_NIL` | `JUMP_IF_NIL reg, offset` | Jump if register is nil |
| `0x68` | `JUMP_IF_NOT_NIL` | `JUMP_IF_NOT_NIL reg, offset` | Jump if register not nil |

### Function Calls

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x70` | `CALL_DIRECT` | `CALL_DIRECT func_idx, argc` | Direct function call |
| `0x71` | `CALL_INDIRECT` | `CALL_INDIRECT reg, argc` | Indirect function call |
| `0x72` | `CALL_BUILTIN` | `CALL_BUILTIN builtin_id, argc` | Built-in function call |
| `0x73` | `RETURN` | `RETURN reg` | Return with value |
| `0x74` | `RETURN_VOID` | `RETURN_VOID` | Return without value |

### Loop Control

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x75` | `LOOP` | `LOOP offset` | Loop back to start |
| `0x76` | `BREAK` | `BREAK loop_id` | Break from loop |
| `0x77` | `CONTINUE` | `CONTINUE loop_id` | Continue loop iteration |

---

## Comparison Operations (15 opcodes)

### Integer Comparisons

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x80` | `EQ_R` | `EQ_R dst, src1, src2` | dst = (src1 == src2) |
| `0x81` | `NE_R` | `NE_R dst, src1, src2` | dst = (src1 != src2) |
| `0x82` | `LT_I32_R` | `LT_I32_R dst, src1, src2` | dst = (src1 < src2) signed 32-bit |
| `0x83` | `LE_I32_R` | `LE_I32_R dst, src1, src2` | dst = (src1 <= src2) signed 32-bit |
| `0x84` | `GT_I32_R` | `GT_I32_R dst, src1, src2` | dst = (src1 > src2) signed 32-bit |
| `0x85` | `GE_I32_R` | `GE_I32_R dst, src1, src2` | dst = (src1 >= src2) signed 32-bit |

### Unsigned Integer Comparisons

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x86` | `LT_U32_R` | `LT_U32_R dst, src1, src2` | dst = (src1 < src2) unsigned 32-bit |
| `0x87` | `LE_U32_R` | `LE_U32_R dst, src1, src2` | dst = (src1 <= src2) unsigned 32-bit |
| `0x88` | `GT_U32_R` | `GT_U32_R dst, src1, src2` | dst = (src1 > src2) unsigned 32-bit |
| `0x89` | `GE_U32_R` | `GE_U32_R dst, src1, src2` | dst = (src1 >= src2) unsigned 32-bit |

### Float Comparisons

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x8A` | `LT_F64_R` | `LT_F64_R dst, src1, src2` | dst = (src1 < src2) 64-bit float |
| `0x8B` | `LE_F64_R` | `LE_F64_R dst, src1, src2` | dst = (src1 <= src2) 64-bit float |
| `0x8C` | `GT_F64_R` | `GT_F64_R dst, src1, src2` | dst = (src1 > src2) 64-bit float |
| `0x8D` | `GE_F64_R` | `GE_F64_R dst, src1, src2` | dst = (src1 >= src2) 64-bit float |

### Boolean Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x8E` | `NOT_BOOL_R` | `NOT_BOOL_R dst, src` | dst = !src (logical NOT) |

---

## Type Operations (24 opcodes)

### Type Casting

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x90` | `CAST_I32_I64` | `CAST_I32_I64 dst, src` | Cast 32-bit int to 64-bit int |
| `0x91` | `CAST_I64_I32` | `CAST_I64_I32 dst, src` | Cast 64-bit int to 32-bit int |
| `0x92` | `CAST_I32_F32` | `CAST_I32_F32 dst, src` | Cast 32-bit int to 32-bit float |
| `0x93` | `CAST_I32_F64` | `CAST_I32_F64 dst, src` | Cast 32-bit int to 64-bit float |
| `0x94` | `CAST_F32_I32` | `CAST_F32_I32 dst, src` | Cast 32-bit float to 32-bit int |
| `0x95` | `CAST_F64_I32` | `CAST_F64_I32 dst, src` | Cast 64-bit float to 32-bit int |
| `0x96` | `CAST_F32_F64` | `CAST_F32_F64 dst, src` | Cast 32-bit float to 64-bit float |
| `0x97` | `CAST_F64_F32` | `CAST_F64_F32 dst, src` | Cast 64-bit float to 32-bit float |

### Boolean Conversions

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x98` | `CAST_I32_BOOL` | `CAST_I32_BOOL dst, src` | Cast 32-bit int to boolean |
| `0x99` | `CAST_BOOL_I32` | `CAST_BOOL_I32 dst, src` | Cast boolean to 32-bit int |
| `0x9A` | `CAST_F64_BOOL` | `CAST_F64_BOOL dst, src` | Cast 64-bit float to boolean |
| `0x9B` | `CAST_BOOL_F64` | `CAST_BOOL_F64 dst, src` | Cast boolean to 64-bit float |

### String Conversions

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0x9C` | `CAST_I32_STR` | `CAST_I32_STR dst, src` | Cast 32-bit int to string |
| `0x9D` | `CAST_F64_STR` | `CAST_F64_STR dst, src` | Cast 64-bit float to string |
| `0x9E` | `CAST_BOOL_STR` | `CAST_BOOL_STR dst, src` | Cast boolean to string |

### Type Checking

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xA0` | `TYPE_CHECK` | `TYPE_CHECK dst, reg, type_id` | Check if register has type |
| `0xA1` | `TYPE_OF` | `TYPE_OF dst, reg` | Get type ID of register value |
| `0xA2` | `INSTANCE_OF` | `INSTANCE_OF dst, reg, class_id` | Check class instance |
| `0xA3` | `IS_NIL` | `IS_NIL dst, reg` | Check if register is nil |
| `0xA4` | `IS_NUMBER` | `IS_NUMBER dst, reg` | Check if register is numeric |
| `0xA5` | `IS_STRING` | `IS_STRING dst, reg` | Check if register is string |
| `0xA6` | `IS_BOOL` | `IS_BOOL dst, reg` | Check if register is boolean |
| `0xA7` | `IS_ARRAY` | `IS_ARRAY dst, reg` | Check if register is array |
| `0xA8` | `IS_FUNCTION` | `IS_FUNCTION dst, reg` | Check if register is function |

---

## Special Operations (18 opcodes)

### I/O Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xB0` | `PRINT_R` | `PRINT_R reg` | Print register value |
| `0xB1` | `PRINT_STR` | `PRINT_STR str_idx` | Print string constant |
| `0xB2` | `PRINT_NEWLINE` | `PRINT_NEWLINE` | Print newline character |
| `0xB3` | `READ_INPUT` | `READ_INPUT reg` | Read input from user |

### Debug Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xB4` | `DEBUG_BREAK` | `DEBUG_BREAK` | Debugger breakpoint |
| `0xB5` | `DEBUG_PRINT` | `DEBUG_PRINT reg` | Debug print register |
| `0xB6` | `TRACE_ENTER` | `TRACE_ENTER func_id` | Enter function trace |
| `0xB7` | `TRACE_EXIT` | `TRACE_EXIT func_id` | Exit function trace |

### System Operations

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xB8` | `TIME_STAMP` | `TIME_STAMP reg` | Get current timestamp |
| `0xB9` | `RANDOM` | `RANDOM reg` | Generate random number |
| `0xBA` | `EXIT` | `EXIT code_reg` | Exit program with code |
| `0xBB` | `HALT` | `HALT` | Halt VM execution |

### Exception Handling

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xBC` | `THROW` | `THROW reg` | Throw exception |
| `0xBD` | `TRY_BEGIN` | `TRY_BEGIN handler_offset` | Begin try block |
| `0xBE` | `TRY_END` | `TRY_END` | End try block |
| `0xBF` | `CATCH` | `CATCH reg, type_id` | Catch exception |

### VM Control

| Opcode | Name | Format | Description |
|--------|------|--------|-------------|
| `0xC0` | `NOP` | `NOP` | No operation |
| `0xC1` | `YIELD` | `YIELD` | Yield execution |

---

## Register Encoding

### Register Types

| Range | Type | Purpose |
|-------|------|---------|
| R0-R63 | Global | Long-lived variables, module state |
| R64-R191 | Frame | Function parameters, local variables |
| R192-R239 | Temporary | Expression evaluation, intermediates |
| R240-R255 | Module | Import/export bindings |

### Special Registers

| Register | Name | Purpose |
|----------|------|---------|
| R0 | PC | Program Counter (read-only) |
| R1 | SP | Stack Pointer |
| R2 | FP | Frame Pointer |
| R3 | EH | Exception Handler |
| R4-R15 | Reserved | System use |

---

## Constant Pool Layout

### Constant Types

| Type ID | Type | Encoding |
|---------|------|----------|
| 0x01 | Integer (32-bit) | Little-endian |
| 0x02 | Integer (64-bit) | Little-endian |
| 0x03 | Float (32-bit) | IEEE 754 |
| 0x04 | Float (64-bit) | IEEE 754 |
| 0x05 | String | UTF-8 length-prefixed |
| 0x06 | Boolean | Single byte (0/1) |
| 0x07 | Nil | No data |

### Constant Pool Structure

```
┌─────────────┬─────────────┬─────────────┬─────────────┐
│  Count      │  Type       │  Length     │  Data       │
│  (32-bit)   │  (8-bit)    │  (32-bit)   │  (variable) │
└─────────────┴─────────────┴─────────────┴─────────────┘
```

---

## Instruction Performance Characteristics

### Dispatch Performance

| Dispatch Type | Cycles/Instruction | Notes |
|---------------|-------------------|-------|
| Computed Goto | 1.2 | GCC/Clang with labels-as-values |
| Switch Statement | 1.5 | Portable C implementation |
| Threaded Code | 1.0 | Direct threading (future) |

### Operation Performance

| Category | Typical Cycles | Cache Friendly |
|----------|---------------|----------------|
| Arithmetic | 1-3 | ✅ |
| Memory Ops | 2-5 | ✅ |
| Control Flow | 2-10 | ⚠️ |
| Type Ops | 3-8 | ✅ |
| Special Ops | 5-50 | ❌ |

---

## Example Programs

### Simple Arithmetic

```orus
x = 42
y = 24
result = x + y
print(result)
```

**Generated Bytecode:**
```
LOAD_CONST_I32 R64, 0     ; Load 42 into R64
LOAD_CONST_I32 R65, 1     ; Load 24 into R65  
ADD_I32_R R66, R64, R65   ; R66 = R64 + R65
PRINT_R R66               ; Print result
RETURN_VOID               ; End program
```

### Loop Example

```orus
for i in 0..10 {
    print(i)
}
```

**Generated Bytecode:**
```
LOAD_CONST_I32 R64, 0     ; i = 0
LOAD_CONST_I32 R65, 10    ; end = 10
LE_I32_R R66, R64, R65    ; condition: i <= 10
JUMP_IF_NOT_R R66, 8      ; exit if condition false
PRINT_R R64               ; print(i)
INC_I32_R R64             ; i++
JUMP_BACK -5              ; loop back
RETURN_VOID               ; end
```

### Type Casting

```orus
x = 42
text = x as string
flag = x as bool
```

**Generated Bytecode:**
```
LOAD_CONST_I32 R64, 0     ; x = 42
CAST_I32_STR R65, R64     ; text = x as string
CAST_I32_BOOL R66, R64    ; flag = x as bool
RETURN_VOID
```

This instruction set provides comprehensive coverage of the Orus language features while maintaining efficient execution characteristics in the register-based VM architecture.