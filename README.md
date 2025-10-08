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


## Installation

### Single-command installer

```bash
curl -fsSL https://raw.githubusercontent.com/jordyorel/orus-lang/main/scripts/install.sh | sh
```

The installer detects your operating system and CPU architecture, downloads the
matching release archive, and stages it so the `orus` binary and bundled
`std/` directory stay together. When run without elevated privileges it defaults
to `~/.local/opt/orus` for the installation root and keeps a symlink in
`~/.local/bin`. If executed with `sudo`, it switches to `/usr/local/opt/orus`
and updates `/usr/local/bin`.

Need a custom layout? Pass flags through `sh -s --`:

```bash
# Install into a custom prefix and publish the executable elsewhere
curl -fsSL https://raw.githubusercontent.com/jordyorel/orus-lang/main/scripts/install.sh \
  | sh -s -- --prefix "$HOME/dev/orus" --bin-dir "$HOME/bin"
```

The script always places the `std/` tree beside the interpreter. If you later
relocate files manually, make sure to move both or set `ORUSPATH` accordingly.


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
