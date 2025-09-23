## Orus Language Reference (workspace snapshot)

This reference collects every feature currently implemented in the Orus compiler and VM that ship with this repository. Examples use the `.orus` syntax that the toolchain accepts today.

### Source Layout and Comments
- The parser is newline-driven. Semicolons (`;`) are rejected with error `E1007`.
- Blocks begin after a colon (`:`) and are formed by indentation. Tabs count as four spaces when the lexer computes indent levels.
- Inconsistent indentation raises "Inconsistent indentation" diagnostics.
- Two comment forms are recognised: `// line comment` and `/* block comment */`. Block comments do not nest.

```orus
// Single line comment
if ready:
    /* block comment
       spanning multiple lines */
    print("launch")
```

### Literals and Primitive Types
- Integers default to `i32`. Values outside the 32-bit range automatically become `i64` while parsing.
- Underscores improve readability (`1_000_000`). Hexadecimal literals use the `0x` prefix.
- Floating literals are always `f64` and support fractional and scientific notation (`6.022e23`).
- Booleans are `true` and `false`.
- Strings use double quotes and support `\n`, `\t`, `\\`, `\"`, `\r`, and `\0` escapes.

```orus
count = 42
big: i64 = 5_000_000_000
mask = 0xFF
ratio = 3.14159
message = "Line one\nLine two"
```

### Variables and Assignment
- Bindings are immutable by default. Use `mut` to allow reassignment.
- Type annotations appear after the name: `value: i64 = 0`.
- Multiple declarations share a line when separated by commas.
- Names must start with a letter or `_` and contain only letters, digits, or `_`.
- Compound assignments `+=`, `-=`, `*=`, `/=`, `%=` are supported for existing bindings.
- `global` is available only at module scope, requires an initializer, and the identifier must be uppercase. Prefix it with `mut` for writable globals.
- `pub` is valid only at module scope and exports `fn`, `global`, `struct`, `enum`, or `impl` declarations.

```orus
score = 10          // immutable
mut retries = 0     // mutable
threshold: f64 = 0.75
x = 1, mut y = 2, label: string = "hi"

mut retries += 1

pub global MAX_CONNECTIONS = 512
pub global mut CACHE_SIZE = 1_048_576
```

### Expressions and Operators
- Arithmetic: `+`, `-`, `*`, `/`, `%`
- Comparisons: `<`, `<=`, `>`, `>=`, `==`, `!=`
- Boolean logic: `and`, `or`, `not`
- The `matches` keyword is a readable alias for equality checks, especially with enums: `flag matches Flag.On` compares like `flag == Flag.On`.
- Explicit casts use `as`: `value as i64`, `count as string`, etc. There are no implicit promotions.
- Parentheses work as expected for grouping.

```orus
ratio = hits as f64 / total as f64
is_large = total > 10_000 and not aborted
use_cache = mode matches Mode.Cached
mut remaining -= chunk
```

### Strings and Printing
- `print(...)` writes arguments separated by a space and ends with a newline.
- `print_no_newline(...)` omits the trailing newline.
- A leading string literal may contain `@` format specifiers that consume the next argument (`@.2f`, `@x`, `@X`, `@b`, `@o`).

```orus
name = "Orus"
pi = 3.14159
print("Hello", name)
print_no_newline("Progress", 42, "%")
print("Pi ~= @.2f", pi)
```

### Arrays
- Array literals use brackets: `values = [1, 2, 3]`. Trailing commas are allowed.
- Indexing uses `array[index]`. Slices create new arrays with `array[start..end]` (`end` is exclusive).
- Built-ins:
  - `len(array)` returns an `i32` length.
  - `push(array, value)` appends in place and returns the array.
  - `pop(array)` removes and returns the last element.

```orus
mut numbers = []
push(numbers, 1)
push(numbers, 2)
push(numbers, 3)
print(len(numbers))        // 3
last = pop(numbers)
print("popped", last)
window = numbers[0..len(numbers)]
```

### Control Flow
- `if`, `elif`, and `else` require boolean conditions. Assignments in conditions are rejected.
- A single statement may follow the colon on the same line, or an indented block may start on the next line.

```orus
if status == "ok": print("ready")
elif status == "retry":
    print("waiting")
else:
    print("failed")
```

