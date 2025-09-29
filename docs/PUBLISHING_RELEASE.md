# Publishing an Orus Release

These steps ensure the GitHub release contains the assets required by the curl installer.

1. Choose a version tag (e.g. `v0.6.1`) and update version references in the repository if needed.
2. Build the release binary for the current platform:
   ```bash
   make clean release
   ```
3. Produce the distributable archive:
   ```bash
   make dist
   ```
   This creates `dist/orus-<os>-<arch>.tar.gz` containing the optimized `orus` binary and `LICENSE`.
4. Repeat the build and `make dist` steps on each supported platform (macOS arm64/x86_64, Linux x86_64/arm64). Cross-compilation targets `cross-linux` and `cross-windows` are available if you prefer to build from a single host.
5. Copy the installer script into the release staging area:
   ```bash
   cp scripts/install-orus.sh dist/
   ```
6. Generate checksums for the artifacts (optional but recommended):
   ```bash
   shasum -a 256 dist/*
   ```
7. Draft the GitHub release:
   - Tag: `vX.Y.Z`
   - Title: `Orus vX.Y.Z`
   - Upload each `dist/orus-<os>-<arch>.tar.gz` archive.
   - Upload `dist/install-orus.sh` (rename to just `install-orus.sh` if desired) so the curl installer URL resolves.
   - Paste the checksums into the release notes if you generated them.
8. Publish the release and verify the installer is working:
   ```bash
   curl -fsSL https://github.com/jordyorel/orus-lang/releases/latest/download/install-orus.sh | bash -- --dry-run
   ```

After publishing, update downstream documentation or announcements as needed.
