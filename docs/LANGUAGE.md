# Orus Language Guide

Orus is an experimental interpreted language influenced by modern scripting languages and Rust-like syntax. This guide covers the features available in version 0.2.0 and serves both as a tutorial and reference. All examples come from the `tests/` directory.

For a step-by-step walkthrough from beginner to advanced topics see
`docs/TUTORIAL.md`. Additional snippets are collected in
`docs/EXAMPLE_SNIPPETS.md`.

## Getting Started

A simple program prints text using the built-in `print` function:

```orus
fn main() {
    print("Hello, Orus!")
}
```

The interpreter looks for a `main` function in the entry file. Exactly one such function must exist across the project.

## Variables and Mutability

Variables are declared with `let`.

```orus
let number: i32 = 5     // immutable
let mut count = 0       // mutable, type inferred as i32
```

- **Immutability** is the default. Reassigning an immutable binding is a compile-time error.
- Use `let mut` to allow reassignment. The type of a binding cannot change after it is set.
- Variables are block scoped and must be declared inside functions.

```orus
fn demo() {
    let mut value = 1
    value = 2       // ✅ allowed
    // value = 3.0  // ❌ type mismatch
}
```

## Constants

Compile-time constants are declared at the top level using `const` and may be
marked `pub` for use in other modules. By convention constant names are written
in uppercase.

```orus
pub const LIMIT: i32 = 10

fn main() {
    for i in 0..LIMIT {
        print(i)
    }
}
```

## Static Variables

Use `static` to define global variables. Add `mut` to make them mutable. These declarations must appear at the top level.

```orus
static mut COUNTER: u64 = 0u

fn increment() {
    COUNTER = COUNTER + 1u
}
```

## Primitive Types

- `i32` – 32‑bit signed integer
- `i64` – 64‑bit signed integer
- `u32` – 32‑bit unsigned integer
- `u64` – 64‑bit unsigned integer
- `f64` – double precision floating point
- `bool` – boolean (`true` or `false`)
- `string` – UTF‑8 text
- `void` – absence of a return value
- `nil` – explicit nil literal

Integer literals default to `i32` when the value fits in 32 bits. Larger
values up to the 64‑bit signed limit become `i64` and anything beyond
that uses `u64`. Append a trailing `u` or an explicit suffix such as
`i32`, `i64`, `u32`, `u64` or `f64` to choose the exact type.

```orus
let flag: bool = true
let text = "hello"       // type inference
```

Numeric types never convert implicitly except when calling a function. When a
value is passed to a function or builtin, it will automatically be converted to
the parameter's numeric type if possible. In all other contexts use `as` to
cast explicitly:

```orus
let a: i32 = -5
let b: u32 = a as u32
let big: u64 = a as u64
let c: i32 = big as i32
```
`u64` values can be cast to `i32`, `u32` or `f64` and vice versa using `as`.

### Casting rules

* Casting between integers of different widths truncates on overflow.
* Integers and floats can be converted back and forth. Floating point casts to
  integers round toward zero and may lose precision.
* Any numeric type may be cast to `bool`. Zero becomes `false`; any other value
  becomes `true`. Booleans cast back to numbers use `1` for `true` and `0` for
  `false`.
* All primitive values can be converted to `string` using `as string`.
* Casting from `string`, `nil` or `void` to numeric types is not allowed.

### Numeric literals

Integer literals are written in decimal by default. Use `0x` to specify a
hexadecimal value. Underscores may separate digits for readability. A
suffix like `i32`, `i64`, `u32`, `u64`, `f64` or just `u` chooses the
literal's type explicitly.

```orus
let dec = 42
let hex = 0x2A
let big = 1_000_000u
```

## Comments

`//` starts a line comment. Block comments use `/* ... */`.

```orus
// single line
let x = 1 /* inline */ + 2

/*
This is a block comment.
*/
```

## Operators

Orus supports common arithmetic (`+`, `-`, `*`, `/`, `%`), comparison (`==`, `!=`, `<`, `>`, `<=`, `>=`), and logical operators (`and`, `or`, `not`). Compound assignments like `+=` and `-=` are also available.

Bitwise operators work on integers: `&`, `|`, `^`, `!`, `<<`, and `>>`. The shift right operator performs arithmetic shifting for signed types and logical shifting for `u32`. Shift amount values may be any integer type and are automatically converted to match the left operand, which determines the result type.

Casting between numeric types must be explicit with `as`.

## Control Flow

### Conditionals

```orus
if n > 0 {
    print("positive")
} elif n == 0 {
    print("zero")
} else {
    print("negative")
}
```

An inline form `condition ? expr1 : expr2` evaluates to `expr1` when the
condition is true, otherwise `expr2`.

### Loops

