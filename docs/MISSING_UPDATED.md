# Orus Programming Language - Development Roadmap
*Balanced Optimization & Feature Development*

## 🎯 Vision & Goals
Build a language that combines Python's readability, Rust's safety, and Lua's performance through a register-based VM with static typing and modern features.

---

## 📊 Current Status Assessment (Updated)

### ✅ **What's Complete**
- **VM Foundation**: Register-based with 256 registers, 100+ opcodes
- **Lexer System**: Full tokenization with all language constructs  
- **Basic Parser**: Precedence climbing with binary expressions (`1 + 2`)
- **Variable System**: Declarations (`x = 42`) and lookup
- **Memory Management**: Garbage collector framework integrated
- **Build System**: Clean makefile with benchmarking
- **⚡ VM Optimizations Phase 1+2**: Fast dispatch, fused instructions (**19% faster than Lua on arithmetic!**)
- **🛡️ Safe Phase 3 Optimizations**: Conservative type-based optimizations for literal operations

### 🔄 **Partially Complete**
- **String Support**: Parsing works, value representation needs fixing
- **Boolean Values**: Parser ready, needs VM integration
- **Error Handling**: Basic framework, needs enhancement
- **⚡ Advanced VM Optimizations**: Loop fusion patterns (INC_CMP_JMP) ready for implementation

### 🎯 **Performance Status**
- **✅ Arithmetic**: **19% FASTER than Lua** (was 1.8x slower)
- **⚠️ Control Flow**: 2.1x slower than Lua (target: **beat Lua completely**)

---

## 📋 Phase 1: Core Language Foundation (Weeks 1-4)

### 1.1 Complete Basic Data Types
**Priority: 🔥 Critical**
- [x] **DONE**: Fix string support and add proper boolean operations to complete the basic type system.

### 1.2 Built-in Functions (Print & I/O)
**Priority: 🔥 Critical**
- [x] **DONE**: Basic `print(value)` statement implemented for program output.

### 1.3 String Interpolation System
**Priority: 🔥 High**
- [x] **DONE**: Implement full string interpolation with format specifiers and expressions.

### 1.4 Variable Assignments
**Priority: 🔥 Critical**
- [x] **DONE**: Basic assignment operations implemented.

### 1.5 Boolean and Comparison Operations
**Priority: 🔥 Critical**
- [x] **DONE**: Added logical operators and comparison operations.

---

## 📋 Phase 2: Control Flow & High-Performance Loops (Weeks 5-8)
*Integrating language features with control flow optimizations*

### 2.1 Conditional Statements + Branch Optimizations
**Priority: 🔥 High**
- [x] **DONE**: Implement if/else statements with nested conditions and `elif`, including jump patching and scoped blocks.

**🚀 Performance Enhancements:**
- [ ] **Branch prediction optimization**: Structure conditionals for CPU branch predictor
- [ ] **Jump table optimization**: For large elif chains and switch-like patterns
- [ ] **Short-circuit evaluation**: Optimized logical operators (and/or)

### 2.2 Loop Constructs + Loop Fusion Optimizations
**Priority: 🔥 High - KEY TO BEATING LUA**
- [x] **DONE**: Basic while/for loops with range syntax and break/continue
- [ ] **🚀 HIGH PRIORITY**: Implement loop fusion patterns (INC_CMP_JMP) to beat Lua

**Core Loop Features:**
- [x] While loop syntax parsing and basic compilation with condition hoisting
- [x] For loop with range syntax (`0..5`, `0..=5`, `0..10..2`) and bounds checking
- [x] Break and continue statements with proper scope handling
- [x] Nested loop support with labeled break/continue
- [x] Loop variable scoping, lifetime management, and register allocation optimization

**🚀 Critical Performance Optimizations (Phase 2.5):**
- [ ] **Loop Fusion (INC_CMP_JMP)**: Combine `i++; if(i<limit) jump` into single instruction
  - **Impact**: Reduce for-loop from 5 instructions to 2 per iteration
  - **Target**: This should close the 2.1x performance gap with Lua
- [ ] **Loop unrolling**: For small, known iteration counts (< 8 iterations)
- [ ] **Strength reduction**: Replace expensive operations with cheaper equivalents
- [ ] **Loop-invariant code motion**: Move constant expressions outside loops
- [ ] **Dead code elimination**: Remove unreachable code within loop bodies
- [ ] **Bounds check elimination**: For provably safe ranges

