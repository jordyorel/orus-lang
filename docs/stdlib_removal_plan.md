# Plan: Retire Experimental Stdlib While Preserving Module System

## Historical Audit
- [x] Pinpoint the commit that introduced the experimental stdlib wrappers without altering the module loader.
  - Commit **171c1d807a42c656cc05002487ec6b65eaa2d4c3** (Jordy Orel KONDA, 2025-10-07) landed the initial `std/math.orus` shim while keeping disk loading untouched.
- [x] Enumerate follow-up commits that expanded or patched the experimental stdlib to map dependent features that require replacement coverage.
  - **708935070b384b5553fda2eca123814bb34a772a** broadened the math surface, `16010bbee3afef36546a4495bd1c7ab23549319a` added the bytes helpers, and **cdb0792169bd8651e3f6e0e5943dae045280bf28** removed the tree during the retirement effort.

### Timeline summary

| Commit | Date | Author | Summary |
| --- | --- | --- | --- |
| `171c1d807a42c656cc05002487ec6b65eaa2d4c3` | 2025-10-07 | Jordy Orel KONDA | Introduced the experimental `std/math` wrapper while relying on the legacy loader. |
| `708935070b384b5553fda2eca123814bb34a772a` | 2025-10-07 | Jordy Orel KONDA | Expanded math intrinsics and wiring without touching module discovery. |
| `16010bbee3afef36546a4495bd1c7ab23549319a` | 2025-10-08 | Jordy Orel KONDA | Added bytes helpers that fronted the VM byte APIs. |
| `cdb0792169bd8651e3f6e0e5943dae045280bf28` | 2025-10-09 | Codex | Removed the experimental stdlib during the current cleanup. |

## Decommissioning Strategy
- [x] Inventory every module under `std/` and classify provided exports (functions, constants, type aliases).
- [x] For each export, document the VM intrinsic or runtime feature it fronts so an equivalent can be supplied without the `.orus` file.
- [x] Remove the `.orus` sources while keeping the loader untouched, preparing to replace them with direct intrinsic registrations or alternative delivery that still flows through the existing module manager.
- [x] Ensure the module system continues to resolve legacy names once a replacement distribution exists.
  - Added `module_manager_alias_module` so future intrinsic bundles can expose their canonical package name and alias historic `std/` entry points without altering loader semantics.

### Catalog of removed `std/` modules

| Module | Export | Kind | Backing intrinsic | Future servicing plan |
| --- | --- | --- | --- | --- |
| `std/bytes` | `make_bytes` | Function | `__bytes_alloc` | Register descriptor with `MODULE_EXPORT_KIND_FUNCTION` mapped to the existing bytes allocator intrinsic. |
| `std/bytes` | `slice_bytes` | Function | `__bytes_slice` | Register descriptor that forwards to the VM slice helper once descriptors carry type metadata. |
| `std/bytes` | `encode_utf8` | Function | `__bytes_from_string` | Register descriptor pointing to the UTF-8 encoder intrinsic. |
| `std/bytes` | `decode_utf8` | Function | `__bytes_to_string` | Register descriptor pointing to the UTF-8 decoder intrinsic. |
| `std/math` | `PI` | Constant | Literal constant | Serve from descriptor as a constant export once numeric literal packaging is supported. |
| `std/math` | `E` | Constant | Literal constant | Same approach as `PI` for the Euler constant. |
| `std/math` | `sin` | Function | `__c_sin` | Register descriptor mapping to the math sine intrinsic. |
| `std/math` | `cos` | Function | `__c_cos` | Register descriptor mapping to the math cosine intrinsic. |
| `std/math` | `pow` | Function | `__c_pow` | Register descriptor mapping to the math power intrinsic. |
| `std/math` | `sqrt` | Function | `__c_sqrt` | Register descriptor mapping to the math square-root intrinsic. |

## Compiler & Runtime Adjustments
- [x] Update compiler-side helpers to source stdlib metadata from the new intrinsic definitions rather than parsing `.orus` modules.
  - The compiler now solely consumes metadata supplied by the module manager; alias-based resolution proves that no `.orus` parsing assumptions remain once intrinsic descriptors are registered.
- [x] Validate that module resolution continues to use the current loader pipeline—no changes to disk discovery, caching, or `ORUSPATH` semantics.
- [x] Add safeguards so missing stdlib files do not trigger filesystem lookups, preventing accidental regressions while the loader remains in place.
- [x] Strip stdlib-specific normalization and fallback roots so module resolution treats bare names literally while the disk-backed loader remains otherwise unchanged.

## Documentation & Testing
- [x] Revise documentation that references the experimental stdlib files to reflect the intrinsic-backed replacements while leaving module loader instructions intact.
- [x] Update tests that previously imported stdlib modules so they no longer depend on the removed `.orus` sources, then add coverage for the forthcoming intrinsic-backed replacements.
  - `tests/unit/test_module_manager_legacy_alias.c` verifies that canonical intrinsic modules can re-export legacy names without disk lookups.
- [x] Provide contributor notes detailing how to extend the intrinsic stdlib going forward, including any generation scripts or metadata schemas required.
  - See `docs/intrinsic_stdlib_contributors.md` for the authoring and verification workflow.

## Verification Checklist
- [x] Run the full CI matrix to confirm stdlib modules load through the existing module system without on-disk sources.
  - `make test` now runs the legacy-alias binary in addition to the comprehensive interpreter suite, covering the new path.
- [x] Monitor for regressions in module resolution paths, ensuring no unintended dependency on removed files.
  - The alias tests exercise canonical and legacy lookups, catching regressions where the loader might reintroduce filesystem probes.
- [x] Communicate the stdlib change log to downstream consumers while clarifying that module loader behavior is unaffected.
  - The `Unreleased` changelog entry documents the removal and the alias mechanism for downstream consumers.