```orus
for i in 0..5 {          // 0 to 4
    print(i)
}

while condition {
    // repeat while true
}

break      // exit loop
continue   // next iteration
```

## Arrays

Fixed-length arrays use `[T; N]` syntax. Elements are zero indexed.

```orus
let nums: [i32; 3] = [1, 2, 3]
let first = nums[0]
nums[1] = 20
```

### Dynamic Arrays

Built-in functions can grow arrays dynamically.

```orus
let values: [i32] = []
push(values, 10)
push(values, 20)
print(len(values))  // 2
```

### Array Fill Expressions

Use `[value; N]` to create an array filled with the same value. The length `N`
must be known at compile time.

```orus
let zeros = [0; 30]       // [i32; 30]
const N = 4
let fixed: [i32; N] = [0; N]

fn make(n: i32) {
    let bad = [0; n]  // compile-time error
}
```

### Length Inference

When the length annotation is omitted, the compiler infers it from the number of
elements. These arrays are fixed-size and do not support `push` or `pop`.

```orus
let nums = [1, 2, 3]  // type is [i32; 3]
```

### Slicing

Subarrays are created with `[start..end]` (end exclusive). The start or end may
be omitted to slice from the beginning or to the end of the array.

```orus
let part = nums[0..2]  // first to 3rd element
let part = nums[..2]   // first to 3rd element
let part = nums[0..]   // first to last element
let part = nums[..]    // entire array
```

## Structs

Structs group named fields.

```orus
struct Point {
    x: i32,
    y: i32,
}

let p = Point{ x: 1, y: 2 }
print(p.x)
```

## Methods with `impl`

Methods attach functions to a struct inside an `impl` block. This style is similar to Rust.

```orus
impl Point {
    fn new(x: i32, y: i32) -> Point {
        return Point{ x: x, y: y }
    }

    fn move_by(self, dx: i32, dy: i32) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

Use the struct name for static methods and a value for instance methods:

```orus
let p = Point.new(1, 2)
p.move_by(3, 4)
```

## Functions

Functions are defined with `fn`. Parameter types are required and the return type follows `->`.

```orus
fn add(a: i32, b: i32) -> i32 {
    return a + b
}

fn greet(name: string) {    // no return value
    print("Hello, {}!", name)
}
```

Functions may be declared after their call site. Generic functions can also be referenced before their definitions thanks to a prepass that records all generic signatures.

## Pattern Matching

`match` compares a value against patterns, similar to `switch` in other languages but with explicit patterns like Rust.

```orus
match value {
    0 => print("zero"),
    1 => print("one"),
    _ => print("other"),
}
```

The first matching branch runs. Use `_` as a wildcard.

## Error Handling

`try`/`catch` blocks handle runtime errors.

```orus
try {
    let x = 10 / 0
} catch err {
    print("Error: {}", err)
}
```

Error messages include the file, line and column as well as a short stack trace.

## Generics

Functions and structs may take type parameters using angle brackets.

```orus
fn id<T>(x: T) -> T {
    return x
}

struct Box<T> { value: T }
```

Type arguments can often be inferred:

```orus
let a = id<i32>(5)
let b: Box<string> = Box { value: "hi" }
```

When using nested generics, place a space between the closing angle brackets so
the scanner doesn't interpret them as the `>>` shift-right operator. For
example:

```orus
let nested: Box<Box<i32> > = Box { value: Box { value: 1 } }
```

Generic functions can be referenced before their definitions thanks to a prepass that records all function signatures. Generics may specify constraints such as `T: Numeric` or `T: Comparable` to enable arithmetic and comparison operations.

`Numeric` parameters permit arithmetic, bitwise and unary minus operators while
`Comparable` enables comparison and equality checks. A numeric constraint also
implies comparability.

Generics work across modules through a prepass that specializes functions and
structs before execution. Type inference handles nested generics and supports
numeric operators when the `Numeric` constraint is used. Standard collections
like `Map` and `Set` are implemented generically in the `std` library.

### Generic Functions

Declare type parameters in angle brackets after the function name. The type can then be used in parameters and the return type.

```orus
fn identity<T>(value: T) -> T {
    return value
}
```

The type argument can often be inferred:

```orus
let x = identity<i32>(5)
let y = identity("hello") // `T` inferred as `string`
```

Functions defined later in the file can still be called thanks to the prepass that records generic signatures:

```orus
fn main() {
    print(firstElement([1, 2, 3]))
}

