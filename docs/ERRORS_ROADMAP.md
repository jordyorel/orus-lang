# Orus Error Roadmap (Future Coverage)

This roadmap captures diagnostics that Orus does **not** report yet, but which developers routinely encounter in other languages. It focuses on the path from variables to modules and cites the ecosystems that inspired each idea so we can prioritise parity.

## Variables & Bindings

| Candidate Diagnostic | Inspiration | Why It Matters |
| :--- | :--- | :--- |
| Unused binding warning | Rust (`unused variable`), TypeScript `no-unused-vars` | Highlights dead locals and encourages `_` or `_name` conventions for intentional ignores. |
| Conditional use-before-init | Rust `E0381`, Swift definite initialisation | Detects branches where a local may be read before ever being assigned. |
| Shadowing outer variable | Rust `E0415`, Python linters | Prevents subtle bugs where an inner declaration hides a parent binding. |
| Immutable capture mutation | Swift/TypeScript closure rules | Flags attempts to mutate values captured by value inside closures unless marked `mut`. |
| Destructuring arity mismatch | JS/TS destructuring errors | Reports when tuple/array patterns do not match the length or shape of the initializer. |
| Pattern must bind all fields | Rust `E0027`, Scala match errors | Ensures struct/tuple destructuring lists every field or explicitly ignores the rest. |

## Type Flow & Functions

| Candidate Diagnostic | Inspiration | Why It Matters |
| :--- | :--- | :--- |
| Mismatched return type | Rust `E0308`, TypeScript return checks | Guarantees each code path yields a value compatible with the declared return type. |
| Missing return in non-`void` function | Swift definite return analysis | Prevents silent fallthrough when a function promises to return a value. |
| Implicit numeric narrowing warning | Swift `E_POTENTIAL_OVERFLOW`, C# CS0665 | Catches precision loss when converting from wider to narrower numeric types without `as`. |
| Generic constraint failure | Rust trait bounds, TypeScript extends clauses | Surfaces when inferred or annotated type parameters do not satisfy declared constraints. |
| Function arity with default/variadic params | Python argument check, Swift parameter labels | Validates mixing of positional, named, and default arguments before we add default-arg support. |
| Unreachable default branch | Rust `match` completeness, Kotlin `when` | Warns when pattern matching has redundant `else`/`_` arms because earlier cases are exhaustive. |

## Control Flow & Pattern Safety

| Candidate Diagnostic | Inspiration | Why It Matters |
| :--- | :--- | :--- |
| Missing `else` for total boolean check | Swift exhaustiveness hints | Encourages handling both truthy and falsy cases when evaluating boolean derived from enums/options. |
| Loop never iterates warning | Rust `unused labels`, Clang `-Wempty-body` | Points out ranges or conditions that statically evaluate to zero iterations. |
| Fallthrough between `match` arms | Swift `switch` fallthrough errors | Forbids implicit fallthrough once we add statement-form pattern matching. |
| Capturing loop variable in closures | JavaScript `for`-closure gotcha warnings | Guards against closures accidentally reusing the same iterator instance. |
| Invalid break label target | Rust `E0767` | Validates that labelled `break`/`continue` jumps refer to an existing loop context. |

## Modules & Imports

| Candidate Diagnostic | Inspiration | Why It Matters |
| :--- | :--- | :--- |
| Private symbol access | Rust `E0603`, TypeScript `TS2440` | Enforces module visibility rules when loading exports. |
| Duplicate import alias | ES module loaders, Rust `E0252` | Prevents two imports from creating the same local identifier. |
| Conflicting glob imports | Rust glob collision warnings | Detects ambiguous names when multiple modules are wildcard-imported. |
| Unused import warning | Rust `unused import`, TypeScript `no-unused-vars` | Keeps dependency graphs tidy and improves compile times. |
| Platform/feature gated module | Rust `cfg` errors, Swift availability | Warns when importing modules that are unavailable for the current build target or feature flag. |
| Cyclic re-export detection | Rust `E0364` | Extends current cyclic detection to cover re-export chains (`pub use`). |

## Implementation Notes

- **Error codes:** reserve contiguous ranges (e.g., extend E101x for binding checks, E201x for advanced type flow, E301x for import hygiene) once concrete designs are ready.
- **Testing:** each diagnostic should ship with fixtures in `tests/error_reporting/` and golden output similar to existing structured errors.
- **Surfacing:** add quick-fix suggestions where possible (e.g., "rename to `_var`" for unused bindings) to match the mentor tone used in current error messaging.
- **Telemetry:** log adoption metrics once implemented so we can tune severity (error vs warning) based on real-world noise.
