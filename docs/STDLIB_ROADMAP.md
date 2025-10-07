# Orus Standard Library Roadmap

This roadmap tracks the work required to ship the Orus standard library as disk-based `.orus` modules that load dynamically at runtime. The milestones below build on the compiler and VM architecture documented in [`COMPILER_DESIGN.md`](COMPILER_DESIGN.md) and complement the outstanding feature work listed in [`MISSING.md`](MISSING.md).

---

## 🎯 Goals

* Ship a modular `std/` directory that the VM can load without recompilation.
* Preserve startup performance and VM stability while moving module loading to disk.
* Provide a testable, extensible foundation for future standard library packages.

---

## 🗂️ Phase 0 – Inventory & Alignment

1. **Audit current builtins**

   * Catalog all functionality currently embedded in C strings or native helpers.
   * Identify which pieces migrate to `.orus` modules versus staying native for performance-critical paths.
2. **Define module boundaries**

   * Draft the initial module list:
     * `math` (abs, sqrt, pow, trig)
     * `string` (split, join, search, case ops)
     * `array` (push, pop, slice, map/filter/reduce)
     * `io` (read_file, write_file)
     * `system` (args, env)
     * `time` (timestamp, sleep, formatting)
     * `random` (srand, rand, distributions)
     * `json` (parse, stringify)
     * `collections` (set, dict/map, queue)
     * `regex` (search, match, replace)
3. **Agree on coding conventions**

   * Document style, naming, and module public API patterns for stdlib authors.

---

## 🧱 Phase 1 – Loader Infrastructure

1. **Filesystem discovery**

   * Update the VM boot sequence to locate the `std/` directory next to the binary.
   * Respect the `ORUSPATH` environment variable for overrides and multiple search roots.
2. **Module resolver**

   * Implement an import resolver that maps `use foo: bar` to `std/foo.orus`.
   * Cache module lookups and report diagnostics when files are missing.
3. **Bytecode integration hooks**

   * Ensure compiled stdlib modules are registered with the VM’s module/import tables.
   * Expose hooks so compiler passes can request stdlib symbols during compilation.
4. **Module resolver**

    * When use foo → load std/foo.orus and export all public symbols.

    * When use foo: a, b, c → load std/foo.orus, but only bind selected symbols into the caller scope.

    * Visibility rules:

    * Inside modules, prefix all internal-only helpers with _ (private by convention).

    * Only symbols without _ are exported by default.

---

## 🧩 Phase 2 – Module Authoring

1. **Bootstrap core modules**

   * Port baseline helpers into `.orus` files:

     * `math`, `string`, `array`, `io`, `system`, `time`, `random`.
   * Provide thin wrappers around performance-critical VM builtins where needed.
2. **Shared utilities**

   * Factor common helpers (error formatting, argument validation) into reusable modules.
3. **Documentation & examples**

   * Add usage examples to `docs/TUTORIAL.md` demonstrating `use` statements and stdlib APIs.

* **Example: `math.orus`**

```orus
// std/math.orus

// Exported by default
fn sin(x: f64) -> f64: ...
fn cos(x: f64) -> f64: ...
fn pow(x: f64, y: f64) -> f64: ...

// Private helper (not exported if `use math`)
fn _deg2rad(d: f64) -> f64: ...
```

* **Usage**:

```orus
use math           // brings all the methods
use math: sin, pow // brings only sin and pow
```

---

## 🧪 Phase 3 – Testing & Tooling

1. **Unit and regression suites**

   * Create targeted tests for each stdlib module under `tests/std/`.
   * Integrate these suites into the existing `make test` workflow and CI.
2. **Import resolution tests**

   * Add scenarios that validate `ORUSPATH` overrides and nested module imports.
3. **Performance guardrails**

   * Benchmark startup and hot paths to ensure the disk-based loader meets latency budgets.

---

## 📦 Phase 4 – Distribution & Developer Experience

1. **Packaging updates**

   * Modify build scripts and release packaging to ship the `std/` directory alongside the binary.
   * Document fallback behavior (e.g., future embedded bundle) for standalone binaries.
2. **Developer workflow**

   * Provide tooling/scripts for syncing stdlib changes without rebuilding the VM.
   * Document `ORUSPATH` usage for contributors and external developers.
3. **Roadmap synchronization**

   * Keep this roadmap, `MISSING.md`, and `IMPLEMENTATION_GUIDE.md` aligned after each milestone.

---

## 🗂️ Future Phases – Extended Modules

* `json` → config parsing, API integration.
* `collections` → sets, dicts/maps, queues, stacks.
* `regex` → pattern matching utilities.

---

## ✅ Completion Criteria

* VM loads stdlib modules from disk by default and honors `ORUSPATH` overrides.
* Core stdlib modules (`base`, `math`, `string`, `array`, `io`, `system`, `time`, `random`) exist as `.orus` files with comprehensive automated tests.
* Extended stdlib modules (`json`, `collections`, `regex`) are planned and partially implemented.
* Release artifacts bundle the stdlib, and documentation reflects the new workflow.
* Roadmap and reference docs stay updated as modules evolve.