fn firstElement<T>(arr: [T]) -> T {
    return arr[0]
}
```

### Generic Structs

Structs also support type parameters:

```orus
struct Box<T> { value: T }
```

Instances specify the concrete type:

```orus
let intBox = Box<i32>{ value: 42 }
let strBox = Box<string>{ value: "hi" }
```

Nested generics require a space so `>>` is not misread as the shift operator:

```orus
let nested: Box<Box<i32> > = Box { value: intBox }
```

### Constraints

Type parameters may declare constraints using a colon. `Numeric` enables arithmetic and bitwise operators while `Comparable` allows comparison and equality.

```orus
fn add<T: Numeric>(a: T, b: T) -> T { return a + b }
fn min<T: Comparable>(a: T, b: T) -> T { if a < b { return a } else { return b } }
```

Numeric types automatically satisfy `Comparable`.

### Cross-Module Generics

Generic functions and structs can be defined in separate modules. The prepass ensures specializations are generated before execution.

```orus
// util.orus
pub fn identity<T>(val: T) -> T { return val }
```

```orus
// main.orus
use util

fn main() {
    print(util.identity<i32>(42))
}
```

### Collections

Standard collections like `Map` and `Set` are implemented generically in `std/collections`. Higher-order helpers such as `map`, `filter` and `reduce` work with any element type.

### Further Reading

See `tests/generics/` for complete examples. The file
`docs/ADVANCED_GENERICS_TUTORIAL.md` explores more complex patterns and
best practices for generic code.

## Modules

Code can be split into multiple files. Use `use` to load an entire module. Only whole modules may be imported; individual items cannot be pulled in directly. After importing, reference functions or types through the module name.

The import system design is described further in `docs/import.md`.

```orus
use math.utils
use datetime as dt

fn main() {
    utils.helper()
    dt.now()
}
```

Modules are executed once. Importing the same file twice causes a runtime error. Each module may define structs, functions and `impl` blocks. Only the entry file needs a `main` function.

An alias can shorten the module name:

```orus
use math.utils as mu
mu.helper()
```

### Public functions

Use the `pub` keyword before a top-level function to export it from a module.

```orus
// utils.orus
pub fn helper() {
    print("from helper")
}

// main.orus
use utils

fn main() {
    utils.helper()
}
```

Struct fields and methods cannot yet be declared `pub`.

### Public structs

Use `pub struct` to export a type from a module.

```orus
pub struct Point { x: i32, y: i32 }
```

### Additional Import Features

The import system also supports a few conveniences:

- `use *` brings all public members of a module into scope.
- `module_name()` and `module_path()` return the current module's name and path.
- Run the interpreter with `--trace-imports` to log module loading.

## Built-in Functions

Common utilities are always available. See `docs/BUILTINS.md` for full details.

- `print(values...)`
- `len(value)`
- `substring(str, start, len)`
- `push(array, value)` / `pop(array)` / `reserve(array, capacity)`
- `range(start, end)`
- `sum(array)`, `min(array)`, `max(array)`
- `type_of(value)`, `is_type(value, name)`
- `input(prompt)`, `int(text)`, `float(text)`
- `timestamp()`
- `sorted(array, key=nil, reverse)`

```orus
let arr: [i32] = [1]
reserve(arr, 10) // preallocate capacity
push(arr, 2)
print(len(arr))
```
`push`, `pop` and `reserve` compile to specialized opcodes when the
array type is statically known, avoiding the overhead of a function call.

## Best Practices and Patterns

This section collects recommended patterns when writing Orus code.

### Module Organization

Group related functions and structs into modules. Use `pub` to expose
only necessary definitions and keep implementation details private.

### Naming Conventions

- Functions and variables use `snake_case`.
- Struct names use `CamelCase`.
- Constants are written in uppercase with underscores.

### Immutability First

Favor immutable bindings and avoid `let mut` unless mutation is
necessary. This reduces accidental state changes and eases reasoning
about code.

### Error Handling

Use `try`/`catch` blocks around operations that may fail. Return
`nil` or custom error structs to propagate failure states.

### Testing

The examples in `tests/` demonstrate how small programs can act as
regression tests. Add new scenarios when fixing bugs or adding
features.

### Debugging

Compile the interpreter with `DEBUG=1` for extra runtime checks and verbose
logging. Insert `print` statements or use `type_of()` to inspect values during
execution. Detailed error messages with file and line information are listed in
`docs/ERROR_REFERENCE.md`. See `docs/DEBUGGING_GUIDE.md` for additional
troubleshooting techniques.

## Feature Status

- Modules, pattern matching, error handling and `impl` blocks are **fully implemented**.
- Generics now support forward declarations, constraints, cross-module specialization and improved inference.
- The standard library is minimal; more built-ins are planned.
- Initial modules are available under `std/` such as `std/math` for math helpers.
- A basic `std/random` module provides pseudo random numbers; see
  `docs/RANDOM_MODULE_LIMITATIONS.md` for details.

Development milestones are tracked in `docs/ORUS_ROADMAP.md` and
`docs/COMPILATION_ROADMAP.md`.

