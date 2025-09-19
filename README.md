# Orus Programming Language

[![Version](https://img.shields.io/badge/version-0.4.0-blue.svg)](CHANGELOG.md)

Fast, elegant programming language with Python's readability and Rust's safety.

## Overview

Orus is a modern register-based VM language that combines performance with safety:
- **High Performance**: 1.7× faster than Python, 2.3× faster than JavaScript, competitive with LuaJIT
- **Type Safety**: Static typing with intelligent type inference and runtime safety checks
- **Developer Experience**: Clean, readable syntax with comprehensive error reporting
- **Cross-Platform**: Optimized builds for Apple Silicon, Intel, and ARM architectures

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: n
    else: fibonacci(n-1) + fibonacci(n-2)

print("Result: ", fibonacci(10))
```

## Performance (Jul 2025)

*Benchmarks run on Apple M1 Pro with optimized builds (5-run average with statistical analysis)*

### Comprehensive Benchmark Results

| Language | **Mean Time** | **Range** | **Relative Performance** |
|----------|---------------|-----------|--------------------------|
| **LuaJIT** | **21.0ms** | 20.8-21.2ms | 1.0× (baseline) |
| **Orus** | **21.0ms** | 20.5-21.5ms | 1.0× (tied for fastest) |
| **Lua** | **21.1ms** | 20.8-21.2ms | 1.00× slower |
| **Python** | **35.3ms** | 34.8-35.7ms | 1.68× slower |
| **JavaScript** | **46.1ms** | 45.7-46.8ms | 2.20× slower |
| **Java** | **72.1ms** | 67.0-75.5ms | 3.43× slower |

### Pure Arithmetic Performance

| Language | **Mean Time** | **Operations** | **Relative Performance** |
|----------|---------------|----------------|--------------------------|
| **Orus** | **5.2ms** | 500+ arithmetic ops | **13.5× faster than Java** |
| **Java (HotSpot)** | **70.4ms** | 500+ arithmetic ops | 1.0× (JVM baseline) |

**Performance Summary**:
- **vs Python**: 1.7× faster (comprehensive) 
- **vs JavaScript**: 2.2× faster (comprehensive)
- **vs Java**: 3.4× faster (comprehensive), 13.5× faster (pure arithmetic)
- **vs LuaJIT**: Tied for fastest
- **vs Lua**: Tied for fastest

## Quick Start

```bash
git clone https://github.com/jordyorel/orus-lang.git
cd orus-lang
make release            # Build optimized release version
./orus                  # REPL
./orus program.orus     # Run file
make benchmark          # Run benchmarks
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

**Build Features:**
- **Release**: Maximum optimization (-O3, LTO, fast-math) for production use
- **Debug**: Full debugging symbols (-O0, -g3) with assertions for development
- **Profiling**: Optimized with gprof instrumentation for performance analysis
- **Cross-compilation**: Linux and Windows targets available
- **Static analysis**: Built-in cppcheck and clang analyzer integration

### Installation

```bash
# System-wide installation (requires sudo)
make install

# Manual installation
make release
sudo cp orus /usr/local/bin/orus
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

### Guidelines
- Follow existing code style
- Add tests for new features
- Run benchmarks before submitting
- Update documentation as needed

### Submitting Changes
1. Fork the repository
2. Create feature branch
3. Make changes with tests
4. Verify with: `make test && make benchmark && make analyze`
5. Test all profiles: `make clean && make debug && make release && make profiling`
6. Submit pull request

**Pre-submission Checklist:**
- ✅ All tests pass (`make test`)
- ✅ Benchmarks show no regression (`make benchmark`) 
- ✅ Static analysis passes (`make analyze`)
- ✅ All build profiles compile successfully
- ✅ Documentation updated for new features

See [BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md) for detailed performance data.

<!-- ## Public API Documentation -->

<!-- For details on embedding the Orus VM in other applications, see
[VM_PUBLIC_API.md](docs/VM_PUBLIC_API.md). -->

## License

Orus is licensed under the [MIT License](LICENSE).
