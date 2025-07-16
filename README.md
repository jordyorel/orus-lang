# Orus Programming Language

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

| Language | **Mean Time** | **Range** | **Relative Performance** |
|----------|---------------|-----------|--------------------------|
| **LuaJIT** | **20.6ms** | 20.5-20.9ms | 1.0× (baseline) |
| **Lua** | **20.8ms** | 20.7-20.9ms | 1.01× slower |
| **Orus** | **21.5ms** | 20.5-24.8ms | 1.04× slower |
| **Python** | **35.1ms** | 34.7-35.7ms | 1.70× slower |
| **JavaScript** | **45.6ms** | 45.0-45.9ms | 2.21× slower |

**Performance Summary**:
- **vs Python**: 1.6× faster  
- **vs JavaScript**: 2.1× faster
- **vs LuaJIT**: Competitive (within 4% difference)
- **vs Lua**: Competitive (within 3% difference)

## Quick Start

```bash
git clone https://github.com/jordyorel/orus-lang.git
cd orus-lang
make clean && make
./orus                    # REPL
./orus program.orus       # Run file
make benchmark           # Run benchmarks
```

## Contributing

### Development Setup
```bash
make clean && make       # Build with optimizations
make test               # Run test suite
make benchmark          # Performance tests
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
4. Run `make test && make benchmark`
5. Submit pull request

See [BENCHMARK_RESULTS.md](docs/BENCHMARK_RESULTS.md) for detailed performance data.

## Public API Documentation

<!-- For details on embedding the Orus VM in other applications, see
[VM_PUBLIC_API.md](docs/VM_PUBLIC_API.md). -->

## License

Orus is licensed under the [MIT License](LICENSE).
