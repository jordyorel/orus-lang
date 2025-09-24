# Complete Orus Tutorial (workspace snapshot)

This tutorial walks through every feature that the current Orus compiler and VM implement. Each section builds on the previous one so you can see the language in action while keeping close to the behaviour encoded in the source.

## 1. Orientation
- Source files are newline-oriented. Semicolons are rejected (`E1007`).
- A colon (`:`) introduces an indented block. Tabs count as four spaces; mixing widths triggers "Inconsistent indentation" errors.
- Comments use `//` for single lines and `/* ... */` for block ranges (block comments do not nest).

```orus
// Single line comment
/* Block comment
   over multiple lines */
print("Hello")
```

## 2. Hello, Orus!

```orus
print("Hello, Orus!")
print_no_newline("Streaming")
print("A | B | C")
```

- `print` adds a trailing newline and separates arguments with a space.
- `print_no_newline` writes the arguments and keeps the cursor on the same line.
- Format specifiers inside the first string literal (`@.2f`, `@x`, `@b`, `@o`) format the next argument: `print("pi = @.2f", 3.14159)`.
  Build custom separators by constructing the string yourself or joining values before printing.

## 3. Values and Types
- Integers default to `i32`; larger values become `i64`. Use underscores for readability and `0x` for hexadecimal.
- Floating literals are `f64`.
- Booleans are `true` and `false`.
- Strings support `\n`, `\t`, `\\`, `\"`, `\r`, and `\0` escapes.
- Type annotations follow the name: `total: i64 = 0`.

```orus
count = 42
big: i64 = 5_000_000_000
mask = 0xFF
ratio = 6.022e23
flag = true
message = "Line one\nLine two"
```

## 4. Variables and Mutability
- Bindings are immutable by default.
- Prepend `mut` to allow reassignment.
- Multiple declarations may share a line when separated by commas.
- Names must start with a letter or `_` and contain only letters, digits, or `_`.
- Compound assignments `+=`, `-=`, `*=`, `/=`, `%=` operate on existing bindings.

```orus
score = 10
mut retries = 0
threshold: f64 = 0.75
x = 1, mut y = 2, label: string = "ready"

mut retries += 1
```

### Globals and Exports
- `global` is only permitted at module scope, requires an initializer, and the name must be uppercase. Use `mut` for writable globals.
- `pub` exports `fn`, `global`, `struct`, `enum`, or `impl` declarations from the module; it is illegal inside nested blocks.

```orus
pub global MAX_CONNECTIONS = 512
pub global mut CACHE_BYTES = 1_048_576
```

## 5. Strings and Printing

```orus
name = "Orus"
pi = 3.14159
print("Hello", name)
print("Pi ~= @.2f", pi)
print_no_newline("Processing")
combined = "a" + " :: " + "b" + " :: " + "c"
print(combined)
```

Treat strings like any value: concatenate with `+` or convert numbers using casts (`value as string`).

## 6. Branching and Loops

### Conditional Logic

```orus
if status == "ok":
    print("ready")
elif status == "retry": print("waiting")
else:
    print("failed")
```

- Conditions must evaluate to `bool`. Using `=` inside a condition raises a syntax error.

### While Loops

```orus
mut attempts = 0
while attempts < 3:
    attempts += 1
    print("attempt", attempts)
```

### For Loops
- Range form: `start..end` is exclusive. Use `start..=end` for inclusive ranges.
- Provide a second `..step` expression to control the step size.
- Iterable form: `for item in array` walks an array.
- Loops support labels written as `'label:` before the keyword. `break`/`continue` accept an optional label target.

```orus
for i in 0..10..2:
    print(i)

values = [1, 2, 3]
for item in values:
    print("item", item)

'outer: for row in 0..5: // not implemented
    for col in 0..5:
        if row == col:
            break 'outer
```