- `while` loops follow the same layout rules.

```orus
mut attempts = 0
while attempts < 3:
    attempts += 1
    print("try", attempts)
```

- `for` loops support two forms:
  - Ranges: `for i in start..end:` (exclusive) or `start..=end` (inclusive). A second `..step` expression sets the step.
  - Iterable loops: `for item in array:` iterates arrays.
- Loops may be labelled using a leading apostrophe (`'outer:`). `break` and `continue` optionally target a label.

```orus
for i in 0..10..2:
    print(i)

values = [1, 2, 3]
for value in values:
    print("item", value)

'outer: for row in 0..5:
    for col in 0..5:
        if row == col:
            break 'outer
```

### Functions and Function Values
- Define functions with `fn name(params) -> ReturnType:` followed by an indented block. Omit `-> ...` for `void` functions.
- Use `return` (with or without a value) to exit. No implicit return value is inserted.
- Function expressions create first-class values with the same syntax but without a name.

```orus
fn add(a: i32, b: i32) -> i32:
    return a + b

square = fn(value: i32) -> i32:
    return value * value

print(add(2, 3))
print(square(6))
```

### Structs and Methods
- Structs declare named fields and may provide default expressions.

```orus
struct Point:
    x: i32
    y: i32 = 0

origin = Point{ x: 0, y: 0 }
```

- `impl` blocks attach methods. When the first parameter is named `self`, the method is treated as an instance method and can be called with dot syntax. Other signatures behave like static helpers invoked as `Type.method(...)`.

```orus
impl Point:
    fn translate(self, dx: i32, dy: i32) -> Point:
        return Point{ x: self.x + dx, y: self.y + dy }

    fn from_tuple(coords: Point, dx: i32, dy: i32) -> Point:
        return Point{ x: coords.x + dx, y: coords.y + dy }

p = Point{ x: 1, y: 2 }
print(p.translate(3, 4).x)
print(Point.from_tuple(p, 5, 6).y)
```

### Enums and Pattern Matching
- Enums list variants. Variants may have unnamed payloads or named fields.

```orus
enum Result:
    Ok(value: i32)
    Err(message: string)
```

- Constructors use `Enum.Variant(...)`. Zero-argument variants can omit parentheses (`Flag.On`).
- `match` works as a statement or an expression. Patterns support literals, `_` wildcards, and enum destructuring with optional binding names. Exhaustiveness is validated for enums, and duplicate literal arms are rejected.

```orus
fn describe(outcome: Result) -> string:
    return match outcome:
        Result.Ok(value) -> "ok: " + (value as string)
        Result.Err(reason) -> "error: " + reason

flag = Flag.Off
if flag matches Flag.On:
    print("active")
```

- Payload bindings are available inside the matched arm. `_` ignores fields.

### Error Handling
- `try:` introduces a protected block. `catch name:` (or `catch:`) must follow immediately and handles thrown values.
- `throw expression` signals an error value.

```orus
try:
    dangerous()
catch err:
    print("caught", err)

throw "boom"
```

### Modules and Imports
- Files may start with `module path` or `module path:`. The declaration must be the first non-comment statement and may appear only once. The block form (`module pkg.tools:`) requires the entire file to be indented under the declaration.
- Dotted module names map to directories (`module pkg.stats` lives at `pkg/stats.orus`).
- `use` is available only at module scope:
  - `use math` imports all public symbols.
  - `use math: *` is synonymous with importing all symbols.
  - `use math: sin, cos as cosine` selectively imports and optionally renames symbols.
  - `use math as alias` records an alias for tooling; namespace-style access is not yet generated.
- Declarations are private by default. Apply `pub` to export `fn`, `struct`, `enum`, `impl`, or `global` items.
- Globals must be uppercase identifiers and may be exported with `pub global`.

```orus
module geometry.points:

    pub struct Point:
        x: i32
        y: i32

    pub fn origin() -> Point:
        return Point{ x: 0, y: 0 }
```

In another file:

```orus
use geometry.points: Point, origin

p = origin()
print(Point{x: 5, y: 6})
```

### Built-in Helpers
- `len(array)`, `push(array, value)`, and `pop(array)` provide basic dynamic array operations.
- `time_stamp()` returns a monotonic `f64` value measured in seconds.

