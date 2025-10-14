# Orus Programming Language

[![Version](https://img.shields.io/badge/version-0.6.4-blue.svg)](CHANGELOG.md)

Fast, elegant programming language with Python's readability and Rust's safety.

## Overview

Orus is a modern register-based VM language that combines performance with safety:
- **Type Safety**: Static typing with intelligent type inference and runtime safety checks
- **Developer Experience**: Clean, readable syntax with comprehensive error reporting (Still improving)
- **Cross-Platform**: Optimized builds for Apple Silicon, Intel, and ARM architectures

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: n
    else: fibonacci(n-1) + fibonacci(n-2)

print("Result: ", fibonacci(10))
```

## Quick Start

```bash
git clone https://github.com/jordyorel/orus-lang.git
cd orus-lang
make release            # Build optimized release version
./orus                  # REPL
./orus program.orus     # Run file
```

### Build Profiles

Orus supports multiple build configurations:

```bash
# Production (optimized, creates 'orus')
make release

# Development (debugging, creates 'orus_debug')
make debug

# Performance analysis (creates 'orus_profiling')
make profiling

# Clean all builds
make clean

# View all available build options
make help
```

### Enable the JIT tier on Apple Silicon

macOS requires binaries that generate executable code at runtime to carry the
`com.apple.security.cs.allow-jit` entitlement. On Apple Silicon machines the
`make` targets automatically invoke the signing helper whenever `codesign` is
available, so freshly built binaries are ready to execute JIT code without any
manual step. The helper is still available if you need to re-sign specific
artifacts or run the process explicitly:

```bash
make
./scripts/macos/sign-with-jit.sh        # optional manual invocation
```

Pass explicit paths to the script if you only want to sign a subset of
artifacts (for example `./scripts/macos/sign-with-jit.sh build/release/tests/test_vm_hot_loop_profiling`).

Unsigned binaries (including release builds) still run correctly, but the VM
detects that macOS denied the JIT buffers and automatically falls back to the
interpreter tier. Signing with the entitlement is required any time you want to
exercise native JIT code on Apple Silicon.

### JIT coverage status

The tiered compiler is still evolving and does not yet replace the interpreter
for every Orus feature. The current roadmap keeps the JIT focused on the hot
numeric loops and mixed boxed workloads that already translate, while the exit
criteria tracking 3–5× speedups and fully GC-safe execution remain unchecked in
`docs/ROADMAP_PERFORMANCE.md`. The benchmark corpus now keeps the
FFI ping/pong harness in translator coverage—the tiered compiler lowers
`OP_CALL_FOREIGN`, and the regression suite mirrors the benchmark loop to keep
the path exercised. Runtime uplift is still tracked in
`docs/ROADMAP_PERFORMANCE.md`, but foreign-call heavy programs can now translate
instead of bailing out on an unsupported opcode.


## Installation

### Using `make install`

`make install` builds the optimized release binary and stages the
interpreter for distribution.

```bash
# macOS and Linux (run with the privileges required for the destination)
sudo make install

# Windows (from a MinGW/MSYS shell with administrative privileges)
make install
```

The default installation roots are:

- **macOS:** `/Library/Orus`
- **Linux:** `/usr/local/lib/orus`
- **Windows:** `C:/Program Files/Orus`

The `orus` binary is copied into the `bin/` subdirectory. Override
`INSTALL_PREFIX` to install elsewhere:

```bash
INSTALL_PREFIX="$HOME/.local/orus" make install
```


## Contributing

### Development Setup
```bash
# Build for development (with debugging)
make debug

# Run comprehensive test suite
make test

# Run performance benchmarks  
make benchmark

# Run static analysis
make analyze

# Build all profiles
make clean && make debug && make release && make profiling
```

## License

Orus is licensed under the [MIT License](LICENSE).
