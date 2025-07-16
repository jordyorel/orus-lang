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

| Benchmark | **Orus** | Python | Node.js | Lua | LuaJIT |
|-----------|----------|--------|---------|-----|--------|
| Arithmetic | **20.7ms** | 35.0ms | 48.7ms | 20.8ms | 20.0ms |
| Comprehensive | **20.2ms** | 35.3ms | 45.4ms | 20.6ms | 20.4ms |
| Extreme Stress | **20.6ms** | 35.4ms | 46.0ms | 20.9ms | 20.7ms |

**Overall Average**: Orus **20.5ms** | Python 35.2ms | JavaScript 46.7ms | Lua 20.8ms | LuaJIT 20.4ms

- **Performance vs Python**: 1.7× faster  
- **Performance vs JavaScript**: 2.3× faster
- **Performance vs Lua**: Competitive (within 1% difference)
- **Performance vs LuaJIT**: Competitive (within 1% difference)

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

### Code Structure
- `src/vm/` - Virtual machine implementation
- `src/compiler/` - Lexer, parser, compiler
- `src/type/` - Type system and inference
- `tests/` - Test files and benchmarks
- `docs/` - Documentation and specs

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

For details on embedding the Orus VM in other applications, see
[VM_PUBLIC_API.md](docs/VM_PUBLIC_API.md).

## License

Orus is licensed under the [MIT License](LICENSE).
