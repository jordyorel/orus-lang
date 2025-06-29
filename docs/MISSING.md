# Orus Programming Language - Development Roadmap

## 🎯 Vision & Goals
Build a language that combines Python's readability, Rust's safety, and Lua's performance through a register-based VM with static typing and modern features.

---

## 📊 Current Status Assessment

### ✅ **What's Complete**
- **VM Foundation**: Register-based with 256 registers, 100+ opcodes
- **Lexer System**: Full tokenization with all language constructs  
- **Basic Parser**: Precedence climbing with binary expressions (`1 + 2`)
- **Variable System**: Declarations (`let x = 42`) and lookup
- **Memory Management**: Garbage collector framework integrated
- **Build System**: Clean makefile with benchmarking

### 🔄 **Partially Complete**
- **String Support**: Parsing works, value representation needs fixing
- **Boolean Values**: Parser ready, needs VM integration
- **Error Handling**: Basic framework, needs enhancement

- [x] Variable assignments (`x = value`)
- [ ] Control flow (`if`, `while`, `for`)
- [ ] Functions (`fn name:`)
- [ ] Arrays and collections
- [ ] Type system integration
- [ ] Module system

---

## 📋 Phase 1: Core Language Foundation (Weeks 1-4)

### 1.1 Complete Basic Data Types
**Priority: 🔥 Critical**
- [x] **DONE**: Fix string support and add proper boolean operations to complete the basic type system.

**Implementation Steps:**

- [x] Fix VALUE type conflicts for strings
- [x] Implement string object allocation in compiler
- [x] Add string concatenation operator (`+`)
- [x] Implement string comparison operators
- [ ] Add string interpolation support


### 1.2 Built-in Functions (Print & I/O)
**Priority: 🔥 Critical**
- [x] **DONE**: Basic `print(value)` statement implemented for program output.

```orus
// Basic print function
print("Hello, World!")
print(42)
print(true)

// Print with string interpolation
let name = "Orus"
let version = 1
print("Welcome to {} version {}", name, version)

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
- [ ] Parse `print()` function calls with variable arguments
- [ ] Support printing all basic types (i32, f64, bool, string)
- [ ] Implement string interpolation with `{}` placeholders
- [ ] Handle escape sequences (`\n`, `\t`, `\"`, `\\`)
- [ ] Add `print_no_newline()` variant for precise output control
- [ ] Format numbers and booleans for display

### 1.3 String Interpolation System
**Priority: 🔥 High**
- [ ] **TODO**: Implement full string interpolation with format specifiers and expressions.

```orus
// Basic interpolation
let x = 42
print("The answer is {}", x)

// Multiple placeholders
let a = 10
let b = 20
print("{} + {} = {}", a, b, a + b)

// Format specifiers (future enhancement)
let pi = 3.14159
print("Pi rounded: {:.2}", pi)  // "Pi rounded: 3.14"

// Expression interpolation
let items = [1, 2, 3]
print("Array has {} items", len(items))
```

### 1.4 Variable Assignments
**Priority: 🔥 Critical**
- [x] **DONE**: Basic assignment operations implemented.

**Features to Implement:**
- [x] `x = value` syntax parsing
- [ ] Mutable vs immutable variables (`let mut x = 42`)
- [ ] Compound assignments (`+=`, `-=`, `*=`, `/=`)
- [ ] Type annotations (`let x: i32 = 42`)

### 1.5 Boolean and Comparison Operations
**Priority: 🔥 Critical**
- [x] **DONE**: Added logical operators and comparison operations.

---

## 📋 Phase 2: Control Flow & Scope (Weeks 5-8)

### 2.1 Conditional Statements
**Priority: 🔥 High**
- [ ] **TODO**: Implement if/else statements with proper jump patching and block scoping.

```orus
// Target syntax:
if condition:
    // block
elif other_condition:
    // block  
else:
    // block

// Ternary operator
let result = x > 0 ? "positive" : "non-positive"
```

### 2.2 Loop Constructs
**Priority: 🔥 High**
- [ ] **TODO**: Add while loops and for loops with break/continue support.

```orus
// While loops
while condition:
    print("looping")
    if should_break: break
    if should_skip: continue

// For loops  
for i in 0..5:
    print(i)

for item in collection:
    print(item)
```

### 2.3 Scope and Symbol Tables
**Priority: 🔥 High**
- [ ] **TODO**: Implement proper lexical scoping with nested scope management.

### 2.4 Main Function Entry Point
**Priority: 🔥 Critical**
- [ ] **TODO**: Implement the main function as the program entry point with proper execution flow.

```orus
// Main function - program entry point
fn main:
    print("Hello, Orus!")
    let x = 42
    print("The answer is {}", x)

// Main function with command line arguments (future)
fn main(args: [string]):
    if len(args) > 1:
        print("First argument: {}", args[1])
    else:
        print("No arguments provided")
```

