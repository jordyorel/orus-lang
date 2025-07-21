# Orus Programming Language - Development Roadmap

## 🎯 Vision & Goals
Build a language that combines Python's readability, Rust's safety, and Lua's performance through a register-based VM with static typing and modern features.

---

## 📊 Current Status Assessment

### ✅ **What's Complete**
- **VM Foundation**: Register-based with 256 registers, 100+ opcodes
- **Lexer System**: Full tokenization with all language constructs  
- **Advanced Parser**: Precedence climbing with complex expressions
- **Variable System**: Declarations (`x = 42`) with type inference
- **Memory Management**: Garbage collector with object pooling
- **Build System**: Clean makefile with comprehensive benchmarking
- **Type System**: Advanced Hindley-Milner type inference with arena allocation
- **Function System**: Complete function definitions and calls with type checking
- **Control Flow**: if/else, loops with type-aware scoping
- **String System**: Full string operations with type-safe concatenation
- **Type Coercion**: Automatic and explicit type conversions

### 🔄 **Partially Complete**
- **Generic System**: Foundation implemented, needs completion
- **Array System**: Type inference ready, needs collection implementation
- **Error Handling**: Basic framework, needs enhanced diagnostics

- [x] Variable assignments (`x = value`)
- [x] Control flow (`if`, `while`, `for`)
- [x] Functions (`fn name:`)
- [ ] Advanced type inference
- [ ] Arrays and collections (type system ready)
- [ ] Struct definitions and methods
- [ ] Pattern matching and enums
- [ ] Module system

---

## 📋 Phase 1: Core Language Foundation & Basic Type System (Weeks 1-4)

### 1.1 Complete Basic Data Types & Early Type Infrastructure
**Priority: 🔥 Critical**
- [x] **DONE**: Fix string support and add proper boolean operations to complete the basic type system.
- [ ] **NEW**: Build minimal type representation system (prepares for advanced features)

**Implementation Steps:**

- [*] Fix VALUE type conflicts for strings
- [ ] Implement string object allocation in compiler
- [*] Add string concatenation operator (`+`)
- [ ] Implement string comparison operators
- [*] **NEW**: Add basic TYPE enum and type checking infrastructure
- [*] **NEW**: Implement type annotation parsing (`x: i32 = 42`)
- [*] **NEW**: Add type mismatch error reporting foundation

### 1.2 Built-in Functions (Print & I/O)
**Priority: 🔥 Critical**
- [x] **DONE**: Basic `print(value)` statement implemented for program output.

```orus
// Basic print function
print("Hello, World!")
print(42)
print(true)

// Print with string interpolation
name = "Orus"
version = 1
print("Welcome to", name, "version", version)

// Multiple values
print("Values:", 1, 2, 3, "done")

// Print with newline (default) and without
print("Line 1")
print("Line 2")
print_no_newline("Same line: ")
print("continues here")
```

**Implementation Requirements:**
- [x] Basic `print(value)` statement with newline
- [x] Parse `print()` function calls with variable arguments
- [x] Support printing all basic types (i32, f64, bool, string)
- [x] Implement string interpolation with placeholders
- [*] Handle escape sequences (`\n`, `\t`, `\"`, `\\`)
- [ ] Format numbers and booleans for display

### 1.3 String Interpolation System
**Priority: 🔥 High**
- [x] **DONE**: Implement full string interpolation with format specifiers and expressions.

```orus
// Basic interpolation
x = 42
print("The answer is", x)

// Multiple placeholders
a = 10
b = 20
print(a, "+", b, "=", a + b)

// Format specifiers
pi = 3.14159
print("Pi rounded: ", pi)  // "Pi rounded: 3.14"

// Expression interpolation
items = [1, 2, 3]
print("Array has items", len(items))
```

### 1.4 Variable Assignments & Basic Type Checking
**Priority: 🔥 Critical**
- [x] **DONE**: Basic assignment operations implemented.
- [*] **NEW**: Add compile-time type checking for assignments

**Features to Implement:**
- [x] `x = value` syntax parsing
- [*] Mutable vs immutable variables (`mut x = 42`)
- [*] Compound assignments (`+=`, `-=`, `*=`, `/=`)
- [x] Type annotations (`x: i32 = 42`)
- [x] **DONE**: Unary operators (`-x`, `not x`) and negative literal parsing
- [x] **DONE**: Runtime string concatenation for variables and expressions
- [ ] **NEW**: Type coercion rules for numeric types

### 1.5 Boolean and Comparison Operations
**Priority: 🔥 Critical**
- [x] **DONE**: Added logical operators and comparison operations.

### 1.6 Fundamental Error Reporting Infrastructure
**Priority: 🔥 Critical**
- [ ] **NEW**: Implement basic structured error reporting (foundation for advanced features)
- [ ] **NEW**: Add source location tracking in parser
- [ ] **NEW**: Create error severity levels (Error, Warning, Info)
- [ ] **NEW**: Implement multi-line error display with context
- [ ] **NEW**: Add "Did you mean?" suggestion framework

---

## 📋 Phase 2: Control Flow & Progressive Optimization (Weeks 5-8)

### 2.1 Conditional Statements & Basic Optimization
**Priority: 🔥 High**
- [*] **Done**: Implement if/else statements with nested conditions and `elif`, including jump patching and scoped blocks.
- [*] **Done**: Add basic dead code elimination for unreachable branches
- [*] **Done**: Implement constant folding for compile-time known conditions
- [ ] **Todo**: If/else and loops should create new scopes in Orus no need 'mut' inside loop or if statement

```orus
// Target syntax:
if condition:
    // block
elif other_condition:
    // block  
else:
    // block

print("ok") if x == 1 elif x == 2 else print("fallback")
- [*] // Ternary operator 
result = x > 0 ? "positive" : "non-positive"
```

** ✅ Correct behavior: temp and other live only inside their blocks.
** ✅ result was declared outside and mutated — no need to redeclare mut

```orus
mut result = 0

if condition:
    result = 42  // ✅ Assigning existing mutable variable
    mut temp = 99  // ✅ Declared only inside the if-block
else:
    result = -1
    mut other = 123

print(result)         // ✅ OK
print(temp)           // ❌ Error: temp is out of scope
print(other)          // ❌ Error: other is out of scope
```

** ✅ Loop body has its own scope — variables declared inside are local to each iteration.
```orus
mut sum = 0

for i in 0..5:
    sum = sum + i  // ✅ Using outer variable
    mut loop_val = i * 10
    print(loop_val)

print(sum)        // ✅ OK
print(loop_val)   // ❌ Error: loop_val is out of scope
```

