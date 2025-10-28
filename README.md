[![Version](https://img.shields.io/badge/version-0.1.1-blue.svg)](CHANGELOG.md)

# Orus Programming Language


Orus is a register-based VM language that aims to combine performance, readability and safety:
- **Type Safety**: Static typing with type inference and runtime safety checks
- **Developer Experience**: Clean, readable syntax with comprehensive error reporting (Still improving)


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
zig build -Dprofile=release   # Build optimized release binary
./orus                        # REPL
./orus program.orus           # Run file
```

### Build Profiles

Orus supports multiple build configurations:

```bash
# Production (optimized, creates 'orus')
zig build -Dprofile=release

# Development (debugging, creates 'orus_debug')
zig build

# Performance analysis (creates 'orus_profiling')
zig build -Dprofile=profiling

# Clean all builds
zig build clean

# List custom build options
zig build --help
```

## Installation

### Using `zig build install`

`zig build install -Dprofile=release` builds the optimized binary and stages it
for distribution.

```bash
# macOS and Linux (run with the privileges required for the destination)
sudo zig build install -Dprofile=release

# Windows (from a shell with the necessary privileges)
zig build install -Dprofile=release
```

The default installation roots are:

- **macOS:** `/Library/Orus`
- **Linux:** `/usr/local/lib/orus`
- **Windows:** `C:/Program Files/Orus`

The `orus` binary is copied into the `bin/` subdirectory. Override
an alternate prefix with `--prefix`:

```bash
zig build install -Dprofile=release --prefix "$HOME/.local/orus"
```


## Contributing
Every contribution is more than welcome

### Development Setup
```bash
# Build for development (with debugging)
zig build

# Run comprehensive test suite
zig build test

# Run interpreter benchmarks  
zig build benchmarks -Dprofile=release

# Run JIT performance benchmarks (use -Dstrict-jit=true to enforce thresholds)  
zig build jit-benchmark -Dprofile=release [-Dstrict-jit=true]

# Build all major profiles
zig build clean && zig build && zig build -Dprofile=release && zig build -Dprofile=profiling
```

## License

Orus is licensed under the [MIT License](LICENSE).
