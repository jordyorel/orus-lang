# Publishing an Orus Release

Follow this checklist to prepare GitHub releases that work with the direct `curl` download instructions.

1. Ensure the working tree is clean and bump any version references that need updating. Commit the changes before tagging.
2. Build the optimized binary for the current platform:
   ```bash
   make clean release
   ```
3. Produce the distributable archive:
   ```bash
   make dist
   ```
   This creates `dist/orus-<os>-<arch>.tar.gz` containing the `orus` executable and the project `LICENSE`.
4. Repeat the build and `make dist` steps on each supported platform (macOS arm64/x86_64, Linux x86_64/arm64). Cross-compilation targets such as `make cross-linux` are available if you prefer to build everything from one machine.
5. (Optional) Generate checksums for the artifacts you plan to upload:
   ```bash
   shasum -a 256 dist/orus-*.tar.gz
   ```
6. Draft the GitHub release:
   - Tag: `vX.Y.Z`
   - Title: `Orus vX.Y.Z`
   - Upload every `dist/orus-<os>-<arch>.tar.gz` archive you built.
   - Include any checksums in the release notes if you computed them.
7. Publish the release and verify that each archive is reachable, for example:
   ```bash
   curl -I https://github.com/jordyorel/orus-lang/releases/latest/download/orus-macos-arm64.tar.gz
   ```

After the release is live, update any announcements or documentation to point to the new version.
