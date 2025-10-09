# Intrinsic Stdlib Contributor Guide

With the experimental `std/` sources removed, new standard-library functionality must be delivered as intrinsic modules that are registered directly with the VM. This guide captures the expectations for authorsing those replacements while keeping the disk-based loader untouched.

## Authoring workflow

1. **Define the intrinsic** in the VM runtime (`src/vm/runtime/` or the appropriate subsystem) and expose it through the native function table.
2. **Populate module exports** by creating or reusing a canonical module (for example `intrinsics/math`) and calling `register_module_export` with the exported name, register index, and optional intrinsic symbol.
3. **Audit canonical names** to ensure exported identifiers only use the new `intrinsics/` hierarchy. The module registry no longer supports registering legacy `std/` aliases.
4. **Update tests** by covering the new exports through the existing interpreter suites or dedicated unit tests when needed.
5. **Document the change** in `docs/STDLIB_ROADMAP.md` and `CHANGELOG.md`, explaining which intrinsics back the new exports and how to regenerate any bytecode bundles if packaging is required.

## Metadata expectations

- *No `.orus` parsing:* Compiler components must continue to rely on the module manager for export metadata. Avoid reintroducing filesystem dependencies when expanding the intrinsic library.
- *Type information:* When type metadata is necessary, allocate `Type` instances with `module_clone_export_type` helpers so downstream compilation phases receive consistent annotations.
- *Canonical-only registration:* The module registry now rejects duplicate or legacy names, so ensure every module registers its canonical `intrinsics/` identifier exactly once.

## Verification checklist

- Run `make test` to execute the comprehensive interpreter suite.
- Add focused unit tests for any new intrinsics where interpreter coverage is insufficient.
- Update documentation to reflect the new surface area and notify downstream consumers through the change log.

Following this flow keeps the module loader unchanged while allowing the project to roll out intrinsic-backed stdlib functionality incrementally.
