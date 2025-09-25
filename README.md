# Orus Programming Language

[![Version](https://img.shields.io/badge/version-0.6.0-blue.svg)](CHANGELOG.md)

Fast, elegant programming language with Python's readability and Rust's safety.

## Overview

Orus is a modern register-based VM language that combines performance with safety:
- **High Performance**: ~4× faster than CPython on our benchmark suite today and narrowing the gap with V8/HotSpot
- **Type Safety**: Static typing with intelligent type inference and runtime safety checks
- **Developer Experience**: Clean, readable syntax with comprehensive error reporting
- **Cross-Platform**: Optimized builds for Apple Silicon, Intel, and ARM architectures

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: n
    else: fibonacci(n-1) + fibonacci(n-2)

print("Result: ", fibonacci(10))
```

## Performance (Sep 2025)

Benchmarks were captured on a Linux x86_64 container (Intel Xeon Platinum 8370C, 5 vCPUs) using `EMCC=true make benchmark`. Each suite performs two warmup executions followed by five timed runs per language; we repeated the full benchmark three times and report the average ± standard deviation below.【ddf43a†L1-L9】【08cb02†L1-L26】【de3462†L1-L20】【7f8e93†L1-L23】【7ac006†L16-L22】【1f8a48†L12-L16】

### Cross-language benchmark averages (3 suites × 5 iterations)

| Language | **Arithmetic (ms)** | **Control Flow (ms)** | **Overall (ms)** | Notes |
|----------|--------------------|-----------------------|------------------|-------|
| **Orus** | **24.0 ± 4.0** | **297.3 ± 7.6** | **160.7 ± 4.0** | Release build with computed-goto dispatch |
| **JavaScript (Node 20.19)** | 108.7 ± 9.9 | 119.3 ± 3.1 | 114.0 ± 3.5 | V8 JIT baseline |
| **Java (OpenJDK 21.0.2)** | 136.0 ± 10.4 | — | 136.0 ± 10.4 | Arithmetic suite only |
| **Python (3.12.10)** | 552.7 ± 42.0 | 720.7 ± 25.7 | 636.7 ± 13.3 | CPython reference interpreter |
| **Lua / LuaJIT** | — | — | — | Interpreters unavailable in CI image |

Runtime versions: Node v20.19.4, Python 3.12.10, OpenJDK 21.0.2.【64b176†L1-L2】【2040da†L1-L2】【ba0f79†L1-L4】

**Performance Takeaways**
- **vs Python**: Orus is ~4× faster overall and ~23× faster on pure arithmetic workloads.【7ac006†L16-L22】【1f8a48†L12-L16】
- **vs JavaScript (V8)**: Orus is still ~1.4× slower overall, but leads on arithmetic-heavy code where the register VM shines.【7ac006†L16-L18】【1f8a48†L12-L13】
- **vs Java (HotSpot)**: The JVM retains a small overall lead today, while Orus delivers ~5.7× faster arithmetic throughput.【7ac006†L17-L20】【1f8a48†L14-L14】
- **Control Flow**: Loop-heavy code is our current bottleneck—Orus trails Node.js by ~2.5× but still halves CPython’s runtime. Optimization work in this area is ongoing.【7ac006†L20-L22】
- **Lua family**: Lua and LuaJIT measurements are omitted because the interpreters are not available in the benchmark container.

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