** ✅ Encourages clarity: no shadowing unless explicitly intended via a new block.
```orus
mut x = 10

if something:
    x = 20         // ✅ Assign
    mut x = 30     // ❌ Error: can't redeclare `x` in the same scope hierarchy
```

### 2.2 Loop Constructs & Performance Foundations
**Priority: 🔥 High**
- [x] **DONE**: Implement high-performance loop constructs with progressive optimization features.
- [x] **DONE**: Build optimization framework that supports advanced features

**Core Implementation Requirements:**
- [x] **DONE**: While loop syntax parsing and basic compilation with condition hoisting
- [x] **DONE**: For loop with range syntax (`0..5`, `0..=5`, `0..10..2`) and bounds checking
- [ ] **TODO**: For loop with iterator syntax (`for item in collection`) with zero-copy iteration
- [x] **DONE**: Break and continue statements with proper scope handling and jump table optimization
- [x] **DONE**: Replace fixed-size break/continue jump arrays with dynamic vectors
- [ ] **TODO**: Nested loop support with labeled break/continue for arbitrary loop depth
- [x] **DONE**: Loop variable scoping, lifetime management, and register allocation optimization
- [x] **DONE**: Advanced Orus Range Syntax: `start..end..step` with direction validation

**Performance & Safety Requirements (Progressive Implementation):**
- [x] **DONE**: Loop invariant code motion (LICM) optimization with full expression replacement system *(Elegant auto-mutable variables + hoisting)*
- [x] **DONE**: Loop unrolling for small, known iteration counts (implemented with comprehensive optimization)
- [ ] **TODO**: Strength reduction for induction variables (foundation for advanced math optimizations)
- [ ] **TODO**: Dead code elimination within loop bodies (enables advanced type system optimizations)
- [ ] **TODO**: Bounds check elimination for provably safe ranges (requires type system)
- [ ] **Phase 6**: Memory allocation minimization in loop constructs (enables advanced memory management)
- [ ] **Phase 6**: Branch prediction hints for common loop patterns (enables advanced profiling)


## 🎯 Orus Range Syntax: `start..end..step`

**High-Performance Range Implementation Requirements:**

### ✅ Core Syntax

```orus
for i in start..end..step:
    // Loop body executes with optimized iteration
```

**Range Components:**
* **`start`**: Inclusive start value (compile-time or runtime expression)
* **`end`**: Exclusive upper bound (bounds-checked for safety)
* **`step`**: Optional stride (must be non-zero, defaults to 1)

**Compile-Time Optimizations:**
- [x] Constant range folding for known bounds
- [x] Loop unrolling for small iteration counts (≤ 8 iterations)
- [x] Strength reduction for power-of-2 steps (framework implemented)
- [x] Dead iteration elimination for unreachable ranges

---

### ✅ Performance-Optimized Examples

```orus
# Constant range (compile-time optimized)
for i in 0..5:          // Unrolled: 0, 1, 2, 3, 4
    process_fast(i)

# Power-of-2 step (strength reduction)
for i in 0..16..2:      // Optimized: 0, 2, 4, 6, 8, 10, 12, 14
    handle_even(i)

# Reverse iteration (direction-aware)
for i in 10..0..-2:     // Countdown: 10, 8, 6, 4, 2
    countdown_step(i)

# Runtime bounds with bounds checking
n = get_array_size()
for i in 0..n:          // Bounds-checked at runtime
    if is_valid_index(i):
        process_array_element(i)

# Large range with memory efficiency
for i in 0..1000000:    // Iterator-based, O(1) memory
    if should_process(i):
        handle_large_dataset_item(i)

# Nested ranges with optimization
for row in 0..height:
    for col in 0..width..2:  // Inner loop optimized for even columns
        process_matrix_cell(row, col)
```

---

### 🛑 Compile-Time Validation & Runtime Safety

**Static Analysis Rules:**
* `step` cannot be `0` — **compile-time error** with helpful suggestion
* Direction consistency enforced at compile-time when bounds are constant:
  * `start < end` → `step > 0` (ascending iteration)
  * `start > end` → `step < 0` (descending iteration)
  * `start == end` → empty iteration (compile-time warning)

**Runtime Safety Mechanisms:**
* Overflow detection for large ranges and steps
* Stack depth monitoring for deeply nested loops
* Iteration count limits to prevent runaway loops
* Memory allocation tracking for range objects

```orus
// ❌ Compile-time errors with enhanced diagnostics
for i in 0..5..0:      // Error: "Step cannot be zero. Did you mean step=1?"
for i in 0..5..-1:     // Error: "Negative step in ascending range (0 < 5). Use positive step or reverse bounds."
for i in 5..0..1:      // Error: "Positive step in descending range (5 > 0). Use negative step or reverse bounds."

# ⚠️ Compile-time warnings for suspicious patterns
for i in 0..0:         // Warning: "Empty range (start == end). Loop body will never execute."
for i in 0..MAX_INT:   // Warning: "Very large range detected. Consider chunking for memory efficiency."

# ✅ Runtime safety with graceful handling
for i in get_start()..get_end()..get_step():  // Runtime validation with fallback
    if not is_safe_iteration(i):
        break  // Safe termination on overflow/underflow
    process_dynamic_range(i)
```

### 🔍 Edge Case Behavior & Performance Characteristics

**Empty Range Handling:**
* `for i in 0..0`: Loop body skips entirely (O(1) detection)
* `for i in 5..5`: Same behavior, consistent with mathematical intervals

**Boundary Conditions:**
* Integer overflow/underflow detection with safe fallback
* Maximum iteration count enforcement (configurable limit)
* Step size validation for numeric stability

**Memory & Performance:**
* Small constant ranges: Unrolled at compile-time
* Large ranges: Iterator pattern with O(1) memory usage  
* Runtime ranges: Bounds checking with branch prediction optimization
* Negative steps: Optimized reverse iteration without temporary storage

**Optimization Guarantees:**
* No heap allocation for simple integer ranges
* SIMD vectorization hints for numerical processing loops
* Loop-invariant code motion for expressions outside iteration variable
* Automatic parallelization detection for independent iterations


### 2.3 Scope and Symbol Tables & Early Generic Preparation
**Priority: 🔥 High**
- [ ] **TODO**: Implement enterprise-grade lexical scoping with high-performance symbol resolution.
- [ ] **NEW**: Design symbol table structure to support generics and advanced type features

