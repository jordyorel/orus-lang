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

<!-- ## Public API Documentation -->

<!-- For details on embedding the Orus VM in other applications, see
[VM_PUBLIC_API.md](docs/VM_PUBLIC_API.md). -->

## License

Orus is licensed under the [MIT License](LICENSE).
