# Orus Versioning

Orus uses **Semantic Versioning 2.0.0** to signal compatibility guarantees.
Versions follow the `MAJOR.MINOR.PATCH` format with optional pre-release and
build metadata.

- **MAJOR** version increments when incompatible language or API changes are
  introduced.
- **MINOR** version increments when new, backwards compatible functionality is
  added.
- **PATCH** version increments for backwards compatible bug fixes.

Pre-release identifiers (e.g. `1.0.0-alpha`) mark unstable releases and have
lower precedence than the associated stable version. Build metadata may be
appended with a plus sign (e.g. `1.0.0+20130313144700`) and is ignored when
comparing versions.

The current Orus version is **0.1.0**, indicating active initial development.
No stability guarantees are made while the major version is zero.
