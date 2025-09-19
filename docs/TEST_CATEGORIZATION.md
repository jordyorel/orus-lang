# Test Categorization Update

## Structs
- `tests/structs/basic_struct.orus` – Positive coverage for struct defaults and impl registration.
- `tests/structs/method_call.orus` – Exercises namespaced method dispatch and struct literal returns.
- `tests/structs/implicit_self_call.orus` – Confirms instance method calls inject the implicit `self` receiver.

## Type Safety Fails
- `tests/type_safety_fails/struct_field_type_mismatch.orus` – Ensures mismatched default triggers a type error.
- `tests/type_safety_fails/impl_missing_struct.orus` – Verifies impl blocks require a previously-declared struct.