**Core Scoping Requirements:**
- [ ] Lexical scoping with proper variable shadowing semantics
- [ ] Nested scope management with O(1) scope entry/exit
- [ ] Symbol table optimization with hash-based lookup (< 5ns average)
- [ ] **NEW**: Compile-time scope analysis and variable lifetime tracking
- [ ] **NEW**: Register allocation optimization across scope boundaries
- [ ] **NEW**: Closure capture analysis for upvalue optimization *(Comprehensive analysis of variable capture patterns across scope boundaries, with heap allocation optimization and register prioritization)*
- [ ] **NEW**: Dead variable elimination in complex scope hierarchies *(Conservative dead code elimination with complex lifetime analysis, write-only variable detection, and register reclamation)*

**Advanced Symbol Table Features:**
- [ ] Interned string keys for symbol names (memory deduplication)
- [ ] Scope-aware variable type inference and propagation
- [ ] Cross-scope optimization for frequently accessed variables
- [ ] Debugging symbol preservation with source location mapping
- [ ] Hot path optimization for global and local variable access
- [ ] Memory layout optimization for stack-allocated variables
- [ ] Scope pollution detection and prevention

---

## 📋 Phase 3: Functions & First-Class Values (Weeks 9-12)

### 3.1 Function Definition and Calls
**Priority: 🔥 High**
- [x] **COMPLETE**: Function declarations, calls, and return values implemented ✅

** Implementation Status:**
- [x] Function syntax parsing: `fn name(params) -> return_type:` ✅
- [x] Parameter type annotations: `a: i32, b: string` ✅
- [x] Return type annotations: `-> i32` ✅
- [x] Function calls with arguments: `add(1, 2)` ✅
- [x] Multiple parameter functions work correctly ✅
- [x] Void functions: functions without return type ✅
- [x] Basic bytecode generation for OP_CALL_R and OP_RETURN_R ✅
- [x] Function object types (ObjFunction, VAL_FUNCTION) ✅
- [x] Memory management for function objects ✅
- [x] Type system integration for VAL_FUNCTION ✅
- [x] Forward references between functions ✅
- [x] Comprehensive test suite: basic/, edge_cases/ ✅
- [x] **COMPLETE**: Register allocation optimization for function parameters ✅
- [x] **COMPLETE**: Support for 250+ function parameters (480% improvement) ✅

```orus
// Basic function
fn add(a: i32, b: i32) -> i32:
    a + b

fn greet(name: string):
    print("Hello ", name)
```

** Function System Status:**
- [x] Function definitions with parameter and return types ✅
- [x] Function calls with argument passing (all parameters work correctly) ✅
- [x] Type inference for function parameters and return values ✅
- [x] Basic recursion support (simple cases working) ✅
- [x] Forward function references ✅
- [x] **COMPLETE**: Register allocation optimization for function parameters ✅
- [x] **COMPLETE**: Hierarchical register system (frame + spill registers) ✅
- [x] **COMPLETE**: Support for 250+ function parameters ✅
- [x] **COMPLETE**: Function objects as first-class values ✅
- [x] **COMPLETE**: Higher-order functions (functions as parameters/return values) ✅
- [ ] **IN PROGRESS**: Closure capture and upvalues 🔧
- [ ] **NEXT**: Lambda/anonymous function syntax
- [ ] **NEXT**: Generic constraints for higher-order functions

**Parameter Support:**
- ✅ **Parameters 1-64**: Frame registers (optimal performance)
- ✅ **Parameters 65-250+**: Spill registers (HashMap storage)
- ⚠️ **Parameters 300+**: Known issue (investigation pending)
- 🏗️ **Architecture**: Supports unlimited parameters by design

### 3.2 Advanced Type System Features
**Priority: 🔥 High**
- [x] **COMPLETE**: Advanced Hindley-Milner type inference system ✅
- [x] **COMPLETE**: Arena-based type object management ✅
- [x] **COMPLETE**: Constraint-based solving with error reporting ✅
- [x] **COMPLETE**: Type variable unification with occurs check ✅
- [x] **COMPLETE**: Polymorphic type schemes for generic programming ✅
- [x] **COMPLETE**: Primitive type conversions and coercion ✅
- [ ] **NEW**: Complete generic system implementation
- [ ] **NEW**: Enhanced error diagnostics with type information

### 3.3 Closures and Upvalues (Partially Complete)
**Priority: 🔥 High**
- [x] **COMPLETE**: VM-level closure infrastructure (ObjClosure, ObjUpvalue, opcodes) ✅
- [x] **COMPLETE**: Basic function system with first-class functions ✅
- [x] **COMPLETE**: Higher-order functions (functions as parameters/return values) ✅
- [x] **COMPLETE**: Basic closure capture analysis (outer scope variable lookup) ✅
- [x] **COMPLETE**: Upvalue access compilation (OP_GET_UPVALUE_R, OP_SET_UPVALUE_R) ✅
- [x] **WORKING**: Simple closure scenarios (function parameter capture) ✅
- [ ] **PARTIAL**: Complex closure scenarios (local variable capture needs refinement) 🔧
- [ ] **NEXT**: Closure variable mutability support
- [ ] **NEXT**: Closure bytecode generation optimization (OP_CLOSURE_R)
- [ ] **NEXT**: Multiple upvalue capture optimization
- [ ] **NEXT**: Lambda/anonymous function syntax
- [ ] **NEXT**: Complex closure scenarios (multiple nesting levels)
- [ ] **NEXT**: TODO: Implement proper mutability tracking for upvalues

```orus
// ✅ WORKING: Basic functions with type checking
fn add(a: i32, b: i32) -> i32:
    a + b

// ✅ WORKING: Higher-order functions  
fn applyTwice(func, value):
    return func(func(value))

// ✅ WORKING: First-class functions
addFunc = add
result = addFunc(5, 3)

// 🔧 IN PROGRESS: Closure capture
fn makeCounter(start):
    count = start
    
    fn increment():         // Nested function
        count = count + 1   // ← Captures 'count' from outer scope
        return count
    
    return increment

counter = makeCounter(0)
print(counter())  // Should print 1
print(counter())  // Should print 2
```

---

## 📋 Phase 4: Collections & Advanced Type Features (Weeks 13-16)

### 4.1 Basic Array Implementation & Generic Type Preparation
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add dynamic arrays with indexing, slicing, and common operations.
- [ ] **NEW**: Design arrays with generic type support in mind (prepares for advanced features)
- [ ] **NEW**: Implement basic generic syntax parsing for arrays ([T])

