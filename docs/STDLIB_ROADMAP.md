# Orus Standard Library Roadmap (Refactored)

> **Status:** The experimental `std/` Orus sources described below have been
> removed from the repository. This roadmap is retained for historical context
> while we rebuild the standard library around intrinsic modules.

This roadmap defines how the Orus standard library will evolve under the **Rust-style model**:

* **VM core** stays minimal.
* **Performance-critical intrinsics** live in C.
## 🔍 Current Readiness Snapshot

### ✅ Foundations already in place

* **Module discovery stays disk-backed.** The runtime now treats bare module
  names literally, adds the executable directory and any `ORUSPATH` entries to
  the search roots, and resolves modules without injecting `std/` prefixes or
  probing legacy stdlib install locations.【F:src/vm/runtime/vm.c†L1599-L1729】
* **Resolver failures surface loader context.** Missing modules report their
  normalized path, enumerate attempted search roots, and remind users about
  `ORUSPATH`, preserving the disk loader ergonomics without stdlib-specific
  messaging.【F:src/vm/runtime/vm.c†L1484-L1521】
* **Compiler bookkeeping for exports/imports exists.** Module compilation
  records exports with type metadata, tracks imports, and enforces that only
  value-like exports become bindings during `use` resolution.【F:src/compiler/backend/codegen/modules.c†L14-L214】
* **Selective and namespace imports are supported.** The statement compiler
  iterates module exports for `use math` and resolves explicit symbol lists with
  helpful diagnostics when a module is missing.【F:src/compiler/backend/codegen/statements.c†L1023-L1089】
* **Module manager API is ready for packaging.** Runtime modules maintain
  register files, export tables, and a registry for fast lookup, which is
  required for bundling stdlib bytecode once authored.【F:src/vm/module_manager.c†L168-L337】
* **Legacy std aliases have been removed.** The registry now enforces
  canonical `intrinsics/` names only, eliminating the helper that once
  accepted `std/` aliases.【F:src/vm/module_manager.c†L168-L337】
* **No builtin descriptor seeding yet.** Future intrinsic-backed std modules
  will need explicit registration logic because the runtime currently provides
  no placeholder metadata for legacy names.【F:src/vm/module_manager.c†L9-L117】
* **Resolver coverage is being rewritten.** Earlier tests validated the
  disk-backed stdlib modules, but those cases were removed along with the
  experimental `std/` sources. The remaining suite focuses on custom search
  roots via `ORUSPATH` while we rebuild intrinsic-backed coverage.【F:tests/modules/resolver/README.md†L1-L11】
* **Core intrinsics bind directly to native implementations.** The compiler now
  emits `OP_CALL_NATIVE_R` trampolines for `@[core]` functions and the loader
  wires module exports to the VM’s native table so calls dispatch into the C
  runtime, with dedicated backend coverage to lock in the behavior.【F:src/compiler/backend/codegen/functions.c†L472-L547】【F:src/vm/runtime/vm.c†L94-L214】【F:tests/unit/test_codegen_core_intrinsics.c†L1-L126】

### ⚠️ Gaps before executing this roadmap

* **No standard modules ship today.** Imports of the legacy names simply miss,
  and the loader reports the unresolved path alongside every search root it
  inspected so projects can vendor replacements or update `ORUSPATH`.【F:src/vm/runtime/vm.c†L1484-L1729】
* **Testing focuses on resolution, not semantics.** With the stdlib removed the
  suite exercises search roots and failure diagnostics; future semantic coverage
  awaits a new distribution.
* **Packaging workflow is manual.** While the runtime can load disk modules,
  developer tooling for distributing, validating, and refreshing stdlib content
  has not been scripted.

### 🛠️ Proposed tasks to close readiness gaps

1. **Polish attribute-bound intrinsics.**
   * Expand the stdlib wrappers so public APIs delegate to the new trampolines
     and verify cross-module re-exports continue to work.
   * Harden diagnostics around duplicate bindings or missing native
     implementations now that the VM wires symbols at load time.
   * Add integration coverage exercising module imports that forward these
     trampolines to downstream users.

