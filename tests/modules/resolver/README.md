# Module resolver tests

This directory exercises the module resolution pipeline.

* `default_std_import.orus` verifies that bare module names resolve to the
  bundled `std/` directory next to the executable. It prints boolean checks for
  `math.add`, `math.scale`, and `math.PI`.
* `selective_std_import.orus` ensures selective imports pick individual symbols
  from the same standard module.
* `oruspath_override.orus` demonstrates overriding the search roots with the
  `ORUSPATH` environment variable:

```sh
ORUSPATH=tests/modules/resolver/lib ./orus tests/modules/resolver/oruspath_override.orus
```