```orus
// Fixed-size array with type and length
nums: [i32; 3] = [1, 2, 3]

// Array fill expression (value; length)
zeros = [0; 5]           // [i32; 5]

// Slicing (end-exclusive)
slice = nums[0..2]       // elements 0 and 1

// Dynamic array (no length annotation)
dynamic: [i32] = []
push(dynamic, 42)
pop(dynamic)

// Iterating over array
for val in nums:
    print(val)

// Comprehension
evens = [x for x in nums if x % 2 == 0]
```

### 4.2 High-Performance Array Extensions
**Priority: 🔥 High** *(Advanced feature building on basic arrays)*
- [ ] **TODO**: Implement high-performance array optimizations with SIMD support, type specialization, and advanced memory layout optimization.

**Enhanced Array Features & Requirements:**
- [ ] Build upon basic array syntax with performance optimizations
- [ ] SIMD-aligned arrays with compile-time alignment annotations (`@align(32)`)
- [ ] Multi-dimensional arrays with optimized memory layouts
- [ ] Zero-copy array slicing with bounds checking elimination  
- [ ] SIMD-optimized operations for numerical arrays (sum, map, filter, reduce)
- [ ] Memory-mapped arrays for large datasets with lazy loading
- [ ] Concurrent array operations with work-stealing parallelization

```orus
// Enhanced arrays building on basic array syntax
// Performance-optimized versions of basic arrays
@align(32) simd_nums: [f32; 1024] = [0.0; 1024]  // SIMD-aligned
@layout(soa) particles: [Particle] = []          // Structure of arrays
```

### 4.3 Advanced Type System Features (Building on Phase 3)
**Priority: 🔥 High**
- [ ] **TODO**: Implement advanced type system features building on Phase 3 foundations.
- [ ] **NEW**: Add generic type parameters and constraints
- [ ] **NEW**: Implement type inference for complex expressions

#### 4.3.1 Type Representation & Core Infrastructure
**Based on IMPLEMENTATION_GUIDE.md Type Representation template**
- [x] **Type enum with comprehensive coverage**: TYPE_UNKNOWN, TYPE_I32, TYPE_I64, TYPE_U32, TYPE_U64, TYPE_F64, TYPE_BOOL, TYPE_STRING, TYPE_VOID, TYPE_NIL, TYPE_ARRAY, TYPE_FUNCTION, TYPE_STRUCT, TYPE_ENUM, TYPE_GENERIC, TYPE_ANY
- [ ] **Type struct with union for different kinds**: Array, Function, Struct, Enum, Generic type variants
- [ ] **Type mutability and nullability flags**: `is_mutable`, `is_nullable` attributes
- [ ] **Type operations**: `type_new()`, `types_equal()`, `type_assignable_to()`, `type_union()`, `type_intersection()`, `type_free()`
- [ ] **Common type constructors**: `primitive_type()`, `array_type()`, `function_type()`, `generic_type()`
- [x] **Arena allocation for Type objects**: Zero-allocation type creation with batch deallocation
- [ ] **Type interning system**: Deduplicate identical types for memory efficiency
- [x] **Basic variable type tracking**: Compiler locals array with type information *(IMPLEMENTED)*

#### 4.3.2 Type Inference Engine (Hindley-Milner)
**Based on IMPLEMENTATION_GUIDE.md Type Inference Engine template**
- [ ] **TypeInferer struct**: `next_type_var`, `substitutions` HashMap, `constraints` Vec, `env` HashMap
- [ ] **Fresh type variable generation**: Unique type variables for inference
- [ ] **Type environment management**: Variable → Type mapping with scope handling
- [ ] **Constraint generation**: Type constraints from expressions and function calls
- [ ] **Unification algorithm**: `unify()` with occurs check and substitution
- [ ] **Constraint solving**: Iterative constraint resolution with substitution application
- [ ] **Type instantiation**: Generic type parameter instantiation
- [ ] **Literal type inference**: Automatic type detection from values
- [ ] **Binary operation type inference**: Numeric, comparison, and equality operations
- [ ] **Function type inference**: Parameter and return type inference
- [ ] **Conditional type inference**: If/else branch type unification

#### 4.3.5 Advanced Type Features
**Future-proof type system extensions**
- [ ] **Type constraint system**: Numeric, Comparable, Equatable, Display, Debug traits
- [ ] **Generic type parameters**: `<T>`, `<T: Constraint>` with bounds checking
- [ ] **Generic constraint solving**: Type parameter unification with bounds
- [ ] **Monomorphization**: Generate specialized concrete implementations
- [ ] **Type aliases**: `type UserId = i64` syntax and semantics
- [ ] **Pattern matching types**: Exhaustiveness checking for enum variants
- [ ] **Associated types**: Types associated with traits/interfaces
- [ ] **Higher-kinded types (future)**: Types that take type parameters
- [ ] **Dependent types (future)**: Types that depend on runtime values
- [ ] **Linear types (future)**: Move semantics and ownership tracking

#### 4.3.6 High-Performance Implementation
**Zero-cost abstraction with enterprise-grade performance**
- [ ] **SIMD-optimized constraint checking**: Bulk type validation with AVX-512/NEON
- [ ] **Lock-free type cache**: Atomic operations for concurrent type access
- [x] **Arena-allocated type objects**: Batch allocation/deallocation for performance
- [ ] **Hash-based unification**: Precomputed type fingerprints for fast comparison
- [ ] **Template specialization**: Common type patterns optimized at compile-time
- [ ] **Compile-time type resolution**: Zero runtime type checking overhead
- [ ] **Type inference caching**: Memoization for expensive computations
- [ ] **Cross-function type propagation**: Inter-procedural analysis optimization

