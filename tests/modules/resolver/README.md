# Module resolver tests

This directory exercises the module resolution pipeline.

* `oruspath_override.orus` demonstrates overriding the search roots with the
  `ORUSPATH` environment variable to locate custom modules:

```sh
ORUSPATH=tests/modules/resolver/lib ./orus tests/modules/resolver/oruspath_override.orus
```
