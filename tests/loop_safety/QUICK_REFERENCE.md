# Loop Safety Test Suite - Quick Reference

## 🚀 Quick Start

```bash
# Run all loop safety tests
cd tests/loop_safety
./run_loop_safety_tests.sh

# Run individual test
../../orus test_edge_thresholds.orus

# Test with custom environment
ORUS_MAX_LOOP_ITERATIONS=0 ../../orus test_custom_limit.orus
```

## 📋 Test Files Summary

| Test File | Purpose | Expected Result |
|-----------|---------|-----------------|
| `test_edge_thresholds.orus` | Boundary conditions (99K, 100K, 1M) | ⚠️ Warning at 1M |
| `test_10m_boundary.orus` | 10M iteration boundary | ⚠️ Warning, completes |
| `test_10m_plus_one.orus` | 10M+1 iteration test | ❌ Error at 10M+1 |
| `test_nested_loops.orus` | Basic nested scenarios | ✅ Silent completion |
| `test_nested_guards.orus` | Nested with guards | ⚠️ Multiple warnings |
| `test_loop_consistency.orus` | For vs while loops | ❌ Error (15M test) |
| `test_static_dynamic.orus` | Analysis types | ✅ Silent completion |
| `test_static_1_5m.orus` | Static 1.5M loop | ⚠️ Warning at 1M |
| `test_env_vars.orus` | Environment variables | ⚠️ Warning at 1M |
| `test_custom_limit.orus` | Custom 2M limit | ❌ Error at 2M |
| `test_very_large.orus` | 50M iterations | ❌ Error at 10M |
| `test_custom_very_large.orus` | 25M with 50M limit | ⚠️ Warning, completes |
| `test_register_stress.orus` | Register allocation | ✅ Silent completion |
| `test_comprehensive.orus` | End-to-end test | ❌ Error at 10M |

## 🔧 Environment Variables

```bash
# Default behavior (100K guards, 1M warning, 10M limit)
./orus test.orus

# Unlimited loops
ORUS_MAX_LOOP_ITERATIONS=0 ./orus test.orus

# Custom guard threshold (enable guards at 50K)
ORUS_LOOP_GUARD_THRESHOLD=50000 ./orus test.orus

# Custom safety limit (stop at 5M)
ORUS_MAX_LOOP_ITERATIONS=5000000 ./orus test.orus

# Combined custom settings
ORUS_LOOP_GUARD_THRESHOLD=200000 ORUS_MAX_LOOP_ITERATIONS=20000000 ./orus test.orus
```

## 📊 Expected Test Results

All 16 tests should pass with these behaviors:

### ✅ Silent Completion (4 tests)
- Basic nested loops 
- Static vs dynamic analysis
- Register stress test
- Tests under warning threshold

### ⚠️ Warning + Completion (8 tests)
- Edge thresholds (1M+ iterations)
- 10M boundary test
- Nested with guards
- Static 1.5M test
- Environment variable tests
- Custom very large test

### ❌ Runtime Error (4 tests)
- 10M+1 iterations
- For vs while consistency (15M test)
- Custom 2M limit (3M test)
- Very large iterations (50M test)
- Comprehensive test (15M test)

## 🐛 Troubleshooting

| Issue | Solution |
|-------|----------|
| Build errors | Run `make clean && make` |
| Test failures | Check expected vs actual behavior |
| Environment issues | Verify env vars are set correctly |
| Register errors | Check for register allocation conflicts |

## 📈 Performance Expectations

- **< 100K iterations**: Instant execution
- **100K - 1M iterations**: Minimal overhead
- **1M+ iterations**: Warning + continued execution
- **10M+ iterations**: Safety stop (configurable)