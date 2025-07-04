# Loop Safety Test Suite

This directory contains comprehensive tests for Orus's progressive loop safety system.

## 🔒 Loop Safety System Overview

Orus implements a progressive loop safety system with the following behavior:

| Iteration Count | Behavior | Message | User Override |
|----------------|----------|---------|---------------|
| `< 100K` | ✅ Fast, guard-free execution | None | N/A |
| `100K–1M` | ✅ Guarded silently | No message | No need |
| `1M–10M` | ⚠️ Warns at 1M | Prints warning | No error yet |
| `> 10M` | ❌ Stops by default | Runtime error | ✅ Via env var |
| `Any` w/ `ORUS_MAX_LOOP_ITERATIONS=0` | ✅ Unlimited | None | Full trust |

## 🧪 Test Categories

### 1. Threshold Edge Cases (`test_edge_thresholds.orus`, `test_10m_boundary.orus`, `test_10m_plus_one.orus`)
- Tests exact boundaries (99,999 vs 100,000 vs 100,001)
- Validates warning trigger at exactly 1,000,000 iterations
- Confirms hard stop at exactly 10,000,001 iterations

### 2. Nested Loop Tests (`test_nested_loops.orus`, `test_nested_guards.orus`)
- Verifies independent tracking of nested loops
- Tests multiple guard registers
- Validates correct warning behavior in nested scenarios

### 3. Loop Type Consistency (`test_loop_consistency.orus`, `test_static_dynamic.orus`, `test_static_1_5m.orus`)
- Ensures for-loops and while-loops behave identically
- Tests static vs dynamic loop analysis
- Validates mathematical consistency between loop types

### 4. Environment Variable Tests (`test_env_vars.orus`, `test_custom_limit.orus`)
- Tests `ORUS_MAX_LOOP_ITERATIONS` configuration
- Tests `ORUS_LOOP_GUARD_THRESHOLD` configuration  
- Validates unlimited mode (`ORUS_MAX_LOOP_ITERATIONS=0`)
- Tests custom limits and edge cases

### 5. Very Large Iteration Tests (`test_very_large.orus`, `test_custom_very_large.orus`)
- Tests behavior with 25M+ iterations
- Validates custom high limits (50M+)
- Ensures system scales correctly

### 6. Stress Tests (`test_register_stress.orus`, `test_comprehensive.orus`)
- Tests register allocation under stress
- Multiple variables and deep nesting
- Multiple guard registers simultaneously
- End-to-end comprehensive validation

## 🚀 Running the Tests

### Run All Tests
```bash
cd tests/loop_safety
./run_loop_safety_tests.sh
```

### Run Individual Tests
```bash
# Basic test
../../orus test_edge_thresholds.orus

# With environment variables
ORUS_MAX_LOOP_ITERATIONS=0 ../../orus test_custom_limit.orus

# With custom guard threshold
ORUS_LOOP_GUARD_THRESHOLD=50000 ../../orus test_env_vars.orus
```

## 📊 Expected Results

All tests should pass with the following expected behaviors:

- **Completion Tests**: Should complete without errors
- **Warning Tests**: Should display warning at 1M iterations but continue
- **Error Tests**: Should stop at configured limit with runtime error
- **No Warning Tests**: Should complete silently without warnings

## 🔧 Environment Variables

### `ORUS_LOOP_GUARD_THRESHOLD` (default: 100,000)
Controls when loop guards are enabled:
```bash
ORUS_LOOP_GUARD_THRESHOLD=50000   # Enable guards at 50K iterations
ORUS_LOOP_GUARD_THRESHOLD=200000  # Enable guards at 200K iterations
```

### `ORUS_MAX_LOOP_ITERATIONS` (default: 10,000,000)
Controls the hard stop limit:
```bash
ORUS_MAX_LOOP_ITERATIONS=0         # Unlimited (no hard stop)
ORUS_MAX_LOOP_ITERATIONS=5000000   # Stop at 5M iterations
ORUS_MAX_LOOP_ITERATIONS=50000000  # Stop at 50M iterations
```

## 🐛 Debugging Failed Tests

If tests fail, check:

1. **Build Status**: Ensure `make clean && make` completes successfully
2. **Environment**: Check if environment variables are set correctly
3. **Output**: Review test output for specific error messages
4. **Register Allocation**: Verify sufficient registers (256 available)

## 📝 Adding New Tests

To add new loop safety tests:

1. Create a new `.orus` file in this directory
2. Add the test to `run_loop_safety_tests.sh`
3. Specify expected behavior (`SHOULD_COMPLETE`, `SHOULD_ERROR`, `SHOULD_WARN`, `NO_WARNING`)
4. Include any required environment variables
5. Run the full test suite to ensure no regressions

## 🎯 Technical Implementation Details

### Guard System Architecture
- **Guard Registers**: Uses 3 consecutive registers per guarded loop
- **Register Safety**: Bounds checking prevents register overflow
- **Bytecode**: `OP_LOOP_GUARD_INIT` and `OP_LOOP_GUARD_CHECK` opcodes
- **Static Analysis**: Compile-time iteration count analysis for for-loops
- **Dynamic Guards**: Runtime monitoring for while-loops and complex cases

### Performance Characteristics
- **No Overhead**: Loops under 100K iterations have zero guard overhead
- **Minimal Overhead**: Guarded loops add ~2 instructions per iteration
- **Scalable**: System handles millions to billions of iterations efficiently

This test suite ensures the loop safety system works correctly across all scenarios and edge cases.