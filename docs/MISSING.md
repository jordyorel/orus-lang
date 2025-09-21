# Orus Programming Language – Development Roadmap

_Last reviewed: v0.3.0 (September 2025)_

This roadmap tracks the gap between the current Orus implementation and the
next set of language and tooling goals. It consolidates the real state of the
multi-pass typed-AST compiler pipeline, runtime, and diagnostics so new
contributors can immediately see what is finished, what is underway, and what
still needs design work.

---

## 📌 Current Snapshot
- ✅ **End-to-end toolchain** – Source code flows through the lexer, parser,
  Hindley–Milner type inference, the multi-pass typed-AST compiler (optimization
  followed by bytecode generation), and the 256-register VM via `interpret()`
  and `compileProgram()`.
- ✅ **Core language surface** – Functions, variables (`mut` + inference),
  arithmetic/logical expressions, conditionals, and `while`/`for` loops execute
  reliably (see `tests/comprehensive/comprehensive_language_test.orus`).
- ✅ **Data model** – Strings, booleans, integers (`i32/i64/u32/u64`), doubles,
  and heap-backed arrays (push/pop/slicing/iteration) are fully wired through
  the type system and VM runtime.
- ✅ **Polymorphism** – Hindley–Milner generalization works for reusable
  functions (e.g., `tests/types/polymorphic_identity.orus`), though first-class
  generic syntax (`fn foo<T>`) is not yet exposed.
- ⚠️ **Diagnostics** – Loop/context validation and scope tracking ship, but the
  planned rich Rust-style error formatter and variable lifecycle diagnostics are
  still pending (`docs/OUTSTANDING_COMPILER_AND_DIAGNOSTICS.md`).
- ✅ **Module system** – Source files can declare `module` headers (with optional
  block bodies), and dotted `use` paths resolve to nested module packages loaded
  through the runtime module manager.
- ✅ **Module visibility** – Parser enforces uppercase `global` declarations,
  tracks `pub` exports for globals, functions, structs, and enums, and the
  runtime registers those exports with the module loader. Modules can use
  public globals, functions, and type declarations from sibling files via
  `use`, including renaming support for structs and enums.
- ✅ **Pattern matching** – `match` supports enums and literal values using the
  unified `pattern ->` arm syntax. Lowering reuses scoped temporaries and
  chained `if` statements, preserving payload destructuring, `_` wildcards, and
  exhaustiveness diagnostics for enums. Expression-form matches now evaluate to
  values, reusing a shared scrutinee binding and unifying branch result types so
  match expressions can flow into surrounding computations.

---

## ✅ Completed Milestones

### Frontend & Type System
- Full tokenization and indentation-aware parsing for the current language
  syntax, covering functions, control flow, and expressions.
- Hindley–Milner inference with constraint solving, explicit annotations, casts,
  numeric literals with suffixes, and polymorphic generalization.
- Typed AST visualization tooling (`--show-typed-ast`) to inspect inference
  results during development.

```orus
fn identity(value):
    return value

print(identity(123))
print(identity(true))
print(identity("orus"))
```

### Compiler & Optimization Pipeline
- `compileProgram()` drives typed AST generation, register allocation, constant
  propagation, loop-invariant hoisting, and bytecode emission into VM chunks.
- Peephole optimizer deduplicates redundant loads and boolean immediates.
- Loop-invariant code motion hoists literal-backed locals out of loops and
  reruns constant folding on the promoted declarations to unlock more
  simplification opportunities.
- Jump patching infrastructure and source-location metadata power reliable
  control-flow codegen and diagnostics.
- CI fixtures cover arithmetic, control flow, functions, arrays, strings, and
  polymorphic type behaviour across `tests/`.

### Runtime & Tooling
- 256-register VM with dedicated banks for globals, frames, temporaries, and
  future module uses; dispatch tables ship in both `goto` and `switch`
  variants.
- Built-in printing, string interpolation, numeric formatting, and array
  helpers are available in the runtime (`src/vm/runtime/builtins.c`).
- CLI, REPL, and configuration layers share the same compiler/runtime path and
  expose toggles for bytecode dumps, typed AST, and profiling.

```orus
numbers = [1, 2, 3, 4]
mut total = 0

for n in 0..len(numbers):
    total = total + numbers[n]

print("sum:", total)
```

---

## 🚧 Active Work (Q4 2025)
| Area | Status | Notes |
| --- | --- | --- |
| Rich error presentation | In progress | Implement the structured renderer from `docs/ERROR_FORMAT_REPORTING.md` across CLI/REPL. |
| Variable lifecycle diagnostics | In progress | Use scope metadata to flag duplicate declarations, use-before-init, and const mutations. |
| Iterator-style `for item in collection` | Design | Parser/codegen support pending; VM array iterators are ready. |
| Typed loop fast paths & LICM safety fences | In progress | Phase 0 telemetry landed (`vm_trace_loop_event`, loop counter dumps); next steps: bool-branch cache, overflow-checked increments, and LICM effect guards while keeping boxed fallbacks available. |
| Module packaging | Completed | Module declarations and dotted `use` paths map to nested files and are covered by regression tests. |
| Print formatting polish | Backlog | Finish escape handling and numeric formatting for the print APIs. |
| Module use resolution | Completed | `use` loads sibling modules and binds their exported globals, functions, structs, and enums—including aliased type declarations—via the module loader. |

