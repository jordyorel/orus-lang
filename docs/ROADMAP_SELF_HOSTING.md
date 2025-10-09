# Orus Self-Hosted Standard Library Roadmap

## 1. Executive Summary
- **Baseline:** The `std/` tree is intentionally empty and reserved for the upcoming self-hosted modules; only scaffolding files exist today while the architecture is defined.【F:std/README.md†L1-L4】
- **Goal:** Deliver a public standard library authored entirely in Orus that executes on top of a minimal kernel of VM primitives.
- **Strategy:** Build the three-layer stack (kernel primitives → Orus prelude → public `std/` modules) so that Orus can compile, load, and run its own standard library without relying on opaque native shims.

## 2. Scope & Success Criteria
- **Scope:** All modules that will live under `std/`, the runtime primitives they require, and the boot sequence that loads the library for user programs.
- **"Fully self-hosted" definition:**
  1. VM exposes only the kernel primitive surface; public modules call into Orus-defined APIs.
  2. Bootstrapping loads an Orus prelude before any user code, and every public API originates from Orus source files.
  3. Standard tests cover IO, FS, math accuracy, random determinism, collections, and formatting on at least two architectures.
- **Exit criteria:** Native scaffolding is limited to kernel primitives, CI runs solely against the Orus implementations, and the runtime ships no helper modules compiled from C.

## 3. Guiding Principles
1. **Start from a clean slate.** All new modules must be authored in Orus, and their APIs become the canonical surface once stabilized.
2. **Keep the kernel primitive surface tiny and stable.** Only provide syscall-style entry points (IO, FS, RNG, time, env, exit). Everything else must be implemented in Orus.
3. **Layered ownership.** Kernel primitives live in C, the prelude is pure Orus and auto-loaded, and `std/` exposes user-facing modules that compose prelude helpers.
4. **Test-driven expansion.** Each phase ships with regression suites that lock in behavior before promoting the new APIs.

## 4. Baseline Integrity & Guardrails
- **Automated audit:** `scripts/verify_std_baseline.py` enforces that `std/` contains no `.orus` modules yet and that repository sources do not import unreleased standard modules ahead of the kernel plan.【F:scripts/verify_std_baseline.py†L1-L58】
- **Repository layout:** `std/` contains only documentation describing the reserved state so toolchains cannot silently depend on unvetted modules.【F:std/README.md†L1-L4】
- **Resolver tests:** The resolver README highlights that only generic search-path scenarios remain until new modules are added, keeping the test suite aligned with the clean baseline.【F:tests/modules/resolver/README.md†L1-L12】

## 5. Architectural Layers & Ownership
| Layer | Owner | Responsibilities | Deliverables |
| --- | --- | --- | --- |
| Kernel primitives | VM / C runtime | Syscall-style opcodes: `io_write`, `io_read`, `fs_open/close/seek`, `rng_fill`, `time_monotonic`, `exit`, `env_get` | Stable ABI doc, conformance tests, deprecation policy |
| Prelude (`std/prelude.orus`) | Pure Orus | `print`/`println`, formatting, panic/error helpers, deterministic RNG wrappers, kernel shims | Auto-loaded bytecode blob, formatter tests, RNG reproducibility fixtures |
| Public stdlib (`std/*.orus`) | Pure Orus | `io`, `fs`, `time`, `random`, `math`, `bytes`, `collections`, `regex`, `json`, etc. | Module docs, API stability guarantees, semantic/regression tests |

## 6. Phase Plan

### Phase -1: Kernel Primitive Definition & Feature Flagging
- Implement syscall table and opcodes for IO, FS, RNG, time, env, and exit.
- Add runtime feature flags to toggle between the existing VM opcodes and Orus prelude implementations for `print` and array helpers.
- Document the primitive ABI and add VM tests covering error paths.

### Phase 0: Prelude Bootstrap
- Author `std/__kernel.orus` exposing typed signatures that wrap the future kernel primitives once they are stabilized.
- Build `std/prelude.orus` implementing `print`, `println`, formatting, panic hooks, and RNG seeding in pure Orus.
- Extend the VM boot sequence to load the prelude before executing user modules.

### Phase 1: IO, Time, Random Core Services
- Implement `std/io`, `std/time`, and `std/random` atop the new primitives.
- Provide deterministic RNG (e.g., xoroshiro128+) in Orus with `rng_fill` reserved for entropy refreshes.
- Backfill unit and property tests for IO buffering, monotonic time bounds, and RNG distribution.

### Phase 2: Data & Memory Utilities
- Move array algorithms (`push`, `pop`, `map`, `filter`, `reduce`, `reserve`) into `std/collections` written in Orus.
- Introduce safe buffer abstractions (slices, mutable views) powered by kernel allocation helpers if needed.
- Port UTF-8 encode/decode and substring manipulation into Orus, replacing the VM helpers once parity is achieved.

### Phase 3: Math Rewrite
- Replace C math intrinsics with Orus implementations (Newton–Raphson `sqrt`, minimax trig approximations, exponentiation by squaring for `pow`).
- Offer compile-time switches to optionally call native libm for platforms that require higher precision.
- Add exhaustive accuracy tests comparing Orus results to high-precision references.

### Phase 4: Filesystem & Environment APIs
- Build `std/fs` using `fs_open`, `fs_read`, `fs_write`, `fs_seek`, plus high-level helpers like `read_file` and `write_file`.
- Implement directory iteration and metadata queries once kernel primitives support them.
- Add `std/env` for environment variables and process exit helpers.

### Phase 5: Extended Utilities
- Deliver `std/json`, `std/regex`, and higher-level collections (maps, sets, priority queues) as pure Orus modules.
- Create streaming IO abstractions that compose with the prelude formatting layer.
- Expand documentation and samples demonstrating real applications powered solely by Orus stdlib code.

### Phase 6: Finalize the Kernel Interface
- Confirm no temporary compatibility hooks remain, update the VM/kernel boundary, and clean up build scripts.
- Tag the release as the "self-hosted stdlib" milestone once all modules run exclusively on kernel primitives and Orus code.

## 7. Testing & Tooling Enhancements
- Golden-file suites for formatter output, filesystem operations, RNG reproducibility, and math accuracy.
- Property tests for collection algorithms (e.g., push/pop invariants, iterator stability).
- CI matrix running on at least x86-64 and ARM64, with nightly stress tests for IO and FS primitives.
- Benchmark harness comparing Orus implementations to their kernel-backed fallbacks to catch performance regressions early.

## 8. Risk Mitigation
- **Performance regressions:** Mitigate with benchmarking gates and optional native fallback build flags during the transition.
- **Bootstrap loops:** Ensure kernel primitives are callable before the prelude loads; keep minimal diagnostic hooks in C until Orus equivalents stabilize.
- **Developer ergonomics:** Provide migration guides and lint rules that flag direct kernel primitive usage from user modules once the public APIs stabilize.

## 9. Timeline Targets
| Quarter | Milestone |
| --- | --- |
| Q1 | Kernel primitive table, prelude bootstrap, IO/Time/Random modules behind feature flags |
| Q2 | Collections and bytes rewrite, math replacement reaching parity, filesystem alpha release |
| Q3 | Extended utilities, kernel interface hardening, CI on multiple architectures |
| Q4 | Release fully self-hosted stdlib, document post-migration maintenance |

## 10. Long-Term Outlook
- Begin authoring compiler support tooling (formatters, linters) in Orus using the new stdlib foundations.
- Explore alternative runtime backends (e.g., WASM) leveraging the same kernel primitive contract.
- Treat the self-hosted stdlib as a stepping stone toward self-hosting the compiler and runtime maintenance tools.