```orus
// 4.3.1 Type Representation & Core Infrastructure Examples
x: i32 = 42           // Explicit primitive type annotation
y = 42                // Type inference to i32
name: string = "Orus" // String type with interning
flag: bool = true     // Boolean type
data: [i32] = [1, 2, 3]  // Array type with element type

// Type mutability and nullability
mut counter: i32 = 0      // Mutable integer
optional: i32? = nil      // Nullable integer
immutable: i32 = 42       // Immutable by default

// 4.3.2 Type Inference Engine (Hindley-Milner) Examples
fn identity(x) -> auto:       // Type inference for parameters and return
    x                         // Inferred as <T>(T) -> T

fn add(a, b):                 // Full type inference
    a + b                     // Inferred based on usage context

result = add(1, 2)        // Infers add: (i32, i32) -> i32
float_result = add(1.0, 2.0)  // Infers add: (f64, f64) -> f64

// Complex inference with constraints
fn compare(a, b):
    a < b                     // Infers <T: Comparable>(T, T) -> bool

// 4.3.3 Numeric Type Conversion System Examples
small: i32 = 42
big: i64 = small          // Implicit i32 → i64 promotion
precise: f64 = small      // Implicit i32 → f64 conversion

// Explicit conversions with `as` operator  
big_val: i64 = 5000000000
truncated: i32 = big_val as i32      // Explicit i64 → i32 truncation
unsigned: u32 = small as u32         // Explicit i32 → u32 reinterpretation
float_val: f64 = 3.14159
rounded: i32 = float_val as i32      // Explicit f64 → i32 truncation

// Boolean conversions
flag: bool = true
flag_num: i32 = flag as i32          // Explicit bool → i32 (true = 1)
zero: i32 = 0
zero_flag: bool = zero as bool       // Explicit i32 → bool (0 = false)

// 4.3.4 Type Conversion VM Opcodes (Implementation Examples)
// These conversions generate specific VM opcodes:
promoted = small_int as i64          // Generates OP_I32_TO_I64_R
demoted = big_int as i32             // Generates OP_I64_TO_I32_R  
float_conv = int_val as f64          // Generates OP_I32_TO_F64_R
int_conv = float_val as i32          // Generates OP_F64_TO_I32_R

// 4.3.5 Advanced Type Features Examples
// Generic type parameters with constraints
fn identity<T>(x: T) -> T:
    x

fn add<T: Numeric>(a: T, b: T) -> T:
    a + b

fn min<T: Comparable>(a: T, b: T) -> T:
    a if a < b else b

// Generic struct with multiple constraints
struct Container<T: Clone + Display>:
    value: T
    
    fn show(self):
        print("Container: ", self.value)
    
    fn duplicate(self) -> T:
        self.value.clone()

// Type aliases for domain modeling
type UserId = i64
type Temperature = f64
type Count = u32

fn process_user(id: UserId, temp: Temperature):
    // Type-safe domain specific parameters
    if temp > 100.0:
        print("User" id, "has high temperature:", temp)

// Pattern matching with exhaustive type checking
enum Result<T, E>:
    Ok(T)
    Error(E)

fn handle_result<T, E>(result: Result<T, E>) -> T:
    match result:
        Ok(value): value
        Error(err): panic("Error: ", err)

// Advanced inference with generic collections
numbers = [1, 2, 3]               // Inferred as [i32]
floats = [1.0, 2.0, 3.0]         // Inferred as [f64]
generic_map = map(numbers, |x| x * 2)  // Inferred as [i32]

// Function overloading with trait constraints
trait Display:
    fn display(self) -> string

trait Debug:
    fn debug(self) -> string

fn print<T: Display>(value: T):
    output = value.display()
    // Implementation for displayable types
    
fn debug_print<T: Debug>(value: T):
    output = value.debug()
    // Implementation for debuggable types

// 4.3.6 High-Performance Implementation (Transparent to user)
// These features work automatically for performance:
fn bulk_convert(numbers: [i32]) -> [i64]:
    // SIMD-optimized bulk conversion using OP_I32_TO_I64_R
    numbers.map(|x| x as i64)

fn type_check_batch<T: Numeric>(values: [T]):
    // SIMD-optimized constraint checking
    for value in values:
        ensure_numeric(value)  // Batch type validation

// Zero-cost abstractions - no runtime overhead
fn generic_sort<T: Comparable>(arr: [T]):
    // Monomorphized to specific types at compile-time
    // No virtual calls or runtime type checking
    quick_sort(arr, |a, b| a < b)

// Type inference caching (transparent performance optimization)
fn complex_inference():
    result = deeply_nested_generic_function(
        another_generic(some_value),
        yet_another_generic(other_value)
    )
    // Type inference results cached for repeated compilations
    result
```

### 4.4 Enhanced Error Reporting System
**Priority: 🔥 High**
**Note: This feature deserves a separate implementation file due to its comprehensive nature**
- [ ] **TODO**: Implement the advanced error reporting system detailed in `ERROR_FORMAT_REPORTING.md`.

The error reporting system should combine Rust's precision with Elm's empathy, providing:

**Core Requirements:**
- Structured multi-section error format with visual hierarchy
- Human-centered language that avoids blame
- Actionable suggestions and helpful context
- Error categorization with consistent numbering (E0000-E9999)
- CLI presentation with colors and Unicode framing
- Integration with parser, type checker, and runtime

**Example Output Format:**
```
-- TYPE MISMATCH: This value isn't what we expected ------- main.orus:3:15

3 | x: i32 = "hello"
              |   ^^^^^ this is a `string`, but `i32` was expected
              |
              = Orus expected an integer here, but found a text value instead.
              = help: You can convert a string to an integer using `int("...")`, if appropriate.
              = note: Strings and integers can't be mixed directly.
```

**Implementation Notes:**
- Create dedicated `src/error/` directory for error infrastructure
- Implement error recovery in parser for multiple error reporting
- Add structured error types with rich formatting capabilities
- Integrate with type system for constraint violation reporting

### 4.5 Advanced Variable Error Detection
**Priority: 📋 Medium-High**
**Note: These features require architectural improvements to Orus's variable system**

**❌ Missing Advanced Variable Error Detection:**
- [ ] **TODO**: Duplicate variable declarations - Currently disabled due to architectural limitations where all variables are globals, causing false positives in loop contexts
- [ ] **TODO**: Use before initialization - Variables declared but used before being assigned a value
- [ ] **TODO**: Out-of-scope access - Using variables outside their declared scope (partially works, needs refinement)
- [ ] **TODO**: Const variable reassignment - When Orus adds const variables
- [ ] **TODO**: Advanced scope violations - Complex nested scope edge cases and lifetime management
- [ ] **TODO**: Variable lifetime issues - Using variables after they should be destroyed or go out of scope

**Implementation Requirements for Missing Features:**
- [ ] Implement proper local variable architecture (currently all variables are globals)
- [ ] Add variable initialization tracking in compiler
- [ ] Enhance scope analysis for complex nested contexts
- [ ] Add const variable support to language
- [ ] Implement variable lifetime analysis
- [ ] Add more sophisticated scope violation detection

**Example Error Messages to Implement:**
```
-- ERROR: Variable used before initialization ------------ main.orus:5:12

5 | print(uninitialized_var)
         ^^^^^^^^^^^^^^^^^ variable 'uninitialized_var' is used here but may not be initialized
         
= help: Make sure to assign a value before using this variable
= note: Variables must be initialized before they can be used

-- ERROR: Variable used outside of scope ----------------- main.orus:8:5

8 | print(loop_var)
        ^^^^^^^^ variable 'loop_var' is not accessible here
        
= help: This variable was declared inside a loop and is not available outside
= note: Variables declared in loops have limited scope
```

