# Orus Programming Language

Fast, elegant programming language with Python's readability and Rust's safety.

## Overview

Orus is a register-based VM language with:
- **Performance**: Outperforms Python by 5×, JavaScript by 11×
- **Safety**: Static typing with type inference
- **Readability**: Clean indentation-based syntax

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: n
    else: fibonacci(n-1) + fibonacci(n-2)

print("Result: ", fibonacci(10))
```

## Performance (Jul 2025)

| Benchmark | **Orus** | Python | Node.js | Lua |
|-----------|----------|--------|---------|-----|
| Arithmetic | **19.2ms** | 68.6ms | 49.0ms | 28.8ms |
| Control Flow | **19.0ms** | 101.1ms | 51.9ms | 36.0ms |
| Function Calls | **20.1ms** | 35.0ms | 44.7ms | 19.8ms |

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