---

## 🎯 Near-Term Targets
1. Ship the structured diagnostic renderer with regression snapshots.
2. Complete variable lifecycle analysis and wire the results into the feature
   error reporters.
3. Extend the parser/typechecker/codegen to support iterator-style `for` loops
   using array iterators.
4. ✅ Define and implement module declarations/`use` syntax backed by the
   existing loader.
5. Document and stabilize the printing APIs once formatting is finalized.

---

## 🔭 Deferred & Future Initiatives
- **First-class generics** – Parse `<T>` parameters, monomorphize function and
  struct definitions, and enforce trait/constraint checking.

  ```orus
  // Generic functions
  fn identity[T](value: T) -> T:
      value

  // Generic structs
  struct Box[T]:
      value: T

  // Generic constraints
  fn add[T: Numeric](a: T, b: T) -> T:
      a + b

  fn min[T: Comparable](a: T, b: T) -> T:
      a if a < b else b

  fn main():
      count = identity[i32](5)
      greeting: Box[str] = Box{ value: "hi" }
      total = add[i32](10, 20)
      print(count, greeting.value, total)
  ```
- **Standard library** – Curate a minimal module set (math, arrays, strings)
  once the module system is stable.
- **Advanced optimizations** – ✅ Loop-invariant code motion shipped; loop
  unrolling and strength reduction passes remain once diagnostics are solid.
- **Performance telemetry** – Expand VM profiling to collect per-opcode
  statistics and surface them in tooling.

### Planned Language Constructs
Each of the following surface features is scoped for post-v0.3.0 delivery once
module packaging lands. The intent is to slot them into the existing
multi-pass typed-AST compiler pipeline—leveraging the typed AST produced by
Hindley–Milner inference, the optimization pass, and the bytecode generation
pass—without introducing additional intermediate representations.

1. **Struct definitions** – Extend the parser with record literals, allocate
   contiguous register frames for fields, and integrate field access in the
   type checker.
   - ✅ Parser, typed AST, and Hindley–Milner inference now understand struct
     literals, dot-based field access, and field assignments (including nested
     member chains).
   - 🚧 Bytecode emission for struct construction and field loads/stores is
     still pending in the backend.

   ```orus
   struct Point:
       x: i32
       y: i32

   fn origin():
       return Point{ x: 0, y: 0 }

   fn shift(p: Point, dx: i32, dy: i32):
       Point{ x: p.x + dx, y: p.y + dy }
   ```

2. **Enum + tagged unions** – Model discriminated unions via tagged payloads in
   the VM, add pattern exhaustiveness checks, and reuse Hindley–Milner type
   inference to propagate variant data.

   - ✅ Parser and typed AST generation understand enum declarations, including
     tuple-style variant payloads.
   - ✅ Hindley–Milner inference registers enum variants in the global type
     registry and materializes `TYPE_ENUM` metadata for downstream compiler
     phases.
   - ✅ Enum metadata now preserves variant payload field names, enabling
     upcoming pattern matching exhaustiveness and destructuring analysis.
   - ✅ Wire up the standard `Result.Ok`/`Result.Err` constructors so they
     allocate concrete tagged values once bytecode lowering is available.
   - ✅ Lower enum constructors in the bytecode backend (variant pattern tests
     remain to be implemented).
   - ✅ Added `matches` syntax sugar so payload-free enum variants can be
     compared ergonomically while keeping parity with equality semantics.
   - ⚠️ Author regression tests that exercise lowering through variant
     patterns once pattern matching lands, ensuring constructor payloads round
     trip correctly. Target scenarios include tuple-style payload capture,
     nested enum payload propagation, and mixtures of literal plus wildcard
     arms so we can verify both the codegen paths and the exhaustiveness
     diagnostics.
   - ⚠️ Full enum ergonomics are blocked on the pending `match` work below—until
     destructuring and exhaustiveness checks ship, enum pattern matching
     remains incomplete. The implementation plan couples the matcher to the
     Hindley–Milner exhaustiveness checker so variant tags and payload arity
     data flow through to bytecode lowering without resorting to ad-hoc tag
     tables.


   ```orus
   enum Result[T]:
       Ok(value: T)
       Err(message: str)

   fn parse_number(text: str): Result[i32]:
       if text == "":
           return Result.Err("empty input")
       Result.Ok(to_i32(text))
   ```