**Current Status:**
- ✅ Basic variable error detection implemented (undefined variables, immutable modification, loop protection)
- ❌ Advanced variable lifetime and scope analysis needs architectural improvements

---

## 📋 Phase 5: Advanced Language Features & Optimization (Weeks 17-20)

### 5.1 Struct and Enum Types & Closures
**Priority: 📋 Medium**
- [ ] **TODO**: Add user-defined types with methods and pattern matching.
- [ ] **NEW**: Implement closures and upvalues (moved from Phase 3 after type system is solid)
- [ ] **NEW**: Add method dispatch optimization using type system knowledge

```orus
// Struct definition
struct Point:
    x: i32
    y: i32

impl Point:
    fn new(x: i32, y: i32) -> Point:
        Point{ x: x, y: y }
    
    fn move_by(self, dx: i32, dy: i32):
        self.x = self.x + dx
        self.y = self.y + dy

// Enum with associated data
enum Status:
    Ok
    NotFound
    Error(message: string)
```

### 5.2 Pattern Matching
**Priority: 📋 Medium**
- [ ] **TODO**: Implement match expressions with destructuring patterns.

```orus
// Match expressions
match value:
    0: print("zero")
    1: print("one")
    _: print("other")
```

### 5.3 Advanced Generic Features & Monomorphization
**Priority: 📋 Medium**
- [ ] **TODO**: Add advanced generic features building on Phase 4 foundations.
- [ ] **NEW**: Implement monomorphization for performance
- [ ] **NEW**: Add higher-order generic constraints

**Implementation Strategy:**
- [ ] Add generic type parameter parsing (`<T>`, `<T: Constraint>`)
- [ ] Implement monomorphization (create specialized versions for each concrete type)
- [ ] Add type parameter substitution in compiler
- [ ] Create constraint system for type bounds (`Numeric`, `Comparable`)
- [ ] Add generic type checking and inference
- [ ] Implement generic struct instantiation with type specialization

**Implementation Steps:**
- [ ] Parse generic syntax in function and struct declarations
- [ ] Build generic symbol table with type parameters
- [ ] Implement type substitution during compilation
- [ ] Add monomorphization phase to generate concrete implementations
- [ ] Create constraint checking system for type bounds
- [ ] Add generic type inference for function calls

```orus
// Generic functions
fn identity<T>(x: T) -> T:
    x

// Generic structs
struct Box<T>:
    value: T

// Generic constraints
fn add<T: Numeric>(a: T, b: T) -> T:
    a + b

fn min<T: Comparable>(a: T, b: T) -> T:
    a if a < b else b

// Usage
fn main:
    a = identity<i32>(5)
    b: Box<string> = Box{ value: "hi" }
    result = add<i32>(10, 20)
```

---
### 5.4 Advanced Loop Optimizations (Moved to Phase 6)
**Note: These advanced features have been moved to Phase 6 for better integration with the module system and production features:**
- [ ] **Phase 6**: SIMD vectorization support for numerical loops
- [ ] **Phase 6**: Loop fusion optimization for adjacent compatible loops  
- [ ] **Phase 6**: Profiling integration for hot loop identification
- [ ] **Phase 6**: Iterator protocol for custom collection types
- [ ] **Phase 6**: Generator-style lazy evaluation for large ranges
- [ ] **Phase 6**: Parallel loop execution hints (`@parallel for i in range`)

 
```orus
# Basic while loop with performance considerations
while condition:
    print("looping")
    if should_break: break
    if should_skip: continue

# While loop with compound condition and short-circuit evaluation
mut i = 0
mut done = false
while i < 10 and not done and is_valid(i):
    i = i + 1
    if i % 2 == 0: continue
    print("Odd number ", i)
    done = check_completion(i)

# High-performance integer range loops
for i in 0..5:
    print("Index: ", i)  // 0, 1, 2, 3, 4 (exclusive end)

for i in 0..=5:
    print("Index: ", i)  // 0, 1, 2, 3, 4, 5 (inclusive end)

# Advanced range syntax with step and direction validation
for i in 0..10..2:
    print("Even: ", i)  // 0, 2, 4, 6, 8 (step=2) - WORKING

for i in 10..0..-2:
    print("Countdown: ", i)  // 10, 8, 6, 4, 2 (negative step) - PLANNED

# Range with runtime bounds (bounds checking required)
start = get_start_value()
end = get_end_value()
for i in start..end:
    if is_safe_index(i):
        process_element(i)

# Collection iteration with zero-copy semantics
for item in large_collection:
    if item.is_valid():
        transform_item(item)

# Nested loops with labeled break/continue for complex control flow
'outer: for i in 0..matrix_height:
    'inner: for j in 0..matrix_width:
        value = matrix[i][j]
        if value == SKIP_MARKER: 
            continue 'inner  # Skip to next column
        if value == ROW_TERMINATOR:
            break 'inner     # Skip to next row
        if value == ABORT_SIGNAL:
            break 'outer     # Exit both loops
        process_cell(i, j, value)

# Performance-critical numerical loop with optimization hints
@simd @unroll(4)
for i in 0..vector_size:
    result[i] = alpha * x[i] + beta * y[i]  # SAXPY operation

# Loop with resource management and timeout protection
@timeout(5000ms) @max_iterations(1000000)
while server.is_running():
    request = server.accept_connection()
    match request:
        Some(req): 
            if process_request(req).is_error():
                continue  // Skip failed requests
        None:
            if server.should_shutdown():
                break

# Generator-style lazy evaluation for memory efficiency
for prime in prime_generator(1..1000000):
    if prime > target_threshold:
        break
    add_to_prime_cache(prime)

# Loop with early termination and cleanup
for item in large_dataset:
    result = expensive_computation(item)
    match result:
        Error(err):
            log_error("Processing failed", err)
            cleanup_partial_state()
            break
        Complete(data):
            if data.confidence < MIN_CONFIDENCE:
                continue  // Skip low-quality results
            accumulate_result(data)
        Partial(data):
            if should_continue_processing():
                continue
            else:
                finalize_partial_result(data)
                break

// Infinite loop with explicit semantics and safety guards
@stack_guard @resource_limit(memory=100MB, cpu=80%)
loop:
    input = read_input_with_timeout(1000ms)
    match input:
        Timeout: continue,
        Quit: break,
        Command(cmd):
            if execute_command(cmd).should_exit():
                break
        
// Parallel loop hint for multi-threaded execution
@parallel @chunk_size(1000)
for i in 0..huge_array.length():
    huge_array[i] = complex_transform(huge_array[i])

```
---