**Advanced Loop Features:**
- [ ] Iterator protocol for custom collection types
- [ ] Parallel loop execution hints (`@parallel for i in range`)
- [ ] SIMD vectorization support for numerical loops
- [ ] Generator-style lazy evaluation for large ranges

```orus
# ✅ Current working syntax
for i in 0..5:
    print(i)  # 0, 1, 2, 3, 4

for i in 0..10..2:
    print(i)  # 0, 2, 4, 6, 8

# 🚀 Target performance after loop fusion
# These loops will use optimized INC_CMP_JMP instruction
for i in 0..1000000:
    sum = sum + i  # Single fused instruction per iteration
```

### 2.3 Scope and Symbol Tables
**Priority: 🔥 High**
- [ ] **TODO**: Implement enterprise-grade lexical scoping with high-performance symbol resolution.

**Performance Requirements:**
- [ ] O(1) scope entry/exit with hash-based lookup (< 5ns average)
- [ ] Interned string keys for symbol names (memory deduplication)
- [ ] Register allocation optimization across scope boundaries

---

## 📋 Phase 3: Functions & Advanced Type Optimizations (Weeks 9-12)

### 3.1 Function Definition and Calls
**Priority: 🔥 High**
- [ ] **TODO**: Implement function declarations, calls, and return values with proper call frames.

**🚀 Performance Enhancements:**
- [ ] **Function inlining**: For small, frequently called functions
- [ ] **Tail call optimization**: Convert recursive calls to loops
- [ ] **Call site optimization**: Optimize hot call paths

```orus
// Basic function
fn add(a: i32, b: i32) -> i32:
    a + b

fn greet(name: string):
    print("Hello ", name)
```

### 3.2 Advanced Type System Optimizations
**Priority: 🔥 High**
**Building on completed safe Phase 3 optimizations**

**✅ Completed Safe Optimizations:**
- Conservative typed instructions for literal operations
- Type tracking for variable assignments
- Safe fallback to regular opcodes for complex expressions

**🚀 Next Phase Advanced Type Optimizations:**
- [ ] **Extended type inference**: Track types across function calls
- [ ] **Flow-sensitive typing**: Type narrowing in conditional branches
- [ ] **Aggressive type specialization**: Generate type-specific code paths
- [ ] **Compile-time type resolution**: Zero runtime type checking overhead

### 3.3 Closures and Upvalues
**Priority: 📋 Medium**
- [ ] **TODO**: Add support for nested functions and variable capture.

---

## 📋 Phase 4: Collections & Complete Type System (Weeks 13-16)

### 4.1 Basic Array Implementation
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add dynamic arrays with indexing, slicing, and common operations.

```orus
// Fixed-size array with type and length
nums: [i32; 3] = [1, 2, 3]
// Dynamic array
dynamic: [i32] = []
```

### 4.2 High-Performance Array Extensions
**Priority: 🔥 High** *(Advanced feature building on basic arrays)*
- [ ] **SIMD-aligned arrays**: `@align(32)` annotations for vectorization
- [ ] **Zero-copy array slicing**: With bounds checking elimination
- [ ] **SIMD-optimized operations**: Bulk operations for numerical arrays
- [ ] **Memory-mapped arrays**: For large datasets with lazy loading

### 4.3 Complete Type System Foundation
**Priority: 🔥 High**
- [ ] **Type Representation & Core Infrastructure** (based on IMPLEMENTATION_GUIDE.md)
- [ ] **Hindley-Milner Type Inference Engine**
- [ ] **Complete Numeric Type Conversion System**
- [ ] **Advanced Type Features**: Generics, constraints, type aliases

**🚀 High-Performance Type System:**
- [ ] **SIMD-optimized constraint checking**: Bulk type validation with AVX-512/NEON
- [ ] **Lock-free type cache**: Atomic operations for concurrent type access
- [ ] **Arena-allocated type objects**: Batch allocation/deallocation
- [ ] **Template specialization**: Common type patterns optimized at compile-time

### 4.4 Enhanced Error Reporting System
**Priority: 🔥 High**
- [ ] **TODO**: Implement the advanced error reporting system detailed in `ERROR_FORMAT_REPORTING.md`.

