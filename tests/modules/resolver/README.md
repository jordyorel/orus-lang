# Module resolver tests

This directory exercises the module resolution pipeline.

The current resolver scenarios focus on search-path behavior while the standard
library is being rebuilt. Additional fixtures will return once new `std/`
modules exist.

* `oruspath_override.orus` demonstrates overriding the search roots with the
  `ORUSPATH` environment variable:

```sh
ORUSPATH=tests/modules/resolver/lib ./orus tests/modules/resolver/oruspath_override.orus
```