2. **Expand the math module.**
   * Build out the remaining floating-point API (tangent family, exponentials,
     logarithms, etc.) on top of the intrinsic bindings now in place.
   * Document visibility for internal helpers and confirm that only intended
     symbols export through `use math`.
   * Provide regression tests for `math` wrappers to ensure values match the
     VM intrinsics within an acceptable tolerance.

3. **Broaden stdlib testing.**
   * ✅ Regression coverage now focuses exclusively on canonical module
     registration, matching the removal of alias-based resolution.
   * Add semantic tests that exercise math correctness, namespace hygiene, and
     attribute error reporting in addition to the current resolver suite.
   * Integrate the new tests into the CI matrix (or document the target harness
     if automation is pending) so regressions surface immediately.

4. **Automate stdlib packaging.**
   * Script a build step that compiles `std/` sources into bytecode bundles and
     copies them into release artifacts.
   * Provide validation tooling (hashes or manifest checks) to ensure the
     packaged stdlib matches the source tree.
   * Document the workflow for contributors so publishing updates remains
     consistent across platforms.

---

## 🎯 Goals

* Provide a **modular `std/` directory** shipped with Orus.
* Keep **C intrinsics hidden** behind `.orus` wrappers.
* Enforce **namespace discipline** (functions only exist under their module).
* Enable clean error reporting and consistent API documentation.

---

## 🗂️ Phase 0 – Scope & Conventions

1. **Core principle**:

   * No math function (`sin`, `pow`, etc.) is directly callable as a VM builtin.
   * They only exist through `use math`.

2. **Module structure**:

   * `std/math.orus` → Orus file, public API.
   * VM exposes internal C symbols prefixed with `__c_*`, inaccessible from userland.
   * Example:

     * `__c_sin` in C
     * `sin` in Orus (wrapper around `__c_sin`)

3. **Visibility rules**:

   * Public API = names without `_`.
   * Internal helpers = names starting with `_`, never exported by `use`.

---

## 🧱 Phase 1 – Loader Infrastructure

1. **Filesystem discovery**

   * VM locates `std/` next to the binary.
   * Honor `ORUSPATH` env var for overrides.

2. **Module resolver**

   * `use math` → binds everything from `std/math.orus`.
   * `use math: sin, pow` → binds only selected functions.
   * Error handling: missing module = compile-time diagnostic.

3. **Bytecode integration**

   * Stdlib functions compiled once and registered into import tables.
   * Compiler queries resolver when resolving `use` statements.

---

## 🧩 Phase 2 – Math Module Authoring

### **C Core (hidden intrinsics)**

```c
// core_math.c
double orus_sin(double x) { return sin(x); }
double orus_cos(double x) { return cos(x); }
double orus_pow(double a, double b) { return pow(a, b); }
double orus_sqrt(double x) { return sqrt(x); }
```

These are registered inside the VM under internal symbol names:
`__c_sin`, `__c_cos`, `__c_pow`, `__c_sqrt`.
They are *never directly visible* to user code.

The VM now exposes a **canonical intrinsic signature table** (see
`src/vm/runtime/vm.c`) that records each `__c_*` symbol’s arity and expected
Orus types. During type inference the compiler queries this registry to ensure
stdlib wrappers match the VM contract and to surface precise diagnostics when a
signature drifts.

---

### **Orus Wrapper (public stdlib module)**

```orus
// std/math.orus

@[core("__c_sin")]
fn __c_sin(x: f64) -> f64

@[core("__c_cos")]
fn __c_cos(x: f64) -> f64

@[core("__c_pow")]
fn __c_pow(base: f64, exponent: f64) -> f64

@[core("__c_sqrt")]
fn __c_sqrt(x: f64) -> f64


// Public API wrappers
pub fn sin(x: f64) -> f64: return __c_sin(x)
pub fn cos(x: f64) -> f64: return __c_cos(x)
pub fn pow(base: f64, exponent: f64) -> f64: return __c_pow(base, exponent)
pub fn sqrt(x: f64) -> f64: return __c_sqrt(x)


// Constants
pub PI: f64 := 3.141592653589793
pub E: f64 := 2.718281828459045
```