## 7. Arrays in Practice
- Literal syntax: `[]` or `[expr, expr, ...]`. Trailing commas are fine.
- `len(array)` returns an `i32` length.
- `push(array, value)` appends and returns the array.
- `pop(array)` removes and returns the last element.
- Slices use `array[start..end]` with an inclusive end index. Passing the array length (or omitting the bound) includes the
  final element.

```orus
mut numbers = []
push(numbers, 1)
push(numbers, 2)
push(numbers, 3)
print("len", len(numbers))
print("pop", pop(numbers))
print("slice", numbers[0..1])
```

## 8. Functions and Higher-Order Code
- Define functions with `fn name(params) -> ReturnType:` and a block body. Omit the arrow for `void` functions.
- Use `return` to exit early; omit the expression to return `void`.
- Function expressions (`fn (...) -> ...:`) create first-class functions.

```orus
fn add(a: i32, b: i32) -> i32:
    return a + b

square = fn(value: i32) -> i32:
    return value * value

print(add(5, 7))
print(square(6))
```

Function values capture surrounding variables automatically.

## 9. Structs and Methods
- Structs declare named fields. Provide default expressions where needed.

```orus
struct Point:
    x: i32
    y: i32 = 0
```

- Construct values with `{ field: value }` and assign fields with dot notation.

```orus
mut origin = Point{ x: 0, y: 0 }
origin.y = 5
```

- `impl` blocks attach methods. Methods whose first parameter is `self` become instance methods. Others act as static helpers invoked through the struct name.

```orus
impl Point:
    fn translate(self, dx: i32, dy: i32) -> Point:
        return Point{ x: self.x + dx, y: self.y + dy }

    fn from_origin(dx: i32, dy: i32) -> Point:
        return Point{ x: dx, y: dy }

p = Point.from_origin(4, 9)
print(p.translate(1, -1).y)
```

## 10. Enums and Pattern Matching
- Enums list variants with optional payloads. Payloads may have names (`Variant(name: Type)`).

```orus
enum Result:
    Ok(value: i32)
    Err(message: string)
```

### Constructors

```orus
success = Result.Ok(42)
failure = Result.Err("boom")
```

### Match Statement

```orus
match success:
    Result.Ok(value) ->
        print("ok", value)
    Result.Err(reason) ->
        print("error", reason)
```

- Arms use `pattern ->` followed by either a single statement or an indented block.
- `_` matches anything. Named bindings capture payload fields and are available in the arm.
- Exhaustiveness is enforced for enums; add a wildcard arm to cover the remainder.

### Match Expression

```orus
label: string = match failure:
    Result.Ok(value) -> "ok: " + (value as string)
    Result.Err(reason) -> "error: " + reason
```

### `matches` Operator

```orus
flag = Flag.Off
if flag matches Flag.On:
    print("enabled")
```

The `matches` keyword provides a readable equality check—code generation treats it like `==`.

## 11. Error Handling
- `try:` introduces protected code. `catch name:` (or `catch:`) handles thrown values. Both accept either a single statement on the same line or an indented block.
- `throw expression` raises an error value.

```orus
try:
    risky = 10 / step
catch err:
    print("caught", err)

throw "unexpected state"
```

## 12. Modules and Code Organisation
- A file may begin with `module path` or `module path:`. The declaration must be the first non-comment statement and may appear only once.
  - The block form (`module pkg.stats:`) requires the entire file to reside inside the indented block.
  - Dotted names map to directories (`geometry/points.orus` for `module geometry.points`).
- `use` is valid only at module scope.
  - `use geometry.points` imports all public symbols from the module.
  - `use geometry.points: Point, origin` selects specific exports.
  - `use geometry.points: length as distance` applies aliases to symbols.
  - `use geometry.points as geo` records a module alias (namespace lookups are not generated yet).
- Declarations are private by default. Use `pub` to expose them. Globals must be uppercase identifiers.

Example layout:

```
geometry/points.orus
--------------------
module geometry.points:

    pub struct Point:
        x: i32
        y: i32

    pub fn origin() -> Point:
        return Point{ x: 0, y: 0 }

main.orus
--------
use geometry.points: Point, origin

fn main():
    p = origin()
    print(Point{ x: 5, y: 6 })
```