```orus
start: f64 = time_stamp()
// work
elapsed = time_stamp() - start
print("elapsed", elapsed)
```

<<<<<<< HEAD
`print()` always uses a single space between values; for more specialized output,
compose strings manually before printing.

#### Format Specifiers
Orus supports minimal formatting syntax using @ as a prefix in strings:

```orus
pi = 3.14159
print("Pi = @.2f", pi)           // Output: Pi = 3.14
```

```orus
num = 255
print("Decimal =", num)          // Output: Decimal = 255
print("Hex = @x", num)           // Output: Hex = ff
print("Binary = @b", num)        // Output: Binary = 11111111
print("Octal = @o", num)         // Output: Octal = 377
```

The format specifier @ applies to the next value in the argument list. You can mix formatted and unformatted values freely.

#### Examples
```orus
print("Hello")
print("x =", x)
print("sum =", 10 + 20)

```

### Arrays

```orus
push(arr, value)
pop(arr)
reserve(arr, capacity)
len(arr)
sorted(arr)
```

### Strings

```orus
substring(s, start, len)
input(prompt)
```

### Type utilities

```orus
type_of(x)
is_type(x, "i32")
```

### Conversion

```orus
int("42")
float("3.14")
```

### Ranges

```orus
range(1, 5)       # [1, 2, 3, 4]
```

### Math helpers (from std):

```orus
sum(arr)
min(arr)
max(arr)
```

### Time

```orus
times_tamp()       # returns milliseconds
```

---

## 🧪 Interactive Examples & Quizzes

### 🔍 Operator Precedence Quiz

What is the result of the following?

```orus
x = 1
y = 2
result = x > 0 ? x + y : x + y * 2
print(result)
```

**Hint:** Multiplication binds tighter than addition, and ternary has the lowest precedence.

Try rewriting with parentheses to clarify behavior.

### 🧠 Type Inference Exercise

```orus
a = 10
b = 3.0
c = a + b
print(type_of(c))
```

What will `type_of(c)` print? Why?

### ❗Casting Safety

Guess whether each line is valid or will error:

```orus
good = 42 as string
fail = "abc" as i32
```

**Try It:** Comment/uncomment lines and run in the REPL.

---

## 🪡 Type System and Casting

### Operators

Orus supports the following operators:

### Unary Operators

* `-x` — negation
* `!x` or `not x` — logical NOT

Examples:

```orus
a = -5
b = not true
c = !false
```

### Short-Circuit Behavior

Logical operators `and` and `or` short-circuit:

* `and` stops at the first false
* `or` stops at the first true

```orus
result = expensive_call() and false  # never runs
check = true or expensive_call()     # never runs
```

### Arithmetic

```orus
+   -   *   /   %   //   # floor division
```

Numeric operators require both operands to have the **same type**. The VM does
not perform implicit promotions like `i32` to `i64` or `f64`. Use explicit casts
with `as` when combining different numeric types.

### Comparison

```orus
<   <=   >   >=   ==   !=
```

### Logical

```orus
and   or   not
```

### Bitwise

```orus
&   |   ^   <<   >>
```

### Operator Precedence (highest to lowest)

> **Note:** The ternary conditional operator `? :` has lower precedence than logical operators `and` and `or`. Use parentheses to clarify when mixing ternary and logical expressions.