3. **`impl` blocks and methods** – Allow attaching functions to struct/enum
   types, synthesize method receivers as the first parameter, and support
   namespaced lookup during codegen.

   - ✅ Parser, typed AST, and Hindley–Milner registration hook struct
     declarations into the global type registry and attach impl methods to the
     struct metadata.
   - ✅ Type inference now marks implicit receivers after resolving the callee,
     so both namespaced (`Point.new`) and instance (`p.move_by`) method calls
     wire the hidden `self` argument automatically.
   - ✅ Codegen emits struct literals and lowers field loads/stores by
     reusing the array allocation opcodes, so instance methods can mutate
     `self`'s backing storage.
   - ✅ Codegen now dispatches struct methods, injecting the implicit `self`
     register for instance receivers while honoring namespaced method symbols.

   ```orus
   impl Point:
       fn magnitude(self):
           sqrt(self.x * self.x + self.y * self.y)

   fn demo():
       p = Point{ x: 3, y: 4 }
       print(p.magnitude())
   ```

4. **Pattern matching** – Statement and expression forms share the single-pass
   lowering strategy while diagnostics for literal arms continue to expand.

  ```orus
  match parse_number(input):
      Result.Ok(value) -> print("parsed", value)
      Result.Err(msg) -> print("failed:", msg)
  ```

   - ✅ Statement-oriented matches support enums, literals, and wildcards with
     scoped temporaries plus exhaustiveness checks for enum variants.
   - ✅ Payload destructuring for enum arms automatically inserts bindings into
     the branch scope before executing the arm body.
  - ✅ Completed: Allow `match` to be used as an expression that evaluates to a
    value while unifying branch types and reporting duplicate literal arms.
    Recent work delivered:
    - ✅ Parser/lowering updates so the scrutinee is evaluated once and branch
          bodies assign into a synthesized result slot without introducing
          extra IR.
    - ✅ Type inference enhancements that unify branch result types, surface
          duplicate literal arms, and continue honouring enum exhaustiveness
          guarantees.
    - ✅ Backend support that threads the synthesized result register through
          the chained `if` lowering and exposes the expression value to callers.
    - ✅ Regression tests covering successful expression matches, duplicate
          literal detection, and mixes of enum and literal fallbacks.
    - 🔭 Backlog: Evaluate guard clauses and `pattern1 | pattern2` sugar once the
          ergonomics and diagnostic strategy are finalized.

     ```orus
     match value:
         0 -> print("zero")
         1 -> print("one")
         _ -> print("other")
     ```

5. **Module system** – Finalize module declarations, `use` statements, and
   namespace resolution so compiled bytecode can map to the existing module
   loader API.

   ```orus
   # geometry/points.orus
   pub struct Point:
       x: i32
       y: i32

   pub fn distance(a: Point, b: Point):
       dx = a.x - b.x
       dy = a.y - b.y
       sqrt(dx * dx + dy * dy)

   # main.orus
   use geometry.points: Point, distance

   fn main():
       origin = Point{ x: 0, y: 0 }
       target = Point{ x: 3, y: 4 }
       print(distance(origin, target))
   ```

---

## ✅ Test Coverage Overview
- `tests/comprehensive/` – CLI smoke coverage for the core language surface.
- `tests/functions/`, `tests/control_flow/`, `tests/variables/` – Focused suites
  for semantics, scoping, and register pressure.
- `tests/types/` – Positive and negative fixtures validating inference,
  polymorphism, and casting behaviour.
- `tests/error_reporting/` – Ensures current diagnostics (loop misuse, syntax
  failures) surface correct locations and messages.

Keep this roadmap in sync whenever functionality lands or priorities change so
contributors can quickly align on the next set of deliverables.
=======
## 📋 Phase 4: Collections & Advanced Type Features (Weeks 13-16)

### 4.1 Basic Array Implementation & Generic Type Preparation
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add dynamic arrays with indexing, slicing, and common operations.
    - [x] `array[index]` expressions compile to bounds-checked reads.
    - [x] `array[index] = value` assignments emit bounds-checked stores.
    - [x] Iteration over arrays using `for value in array:`.
    - [x] Array slicing (`array[start..end]`).
    - [ ] Array comprehensions and higher-order operations (`[x for x in array]`).
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
- [x] **Pattern matching types**: Exhaustiveness checking for enum variants
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
        Ok(value) ->
            return value
        Error(err) ->
            panic("Error: ", err)

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
- ✅ Implement match expressions with destructuring patterns and runtime payload
  extraction backed by dedicated bytecode instructions.

```orus
// Match expressions
match value:
    0 -> print("zero")
    1 -> print("one")
    _ -> print("other")
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
- ✅ Range loop comparisons now emit typed opcodes to bypass boxed `Value` checks in tight counters.
- ✅ Range loop increments use typed addition to avoid the `OP_ADD_I32_R` string-concatenation slow path.
- [ ] Iterator-based `for` loops must eliminate per-iteration allocator churn by keeping iteration state in typed registers.

 
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
- [ ] **TODO**: Add use/export functionality with module loading.
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

