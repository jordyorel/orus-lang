# Orus Standard Library Roadmap (Refactored)

This roadmap defines how the Orus standard library will evolve under the **Rust-style model**:

* **VM core** stays minimal.
* **Performance-critical intrinsics** live in C.
* **Stdlib modules** (disk-based `.orus` files) wrap those intrinsics.
* Users only access functionality via `use module`, never by calling raw C hooks.

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

Exposed to the VM as `__c_sin`, `__c_cos`, etc.

### **Orus Wrapper**

```orus
// std/math.orus

extern fn __c_sin(f64) -> f64
extern fn __c_cos(f64) -> f64
extern fn __c_pow(f64, f64) -> f64
extern fn __c_sqrt(f64) -> f64

fn sin(x: f64) -> f64: return __c_sin(x)
fn cos(x: f64) -> f64: return __c_cos(x)
fn pow(a: f64, b: f64) -> f64: return __c_pow(a, b)
fn sqrt(x: f64) -> f64: return __c_sqrt(x)

const PI: f64 = 3.141592653589793
const E: f64 = 2.718281828459045
```

### **Usage**

```orus
use math
print(math.sin(math.PI / 2)) // 1.0

use math: pow
print(pow(2.0, 10.0))        // 1024.0
```

---

## 🧪 Phase 3 – Testing & Tooling

1. **Unit tests**

   * Validate all `math` functions against known values.
   * Place under `tests/std/math.orus`.

2. **Import resolution tests**

   * Ensure `use math` loads functions only via module, not directly.

3. **Performance baselines**

   * Benchmark calls (`math.sin`) vs direct C to confirm negligible overhead.

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
