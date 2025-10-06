# Orus Roadmap

### **Language Goals**
- Type safety without verbosity
- Performance without complexity
- Expressiveness without magic
- Interoperability without friction

**Code Quality Standards:**
- Use clear, descriptive names
- Early return for error handling  
- Consistent naming conventions:
  - snake_case for functions
  - PascalCase for types
  - UPPER_CASE for constants


  ## 🎯 Success Metrics

### **Performance Targets**
- Beat Python by 1 or 2x in compute-heavy benchmarks
- Compete with Lua for scripting performance
- Startup time < 5ms for hello world
- Memory usage < 10MB baseline
- Compilation speed > 100k LOC/second

## Delivered ✅
- Lexer, parser, and Hindley–Milner type inference.
- Baseline compiler emitting bytecode without loop-specific optimisations.
- Virtual machine executing all opcodes through boxed value handlers.

## In Progress 🔄
- Improve diagnostics and error reporting quality.
- Expand language surface with additional standard library helpers.
- Grow the regression suite to cover more user programs.

## Next Steps
1. Finish the structured diagnostic renderer.
2. Add lifecycle analysis for variables.
3. Introduce more iterator-friendly syntax once the baseline execution paths are fully covered by tests.


### Advanced Loop Optimizations
- [ ] **TODO**: Dead code elimination within loop bodies (enables advanced type system optimizations)
- [ ] **PARTIAL**: Complex closure scenarios (local variable capture needs refinement) 🔧
- [ ] **NEXT**: Closure variable mutability support
- [ ] **NEXT**: Closure bytecode generation optimization (OP_CLOSURE_R)
- [ ] **NEXT**: Multiple upvalue capture optimization
- [ ] SIMD-aligned arrays with compile-time alignment annotations (`@align(32)`)

### Advanced features
- [ ] **Phase 6**: SIMD vectorization support for numerical loops
- [ ] **Phase 6**: Loop fusion optimization for adjacent compatible loops  
- [ ] **Phase 6**: Profiling integration for hot loop identification
- [ ] **Phase 6**: Iterator protocol for custom collection types
- [ ] **Phase 6**: Generator-style lazy evaluation for large ranges
- [ ] **Phase 6**: Parallel loop execution hints (`@parallel for i in range`)


## Final Behavior Summary (Canonical)

| Code                    | Behavior                       | Why                               |
| ----------------------- | ------------------------------ | --------------------------------- |
| `x = 10`                | ✅ `i32`                        | inferred                          |
| `x = 3.14`              | ✅ `f64`                        | inferred                          |
| `x = 10i64`             | ✅ `i64`                        | suffix                            |
| `x: u64 = 100`          | ✅ `i32` → `u64` if convertible |                                   |
| `x: f64 = 10`           | ✅ `i32` → `f64` if valid       |                                   |
| `x: f64 = 10u32`        | ❌                              | no implicit cross-type assignment |
| `x: f64 = 10u32 as f64` | ✅                              | cast required                     |


# Orus Typing System Roadmap

## ✅ Official Orus Typing Behavior (as per tutorial)

| Syntax                  | Valid? | Inferred/Parsed As                                  |
| ----------------------- | ------ | --------------------------------------------------- |
| `x = 10`                | ✅      | Inferred as `i32`                                   |
| `y = 3.14`              | ✅      | Inferred as `f64`                                   |
| `x = 10i64`             | ✅      | Explicit suffix → `i64`                             |
| `x: u32 = 10`           | ✅      | Explicit annotation, constant converted if possible |
| `x: i64 = 10u32`        | ❌      | ❌ Implicit coercion not allowed                     |
| `x: i64 = 10u32 as i64` | ✅      | ✅ Requires `as`    