---

## 📋 Phase 5: Advanced Language Features (Weeks 17-20)

### 5.1 Struct and Enum Types
**Priority: 📋 Medium**
- [ ] **TODO**: Add user-defined types with methods and pattern matching.

### 5.2 Pattern Matching
**Priority: 📋 Medium**
- [ ] **TODO**: Implement match expressions with destructuring patterns.

### 5.3 Generics and Generic Constraints
**Priority: 📋 Medium**
- [ ] **TODO**: Add generic type parameters for functions and structs with constraint system.

**🚀 Performance Focus:**
- [ ] **Monomorphization**: Generate specialized concrete implementations
- [ ] **Zero-cost abstractions**: No runtime overhead for generics
- [ ] **Compile-time constraint resolution**: Eliminate runtime checks

---

## 📋 Phase 6: Module System & Advanced VM Optimizations (Weeks 21-24)

### 6.1 Module System
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add import/export functionality with module loading.

### 6.2 Standard Library Core
**Priority: 📋 Medium**
- [ ] **TODO**: Implement essential standard library modules.

### 6.3 Advanced VM Optimizations (Future)
**Priority: 📋 Low** *(After language core is complete)*

**Next-Generation Performance Features:**
- [ ] **JIT compilation**: For hot functions and loops
- [ ] **Escape analysis**: Stack allocation optimization
- [ ] **Method inlining**: Cross-module optimization
- [ ] **Dead code elimination**: Global optimization
- [ ] **Profile-guided optimization**: Runtime feedback for optimization

**Target Performance After All Optimizations:**
- Beat Lua by 40% on all benchmarks
- Beat Python by 10-15x on compute workloads
- Startup time < 5ms
- Memory usage < 10MB baseline

---

## 🎯 **Immediate Priorities (Next 2 Weeks)**

### **Week 1 - Beat Lua on Control Flow:**
- [ ] **Implement INC_CMP_JMP loop fusion** (compiler + VM opcodes)
- [ ] **Add loop unrolling for small constant ranges** 
- [ ] **Test control flow benchmarks** - target: beat Lua

### **Week 2 - Complete Phase 2:**
- [ ] **Optimize branch prediction** for conditionals
- [ ] **Implement main function entry point**
- [ ] **Add comprehensive loop safety system**

**Success Metric**: Orus becomes **faster than Lua** on control flow benchmarks

---

## 🏗️ **Development Philosophy**

### **Balanced Optimization Strategy:**
1. **Feature First**: Implement core language features for usability
2. **Optimize Smart**: Add performance optimizations that enhance user experience
3. **Measure Always**: Benchmark every optimization against real workloads
4. **Ship Incrementally**: Deliver value at each phase completion

### **Performance vs. Features Balance:**
- **Phase 1-2**: 70% features, 30% optimization (foundation)
- **Phase 3-4**: 50% features, 50% optimization (performance + capability)
- **Phase 5-6**: 30% features, 70% optimization (production ready)

### **Quality Gates:**
- All optimizations must pass existing tests
- No optimization should break language semantics
- Performance improvements must be measurable (>5% gain)
- Code must remain maintainable and debuggable

---

## 📊 **Success Metrics**

### **Performance Targets (Updated):**
- **✅ Arithmetic**: 19% faster than Lua (ACHIEVED)
- **🎯 Control Flow**: Beat Lua by 10% (NEXT MILESTONE)
- **🚀 Overall**: Beat Python by 10x, compete with C in hot loops
- **⚡ Development**: Sub-second compilation for 100k LOC

### **Developer Experience Goals:**
- **Error Messages**: Rust-quality with actionable suggestions
- **Debugging**: Rich source location and context
- **IDE Integration**: LSP with real-time type checking
- **Documentation**: Comprehensive examples and guides

### **Language Completeness:**
- **Type Safety**: Catch errors at compile-time
- **Performance**: No abstraction overhead
- **Expressiveness**: Clean syntax for complex ideas
- **Interoperability**: Easy C/FFI integration

---

This updated roadmap balances performance optimization with language feature development, ensuring Orus becomes both fast AND capable. The next major milestone is implementing loop fusion to beat Lua on control flow, then continuing with core language features while maintaining our performance advantage.