# Type Error Test Suite

This document describes the comprehensive type error tests for the Orus language type system.

## Test Organization

### 📁 `tests/type_safety_fails/` - Error Cases
Tests that should **fail compilation** with friendly error messages:

#### Type Mismatch Errors
- `type_mismatch_string_to_int.orus` - String assigned to i32 variable
- `type_mismatch_bool_to_float.orus` - Boolean assigned to f64 variable  
- `type_mismatch_float_to_bool.orus` - Float assigned to bool variable

#### Invalid Cast Errors
- `invalid_cast_string_to_int.orus` - String cast to i32 (not allowed)
- `invalid_cast_string_to_bool.orus` - String cast to bool (not allowed)
- `invalid_cast_string_to_float.orus` - String cast to f64 (not allowed)

#### Mixed Arithmetic Errors
- `mixed_arithmetic_int_float.orus` - i32 + f64 operation
- `mixed_arithmetic_signed_unsigned.orus` - i32 + u32 operation  
- `mixed_arithmetic_different_sizes.orus` - i32 + i64 operation

#### Undefined Type Errors
- `undefined_type_cast.orus` - Cast to non-existent type
- `undefined_type_annotation.orus` - Variable with invalid type annotation

#### Complex Error Scenarios
- `complex_mixed_operations.orus` - Multiple type errors in sequence
- `chain_cast_with_error.orus` - Chain casting with invalid conversion

### 📁 `tests/types/` - Success Cases
Tests that should **pass compilation**:

- `valid_string_conversions.orus` - All types → string conversions
- `valid_numeric_conversions.orus` - Valid numeric type conversions

## Error Message Format

All error messages follow the friendly format from `docs/ERROR_FORMAT_REPORTING.md`:

```
-- CATEGORY: Friendly title -------------------------- file.orus:line:col

line | source code
     |      ^^^^ this is a `actual`, but `expected` was expected
     |
     = Main explanation of what went wrong
     = help: Helpful suggestion for fixing the issue
     = note: Additional context about why this matters
```

## Test Runner

Use `./test_type_errors.sh` to run all type error tests systematically:

```bash
chmod +x test_type_errors.sh
./test_type_errors.sh
```

## Key Features Tested

### ✅ **String Conversion Rules**
- ✅ All types can be cast **TO** string: `value as string`
- ❌ String **cannot** be cast to other types: `"text" as i32` → friendly error

### ✅ **Type Safety**
- ❌ Mixed arithmetic requires explicit casting: `i32 + f64` → helpful guidance
- ❌ Type mismatches show clear "expected vs found": `i32 = "text"` → friendly error
- ✅ Explicit casting works: `(value as f64) + other_f64` → success

### ✅ **Error Quality**
- Friendly, mentor-like tone (not blaming)
- Clear guidance on how to fix issues
- Consistent format across all error types
- Helpful context about why type safety matters

## Adding New Tests

When adding new type error tests:

1. **For errors**: Add to `tests/type_safety_fails/`
2. **For success**: Add to `tests/types/`
3. **Update test runner**: Add test case to `test_type_errors.sh`
4. **Include comments**: Explain what error is expected
5. **Test manually**: Verify error message is helpful and friendly