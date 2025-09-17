# Test Categorization Update

## Structs
- `tests/structs/basic_struct.orus` – Positive coverage for struct defaults and impl registration.

## Type Safety Fails
- `tests/type_safety_fails/struct_field_type_mismatch.orus` – Ensures mismatched default triggers a type error.
- `tests/type_safety_fails/impl_missing_struct.orus` – Verifies impl blocks require a previously-declared struct.