## 13. Explicit Conversions
- Use `as` to convert between types; implicit promotions do not exist.
- Supported conversions:
  - `i32` → `i64`, `u32`, `u64`, `f64`, `bool`, `string`
  - `i64` → `i32`, `u64`, `f64`, `string`
  - `u32` → `i32`, `i64`, `u64`, `f64`, `string`
  - `u64` → `i32`, `i64`, `u32`, `f64`, `string`
  - `f64` → `i32`, `i64`, `u32`, `u64`, `string` (fractional parts truncate toward zero)
  - `bool` → `i32`, `string`
  - Any value → `string`
- Invalid casts raise runtime errors and are flagged by type inference where possible.

```orus
value: i32 = 42
flag: bool = value as bool
text = value as string
ratio = (value as f64) / 10.0
```

## 14. Built-ins and Utilities
- `print(...)` and `print_no_newline(...)` for output.
- `len(array)`, `push(array, value)`, and `pop(array)` for dynamic arrays.
- `time_stamp()` returns a monotonic `f64` timestamp in seconds.

```orus
start: f64 = time_stamp()
for i in 0..1_000_000:
    pass
elapsed = time_stamp() - start
print("elapsed seconds:", elapsed)
```

## 15. Putting It Together

```orus
module diagnostics.report:

    pub struct Sample:
        values: [i32]

    pub fn collect(limit: i32) -> Sample:
        mut data = []
        for n in 0..=limit:
            if n % 2 == 0:
                push(data, n)
        return Sample{ values: data }

    pub fn summarize(sample: Sample) -> string:
        total = len(sample.values)
        return "even count: " + (total as string)
```

```orus
use diagnostics.report: collect, summarize

enum Status:
    Ready
    Busy(reason: string)

fn status_text(status: Status) -> string:
    return match status:
        Status.Ready -> "ready"
        Status.Busy(reason) -> "busy: " + reason

fn main():
    sample = collect(10)
    print(summarize(sample))

    current = Status.Busy("pending I/O")
    print(status_text(current))

    try:
        throw "simulated failure"
    catch err:
        print("recovered", err)
```

This program demonstrates modules, array helpers, struct methods, pattern matching, and error handling working together.

## 16. Feature Checklist
- [x] Indentation-scoped syntax (no semicolons)
- [x] Line and block comments
- [x] Numeric, string, and boolean literals with type annotations
- [x] Immutable and mutable variables, multi-declarations, compound assignments
- [x] Module-scope `pub` and uppercase `global` declarations
- [x] Arithmetic, comparison, boolean operators, and explicit casts via `as`
- [x] `matches` keyword for readable equality checks
- [x] String formatting inside `print`
- [x] Dynamic arrays with `len`, `push`, `pop`, and slicing
- [x] `if`/`elif`/`else`, `while`, and `for` (ranges, inclusive ranges, custom steps, iterables, loop labels)
- [x] Functions and function expressions
- [x] Structs, struct literals, and `impl` blocks with instance/static methods
- [x] Enums, constructors, `match` statements/expressions, destructuring, wildcard arms, exhaustiveness checks
- [x] `try`/`catch` and `throw`
- [x] Module declarations and `use` imports (including selective and aliased forms)
- [x] `time_stamp()` and the printing built-ins
- [x] Explicit cast rules, including conversions to `string`

## 17. Current Limitations
- `const`, `static`, bitwise operators (`&`, `|`, `^`, `<<`, `>>`, unary `~`), generics, list comprehensions, `range(...)`, ternary `?:`, and inline `expr if cond else alt` forms are not yet implemented.
- Block comments cannot nest.
- Module aliases created with `use module as alias` are stored for tooling but do not create a namespace object; imported symbols bind directly into the current scope.

Stay within these boundaries and the Orus toolchain will behave exactly as described above.