**Implementation Requirements:**
- [ ] Parse `fn main:` or `fn main():` syntax 
- [ ] Generate VM entry point that calls main function
- [ ] Handle main function return values (program exit codes)
- [ ] Proper error handling if main function is missing
- [ ] Support for both parameterless and parameterized main functions

---

## 📋 Phase 3: Functions & First-Class Values (Weeks 9-12)

### 3.1 Function Definition and Calls
**Priority: 🔥 High**
- [ ] **TODO**: Implement function declarations, calls, and return values with proper call frames.

```orus
// Basic function
fn add(a: i32, b: i32) -> i32:
    a + b

fn greet(name: string):
    print("Hello, {}!", name)
```

### 3.2 Closures and Upvalues
**Priority: 📋 Medium**
- [ ] **TODO**: Add support for nested functions and variable capture.

```orus
// Closures (future feature)
fn make_adder(x: i32):
    // Return function that captures x
    // Implementation TBD
```

---

## 📋 Phase 4: Collections & Type System (Weeks 13-16)

### 4.1 Array Implementation
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add dynamic arrays with indexing, slicing, and common operations.

```orus
// Array literals and operations
let nums: [i32; 3] = [1, 2, 3]
let zeros = [0; 5]
let slice = nums[0..2]

let dynamic: [i32] = []
push(dynamic, 42)
pop(dynamic)

for val in nums:
    print(val)

let evens = [x for x in nums if x % 2 == 0]
```

### 4.2 Type System Foundation
**Priority: 📋 Medium**
- [ ] **TODO**: Implement basic static typing with type inference and checking.

```orus
// Type annotations and inference
let x: i32 = 42           // Explicit typing
let y = 42                // Type inference
let name: string = "Orus" // String type
let flag: bool = true     // Boolean type
```

---

## 📋 Phase 5: Advanced Features (Weeks 17-20)

### 5.1 Struct and Enum Types
**Priority: 📋 Medium**
- [ ] **TODO**: Add user-defined types with methods and pattern matching.

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

### 5.3 Generics and Generic Constraints
**Priority: 📋 Medium**
- [ ] **TODO**: Add generic type parameters for functions and structs with constraint system.

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
    let a = identity<i32>(5)
    let b: Box<string> = Box{ value: "hi" }
    let result = add<i32>(10, 20)
```

---

## 📋 Phase 6: Module System & Standard Library (Weeks 21-24)

### 6.1 Module System
**Priority: 📋 Medium-High**
- [ ] **TODO**: Add import/export functionality with module loading.

```orus
// math.orus
pub fn sqrt(x: f64) -> f64:
    // Implementation

// main.orus  
use math
use math: sqrt, PI

fn main:
    let result = math.sqrt(25.0)
    print("Square root: {}", result)
```

### 6.2 Standard Library Core
**Priority: 📋 Medium**
- [ ] **TODO**: Implement essential standard library modules.

```orus
// std/io.orus
pub fn print(fmt: string, args: ...any)
pub fn input(prompt: string = "") -> string

// std/collections.orus  
pub struct Vec<T>
pub struct Map<K, V>

// std/result.orus
pub enum Result<T, E>
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
- Rich error reporting with suggestions
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
- [ ] **Weeks 1-4**: Complete basic types, assignments, booleans
- [ ] **Weeks 5-8**: Control flow, scoping, loops
- [ ] **Weeks 9-12**: Functions, closures, first-class values

### **Quarter 2: Data & Types (Weeks 13-24)**
- [ ] **Weeks 13-16**: Arrays, collections, basic type system
- [ ] **Weeks 17-20**: Pattern matching, structs, enums
- [ ] **Weeks 21-24**: Module system, standard library core

### **Quarter 3: Production Ready (Weeks 25-36)**
- [ ] **Weeks 25-28**: Optimization, advanced GC
- [ ] **Weeks 29-32**: Tooling, LSP, debugger
- [ ] **Weeks 33-36**: Package manager, documentation

---

## 📝 Next Immediate Actions

**Week 1 Priority:**
- [ ] Fix string VALUE type conflicts in compiler
- [ ] Implement variable assignment (`x = value`) parsing
- [ ] Add boolean comparison operators (`==`, `!=`, `<`, `>`)
- [ ] Test basic arithmetic with all data types

**Week 2-4 Goals:**
- [ ] Complete string concatenation and operations
- [ ] Add if/else conditional statements
- [ ] Implement while loops with break/continue
- [ ] Build comprehensive test suite for Phase 1 features

This roadmap progresses systematically from basic language features to advanced capabilities, ensuring each phase builds solid foundations for the next. The register-based VM and existing infrastructure provide an excellent platform for rapid feature development.