| Precedence | Operators                        | Description         | Associativity |               |   |
| ---------- | -------------------------------- | ------------------- | ------------- | ------------- | - |
| 1          | `()`                             | Grouping            | left-to-right |               |   |
| 2          | `!`, `not`                       | Unary               | right-to-left |               |   |
| 3          | `*`, `/`, `%`, `//`              | Arithmetic          | left-to-right |               |   |
| 4          | `+`, `-`                         | Arithmetic          | left-to-right |               |   |
| 5          | `<<`, `>>`                       | Bitwise shift       | left-to-right |               |   |
| 6          | `&`                              | Bitwise AND         | left-to-right |               |   |
| 7          | `^`                              | Bitwise XOR         | left-to-right |               |   |
| 8          | \`                               | \`                  | Bitwise OR    | left-to-right |   |
| 9          | `<`, `>`, `<=`, `>=`, `==`, `!=` | Comparison          | left-to-right |               |   |
| 10         | `and`                            | Logical AND         | left-to-right |               |   |
| 11         | `or`                             | Logical OR          | left-to-right |               |   |
| 12         | `? :`                            | Ternary conditional | right-to-left |               |   |

### Common Operator Mistakes

* **Ternary binds looser than logical operators**:

  ```orus
  result = x > 0 ? a : b and c  // wrong: `b and c` is grouped
  result = x > 0 ? a : (b and c)  // ✅ correct
  ```

* **Mixing `not` with comparisons without parentheses**:

  ```orus
  ok = not x == 1      // wrong: means `(not x) == 1`
  ok = not (x == 1)    // ✅ correct
  ```

* **Chained comparisons don't work like in Python**:

  ```orus
  if 0 < x < 10: ...        // ❌ invalid
  if x > 0 and x < 10: ...  // ✅ correct
  ```

* **Unintended precedence between `or` and ternary**:

  ```orus
  res = cond ? a : b or c   # actually means `cond ? a : (b or c)`
  ```


### Precedence Error Example

```orus
// Misleading: 'x > 0 ? a : b and c' actually binds as:
x > 0 ? a : (b and c)

// Better: use parentheses
result = x > 0 ? a : (b and c)
```

Parentheses can override default precedence and ensure clarity:

```orus
result = (a > 0 and b > 0) ? "ok" : "fail"
safe = not (x == 1 or y == 2)
```

Without parentheses, expressions follow the precedence table above.

### Type Inference

Variables default to `i32` unless otherwise specified. Literal suffixes (`u`, `f64`, etc.) can override the default.

```orus
x = 10          // i32
y = 10000000000u  // u64
z = 3.14        // f64
```

### Type Casting

Use `as` to convert between types:

```orus
a: i32 = -5
b: u32 = a as u32
c: f64 = a as f64
d: string = a as string
```

### Casting Rules

* **Int to float**: valid, may lose precision
* **Float to int**: truncates toward zero
* **Int to bool**: 0 is `false`, non-zero is `true`
* **Bool to int**: `true` → `1`, `false` → `0`
* **All types** can be cast to `string` using `as string`
* **Invalid casts** (e.g. `string` → `i32`) raise runtime errors

```orus
b: bool = 0 as bool
s = 123 as string
```

### Parentheses and Grouping

```orus
result = (a > 0 and b > 0) ? "ok" : "fail"
safe = not (x == 1 or y == 2)
```

---
=======
### Cast (`as`) Behaviour
- Numeric casts follow explicit rules:
  - `i32` → `i64`, `u32` (value must be non-negative), `u64` (value must be non-negative), `f64`, `bool`, `string`
  - `i64` → `i32`, `u64`, `f64`, `string`
  - `u32` → `i32`, `i64`, `u64`, `f64`, `string`
  - `u64` → `i32`, `i64`, `u32`, `f64`, `string`
  - `f64` → `i32`, `i64`, `u32`, `u64`, `string` (fractional parts truncate toward zero)
- `bool` casts to `i32` (`true` → `1`, `false` → `0`) and to `string`.
- Any value may be converted to `string` with `as string`.
- Invalid conversions raise runtime errors and are caught by the type checker where possible.

```orus
count: i32 = 42
flag: bool = count as bool
hex = (count as string)
ratio = (count as f64) / 10.0
```

### Not Yet Implemented / Reserved
- `const` and `static` are reserved keywords without behaviour today.
- Bitwise operators (`&`, `|`, `^`, `<<`, `>>`, unary `~`) are tokenised but code generation is unfinished.
- Generics, list comprehensions, the `range(...)` helper, ternary `?:`, and inline `expr if cond else alt` forms are not available yet.
- Block comments cannot nest.

### Quick Reference
- Indentation defines scope; tabs count as four spaces.
- No implicit numeric promotions—use `as`.
- Arrays are dynamic and modified in place with `push`/`pop`.
- Pattern matching is exhaustive for enums and supports destructuring.
- Modules require uppercase globals and `pub` exports for cross-file use.
- `time_stamp()` alongside `print(...)` and `print_no_newline(...)` are the available runtime utilities.

Keep these rules in mind when writing Orus code—the compiler enforces them exactly as described above.
>>>>>>> 3aadbaa (Update LANGUAGE.md for Orus language reference and enhance built-in functions for time handling)
