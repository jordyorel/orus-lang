# Intrinsic Stdlib Contributor Guide

With the experimental `std/` sources removed, new standard-library functionality must be delivered as intrinsic modules that are registered directly with the VM. This guide captures the expectations for authorsing those replacements while keeping the disk-based loader untouched.

## Authoring workflow

1. **Define the intrinsic** in the VM runtime (`src/vm/runtime/` or the appropriate subsystem) and expose it through the native function table.
2. **Populate module exports** by creating or reusing a canonical module (for example `intrinsics/math`) and calling `register_module_export` with the exported name, register index, and optional intrinsic symbol.
3. **Alias historic names** with `module_manager_alias_module(manager, "intrinsics/<module>", "std/<module>")` so existing user code continues to reference legacy `std/` imports without changing the loader.
4. **Update tests** by extending `tests/unit/test_module_manager_legacy_alias.c` (or adding a sibling test) to cover the new exports and aliases, ensuring the build pipeline exercises the canonical and legacy names.
5. **Document the change** in `docs/STDLIB_ROADMAP.md` and `CHANGELOG.md`, explaining which intrinsics back the new exports and how to regenerate any bytecode bundles if packaging is required.

## Metadata expectations

- *No `.orus` parsing:* Compiler components must continue to rely on the module manager for export metadata. Avoid reintroducing filesystem dependencies when expanding the intrinsic library.
- *Type information:* When type metadata is necessary, allocate `Type` instances with `module_clone_export_type` helpers so downstream compilation phases receive consistent annotations.
- *Deterministic registration order:* Register canonical modules before declaring aliases. This keeps `module_manager_alias_module` deterministic and ensures alias tests can assert pointer equality.

## Verification checklist

- Run `make test` to execute the comprehensive interpreter suite together with the legacy-alias regression binary.
- Add focused unit tests for any new intrinsics, mirroring the style used by `test_module_manager_legacy_alias.c`.
- Update documentation to reflect the new surface area and notify downstream consumers through the change log.

Following this flow keeps the module loader unchanged while allowing the project to roll out intrinsic-backed stdlib functionality incrementally.
