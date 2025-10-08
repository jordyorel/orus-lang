# Module resolver tests

This directory exercises the module resolution pipeline.

* `default_std_import.orus` verifies that bare module names resolve to the
  bundled `std/` directory next to the executable. It prints boolean checks for
  the floating-point math wrappers (`math.sin`, `math.cos`, `math.pow`,
  `math.sqrt`) and constants (`math.PI`, `math.E`).
* `selective_std_import.orus` ensures selective imports pick individual symbols
  from the same standard module.
* `oruspath_override.orus` demonstrates overriding the search roots with the
  `ORUSPATH` environment variable:

```sh
ORUSPATH=tests/modules/resolver/lib ./orus tests/modules/resolver/oruspath_override.orus
```
* `macos_std_fallback_probe.orus` is used by macOS-specific regression tests to
  ensure the resolver reports `/Library/Orus` fallbacks when the bundled `std`
  symlink is absent next to the executable.
