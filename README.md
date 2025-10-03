# Orus Programming Language

[![Version](https://img.shields.io/badge/version-0.6.2-blue.svg)](CHANGELOG.md)

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


### Release Archives

### Installation

Example install (macOS arm64 shown; adjust the archive name as needed):

```bash
# Install to ~/.local/bin (no sudo required)
mkdir -p "$HOME/.local/bin"
curl -fsSL https://github.com/jordyorel/orus-lang/releases/latest/download/orus-macos-arm64.tar.gz \
  | tar -xz -O orus > "$HOME/.local/bin/orus"
chmod 755 "$HOME/.local/bin/orus"

# Optional: install system-wide (requires sudo)
curl -fsSL https://github.com/jordyorel/orus-lang/releases/latest/download/orus-macos-arm64.tar.gz \
  | tar -xz -O orus | sudo tee /usr/local/bin/orus >/dev/null \
  && sudo chmod 755 /usr/local/bin/orus
```

Add `~/.local/bin` to your `PATH` if it is not already present. To keep accompanying files (for example the `LICENSE`), extract into a temporary directory with `tar -xzf` before installing.


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