## 📋 Phase 6: Module System & Production Features (Weeks 21-24)

### 6.1 Module System & Advanced Optimization
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add import/export functionality with module loading.
- [ ] **NEW**: Implement cross-module optimization using type system knowledge
- [ ] **NEW**: Add SIMD vectorization support for numerical loops
- [ ] **NEW**: Implement advanced loop optimizations (fusion, parallelization)
- [ ] **NEW**: Add comprehensive File I/O system for practical programming tasks

```orus
// math.orus
pub fn sqrt(x: f64) -> f64:
    // Implementation

// main.orus  
use math
use math: sqrt, PI

fn main:
    let result = math.sqrt(25.0)
    print("Square root: ", result)
```

### 6.2 Standard Library Core & Performance Integration
**Priority: 📋 Medium-High**
- [ ] **TODO**: Implement essential standard library modules.
- [ ] **NEW**: Integrate all advanced features (generics, SIMD, optimization) into standard library
- [ ] **NEW**: Add performance-optimized collection implementations
- [ ] **NEW**: Complete File I/O system with error handling and resource management

```orus
// std/io.orus
pub fn print(fmt: string, args: ...any)
pub fn input(prompt: string = "") -> string

// std/collections.orus  
pub struct Vec<T>
pub struct Map<K, V>

// std/result.orus
pub enum Result<T, E>

// std/fs.orus - File I/O and File System Operations
pub struct File
pub enum OpenMode: Read, Write, Append, ReadWrite

// Basic file operations
pub fn open(path: string, mode: OpenMode) -> Result<File, IoError>
pub fn close(file: File) -> Result<(), IoError>
pub fn read(file: File) -> Result<string, IoError>
pub fn read_bytes(file: File, count: usize) -> Result<[u8], IoError>
pub fn write(file: File, content: string) -> Result<(), IoError>
pub fn write_bytes(file: File, bytes: [u8]) -> Result<(), IoError>
pub fn append(file: File, content: string) -> Result<(), IoError>

// Line-based operations
pub fn read_lines(file: File) -> Result<[string], IoError>
pub fn write_lines(file: File, lines: [string]) -> Result<(), IoError>
pub fn read_line(file: File) -> Result<string, IoError>

// File positioning
pub fn seek(file: File, position: i64) -> Result<(), IoError>
pub fn tell(file: File) -> Result<i64, IoError>
pub fn rewind(file: File) -> Result<(), IoError>

// Convenience functions for entire file operations
pub fn read_file(path: string) -> Result<string, IoError>
pub fn write_file(path: string, content: string) -> Result<(), IoError>
pub fn read_file_bytes(path: string) -> Result<[u8], IoError>
pub fn write_file_bytes(path: string, bytes: [u8]) -> Result<(), IoError>

// File system operations
pub fn exists(path: string) -> bool
pub fn is_file(path: string) -> bool
pub fn is_dir(path: string) -> bool
pub fn mkdir(path: string) -> Result<(), IoError>
pub fn mkdir_all(path: string) -> Result<(), IoError>
pub fn rmdir(path: string) -> Result<(), IoError>
pub fn delete(path: string) -> Result<(), IoError>
pub fn copy(src: string, dest: string) -> Result<(), IoError>
pub fn move(src: string, dest: string) -> Result<(), IoError>
pub fn rename(old_path: string, new_path: string) -> Result<(), IoError>

// Directory operations
pub fn list_dir(path: string) -> Result<[string], IoError>
pub fn walk_dir(path: string) -> Result<[string], IoError>
pub fn current_dir() -> Result<string, IoError>
pub fn set_current_dir(path: string) -> Result<(), IoError>

// File metadata
pub struct FileMetadata:
    size: u64
    modified: DateTime
    created: DateTime
    permissions: FilePermissions
    is_file: bool
    is_dir: bool

pub fn metadata(path: string) -> Result<FileMetadata, IoError>
pub fn size(path: string) -> Result<u64, IoError>
pub fn modified_time(path: string) -> Result<DateTime, IoError>
pub fn created_time(path: string) -> Result<DateTime, IoError>

// File permissions
pub struct FilePermissions:
    readable: bool
    writable: bool
    executable: bool

pub fn permissions(path: string) -> Result<FilePermissions, IoError>
pub fn set_permissions(path: string, perms: FilePermissions) -> Result<(), IoError>

// Path operations
pub fn join_path(parts: [string]) -> string
pub fn split_path(path: string) -> [string]
pub fn parent_dir(path: string) -> string?
pub fn filename(path: string) -> string?
pub fn extension(path: string) -> string?
pub fn absolute_path(path: string) -> Result<string, IoError>
pub fn canonical_path(path: string) -> Result<string, IoError>

// Error types
pub enum IoError:
    NotFound(path: string)
    PermissionDenied(path: string)
    AlreadyExists(path: string)
    InvalidInput(message: string)
    UnexpectedEof
    WriteZero
    Interrupted
    Other(message: string)

// Resource management with automatic cleanup
pub fn with_file<T>(path: string, mode: OpenMode, callback: fn(File) -> T) -> Result<T, IoError>
```

---

## 🏗️ Implementation Guidelines

### **Development Best Practices**

**Testing Strategy:**
- Unit tests for each component
- Integration tests for language features  
- Fuzzing for parser robustness
- Benchmark suite for performance regression

**Code Quality Standards:**
- Use clear, descriptive names
- Early return for error handling  
- Consistent naming conventions:
  - snake_case for functions
  - PascalCase for types
  - UPPER_CASE for constants

**Error Handling Philosophy:**
- Rich error reporting with suggestions (see `ERROR_FORMAT_REPORTING.md`)
- Include source location and context
- Provide helpful "Did you mean...?" suggestions
- Show related locations for multi-part errors

---

## 🎯 Success Metrics

### **Performance Targets**
- Beat Python by 10x in compute-heavy benchmarks
- Compete with Lua for scripting performance
- Startup time < 5ms for hello world
- Memory usage < 10MB baseline
- Compilation speed > 100k LOC/second

### **Developer Experience**
- Rust-quality error messages with suggestions
- Sub-second feedback in development
- Rich IDE integration with LSP
- Comprehensive standard library

### **Language Goals**
- Type safety without verbosity
- Performance without complexity
- Expressiveness without magic
- Interoperability without friction

---

## 📅 Development Timeline