* The `@[core("...")]` attribute tells the compiler:

  > “This function’s body lives in the VM core under the given symbol.”
* Users never call `__c_*` directly — they only see `math.sin`, `math.cos`, etc.
* Typed AST nodes cache the validated intrinsic signature so later passes can
  reference the VM symbol when reporting errors.

### ✅ Current public surface

The math module currently exports the following bindings:

* `PI: f64`
* `E: f64`
* `fn sin(x: f64) -> f64`
* `fn cos(x: f64) -> f64`
* `fn pow(base: f64, exponent: f64) -> f64`
* `fn sqrt(x: f64) -> f64`

Historically each wrapper delegated to its corresponding `__c_*` intrinsic,
keeping the native entry points private while providing a stable surface for
Orus programs. That shim has been removed while we rebuild the module as a
pure intrinsic.

---

### **Usage Example**

```orus
// Historical example (the experimental stdlib has been removed)
use math
print(math.sin(math.PI / 2)) // 1.0

use math: pow
print(pow(2.0, 10.0))        // 1024.0
```

---

✅ With this structure:

* **VM intrinsics** remain hidden.
* **Stdlib provides the public API.**
* **Namespaces enforce modularity.**


---

## 🧪 Phase 3 – Testing & Tooling

### 1. **Unit Tests for Math Functions**

* Validate correctness of all math functions against known values.
* Example tests under `tests/std/math.orus`:

  ```orus
  use math

  assert math.sin(math.PI / 2) == 1.0
  assert math.cos(0.0) == 1.0
  assert math.pow(2.0, 8.0) == 256.0
  assert math.sqrt(9.0) == 3.0
  ```


### 2. **Import Resolution & Visibility Tests**

* Confirm that public functions/constants can be imported:

  ```orus
  use math: sin, PI
  assert sin(PI / 2) == 1.0
  ```
* Confirm that internal VM intrinsics (`__c_*`) **cannot** be imported or used directly:

  ```orus
  use math: __c_sin   // ❌ should fail at compile-time
  ```
* Expected diagnostic:

  ```
  error: symbol `__c_sin` is not exported by module `math`
  ```


### 3. **Negative Tests for Direct Access**

* Ensure user code cannot bypass stdlib and call VM hooks:

```orus
__c_sin(1.0)   // ❌ should fail: unknown symbol
```
* Compiler should reject `__c_*` identifiers outside stdlib files.


### 4. **Performance Guardrails**

* Benchmark wrapper calls vs direct C calls to confirm negligible overhead.
* Example: `math.sin` should be within ~1–2% of calling `sin()` in C directly.


✅ With this, you guarantee:

* **Correctness** (math works).
* **Encapsulation** (only `math.*` API is visible).
* **Security** (no userland direct access to VM internals).
* **Performance** (wrappers are essentially zero-cost).

---

## 📦 Phase 4 – Packaging & Developer Experience

1. **Distribution**

   * Bundle `std/` alongside Orus binary.
   * Ensure fallback works if `std/` is missing (clear error message).

2. **Developer workflow**

   * Stdlib modules can be edited/replaced without recompiling the VM.
   * Document how `extern fn` bindings map to hidden C intrinsics.
   * Provide tooling/scripts for syncing stdlib changes without rebuilding the VM.
   * Document `ORUSPATH` usage for contributors and external developers.

3. **Docs & examples**

   * Update `docs/STDLIB.md` with examples for `math`.
   * Show how to import selectively (`use math: sin`).


---

## ✅ Completion Criteria (for `math`)

* VM loads `std/math.orus` from disk.
* Functions (`sin`, `cos`, `pow`, `sqrt`) callable only via `use math`.
* Internal C hooks (`__c_*`) are inaccessible to user code.
* Constants (`PI`, `E`) exposed under `math`.
* Tests confirm correctness + import enforcement.
* Documentation reflects the new modular design.

---

👉 Next step after `math`: replicate the same **C core + Orus wrapper** pattern for `time`, `random`, and `string`.