### **Quarter 1: Language Core (Weeks 1-12)**
- [x] **Weeks 1-4**: Complete basic types, assignments, booleans ✅
- [x] **Weeks 5-8**: Control flow, scoping, loops ✅
- [x] **Weeks 9-12**: Functions and advanced type system ✅
  -  Function definitions and calls complete
  - ✅ Advanced Hindley-Milner type inference implemented
  - ✅ Arena-based type memory management
  - ✅ Constraint-based solving with comprehensive type checking

### **Quarter 2: Data & Types (Weeks 13-24)**
- [ ] **Weeks 13-16**: Arrays, collections, basic type system, enhanced error reporting
- [ ] **Weeks 17-20**: Pattern matching, structs, enums
- [ ] **Weeks 21-24**: Module system, standard library core

### **Quarter 3: Production Ready (Weeks 25-36)**
- [ ] **Weeks 25-28**: Optimization, advanced GC
- [ ] **Weeks 29-32**: Tooling, LSP, debugger
- [ ] **Weeks 33-36**: Package manager, documentation

---

## 📝 Next Immediate Actions

**Week 1 Priority:**
- [x] Fix string VALUE type conflicts in compiler
- [x] Implement variable assignment (`x = value`) parsing
- [x] Add boolean comparison operators (`==`, `!=`, `<`, `>`)
- [x] Test basic arithmetic with all data types

**Week 2-4 Goals:**
- [x] Complete string concatenation and operations
- [x] Add if/else conditional statements
- [ ] Implement while loops with break/continue (label support pending)
- [ ] Build comprehensive test suite for Phase 1 features
- [ ] Implement TypedExpDesc infrastructure for new compiler

**Critical Missing Features for Full Language:**
- [ ] **Functions & Closures** - Essential for code organization and reusability
- [ ] **File I/O System** - Critical for practical programming tasks
- [ ] **Collections (Arrays/Maps)** - Fundamental data structures
- [ ] **User-Defined Types** - Structs, enums, and custom types
- [ ] **Module System** - Code organization and library support
- [ ] **Advanced Type System** - Generics, inference, and safety
- [ ] **Error Handling** - Result types and exception management
- [ ] **Pattern Matching** - Modern language feature for data handling

This roadmap progresses systematically from basic language features to advanced capabilities, ensuring each phase builds solid foundations for the next. The register-based VM and existing infrastructure provide an excellent platform for rapid feature development.

---

# Orus Typing System Roadmap

## ✅ Official Orus Typing Behavior (as per tutorial)

| Syntax                  | Valid? | Inferred/Parsed As                                  |
| ----------------------- | ------ | --------------------------------------------------- |
| `x = 10`                | ✅      | Inferred as `i32`                                   |
| `y = 3.14`              | ✅      | Inferred as `f64`                                   |
| `x = 10i64`             | ✅      | Explicit suffix → `i64`                             |
| `x: u32 = 10`           | ✅      | Explicit annotation, constant converted if possible |
| `x: i64 = 10u32`        | ❌      | ❌ Implicit coercion not allowed                     |
| `x: i64 = 10u32 as i64` | ✅      | ✅ Requires `as`                                     |

## 🧭 Implementation Roadmap: Default Inference + Explicit Typing

> ✳️ Goal: Orus supports **default type inference from numeric literals** — `x = 10` is valid and becomes `i32`. No need for suffix or annotation unless you want another type.

### ✅ Phase 1: Literal Type Inference (Parser) - COMPLETED

#### 1.1 In `parsePrimaryExpression()`:

| Case                | Result           | Status |
| ------------------- | ---------------- | ------ |
| Unsuffixed integer  | infer `VAL_I32`  | ✅ DONE |
| Unsuffixed float    | infer `VAL_F64`  | ✅ DONE |
| Suffix like `10i64` | assign `VAL_I64` | ✅ DONE |
| `true` / `"text"`   | pretyped         | ✅ DONE |

**Implementation Details:**
- ✅ Default `i32` inference for unsuffixed integers that fit in int32 range
- ✅ Default `f64` inference for unsuffixed floats (contains `.`, `e`, `E`)
- ✅ All suffixes supported: `i32`, `i64`, `u32`, `u64`, `f64`, `u`
- ✅ Boundary handling: integers > INT32_MAX automatically become `i64`
- ✅ Underscore stripping: `1_000_000` → `1000000`
- ✅ Scientific notation: `1.23e5` → `f64`

**Implementation Location:** `src/compiler/parser.c:1314` in `parsePrimaryExpression()`

### ✅ Phase 2: Variable Declarations - COMPLETED

#### 2.1 Accept these forms:

```orus
a = 10        // infer i32
b = 3.14      // infer f64
c = 10u64     // use suffix
d: u32 = 42   // annotation overrides
```

In `parser.c → parseVariableDeclaration()`:

* Store either:
  * `initializer->literal.value.type`
  * or `typeAnnotation` (if exists)

### ✅ Phase 3: Compiler Type Resolution - COMPLETED

In `compiler.c → compileNode`:

```c
if (node->varDecl.typeAnnotation) {
    // Check: is initializer type convertible?
    if (!types_match(inferredType, annotatedType)) {
        error("Cannot assign %s to %s", inferredType, annotatedType);
    }
} else {
    // use literal's inferred type
    varType = node->initializer->literal.value.type;
}
```

### ✅ Phase 4: Binary Operation Rules - COMPLETED

* **Types must match exactly**
* **`as` keyword required** for cross-type operations

```orus
a = 10
b = 2.5
c = a + b        # ❌ error: i32 + f64
c = (a as f64) + b  # ✅
```

### ✅ Phase 5: Casting Rules - COMPLETED

```orus
a = 10
b = a as f64    # OK
c = b as string # OK
```

Only legal way to change type is `as`. All other conversions are errors.

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

## 🔧 Implementation Tasks

| Task                             | Where        | What to do                                       |
| -------------------------------- | ------------ | ------------------------------------------------ |
| Default i32/f64 inference        | `parser.c`   | Already supported — confirm logic                |
| Suffix parsing                   | `parser.c`   | Confirm all numeric suffixes: `i32`, `u32`, etc. |
| Cast-only type conversions       | `compiler.c` | Block cross-type assignment unless `as` is used  |
| Binary op type match enforcement | `compiler.c` | Add checks in `NODE_BINARY`                      |
| Good error messages              | both         | "Try using `as` to convert types"                |

## Status

- [x] Phase 1: Literal Type Inference ✅ COMPLETED
- [x] Phase 2: Variable Declarations ✅ COMPLETED
- [x] Phase 3: Compiler Type Resolution ✅ COMPLETED
- [x] Phase 4: Binary Operation Rules ✅ COMPLETED
- [x] Phase 5: Casting Rules ✅ COMPLETED