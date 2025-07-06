## ✨ Simplified Orus Syntax Guide (v0.2.0+)

This guide introduces a simplified and elegant Orus syntax inspired by Python, V, and Rust, using indentation-based blocks, automatic type inference, and optional return statements. It assumes Orus v0.2.0+.

---

## 🚀 Getting Started

```orus
fn main:
    print("Hello, Orus!")
```

---

## 🧳 Variables and Mutability

All variables default to `i32` unless an explicit type is provided. The `let` keyword has been removed.

```orus

number = 5           # inferred as i32
flag: bool = true    # explicitly bool
mut count = 0        # mutable, inferred as i32
x = 5               # inferred as immutable i32
mut y = 10          # mutable, inferred as i32
z: i64 = 0          # explicitly typed immutable
mut a: u32 = 10     # explicitly typed mutable

```

Multiple declarations can be written on a single line by separating them with
commas. Semicolons are **not** allowed; use a newline to terminate statements.

```orus
x = 1, y = 2, z = 3  # declares three variables
```

### Lexical Scoping

Orus uses lexical scoping. A variable declared inside a block is visible only
within that block. Inner declarations with the same name **shadow** outer
variables without modifying them.

```orus
x = 5
if true:
    mut x = 1  # shadows outer `x`
    print(x)   # prints 1
print(x)       # prints 5
```

---

## 🔢 Constants

```orus
pub const LIMIT = 10

fn main:
    for i in 0..LIMIT:
        print(i)
```

---

## ⟳ Control Flow

### Conditionals

```orus
if n > 0:
    print("positive")
elif n == 0:
    print("zero")
else:
    print("negative")

# Inline conditionals (supports if, elif, else)
print("ok") if x == 1 elif x == 2 else print("fallback")

# Ternary expression assignment
label = x > 0 ? "positive" : "non-positive"
```

---

### Loops

#### Basic Loops

```orus
for i in 0..5:
    print(i)

while condition:
    print("looping")

break     # exits the nearest loop
continue  # skips to the next iteration
```

#### Advanced Range Syntax with Step

Orus supports advanced range syntax with customizable step values:

```orus
# Forward iteration with step
for i in 0..10..2:
    print(i)  # prints: 0, 2, 4, 6, 8

# Backward iteration with negative step
for i in 10..0..-2:
    print(i)  # prints: 10, 8, 6, 4, 2

# Large steps
for i in 0..100..25:
    print(i)  # prints: 0, 25, 50, 75

# Single element iteration
for i in 5..6..1:
    print(i)  # prints: 5
```

#### Progressive Loop Safety System

Orus includes a progressive loop safety system that automatically protects against infinite loops while maintaining maximum performance:

| **Iteration Count** | **Behavior** | **Message** |
|---------------------|--------------|-------------|
| `< 100K` | ✅ Fast, guard-free execution | None |
| `100K–1M` | ✅ Guarded silently | No message |
| `1M–10M` | ⚠️ Warns at 1M | Prints warning |
| `> 10M` | ❌ Stops by default | Runtime error |

**Examples:**
```orus
# Fast execution (no guards)
for i in 0..50000:
    print(i)

# Silent protection (guards active)
for i in 0..500000:
    process_data(i)

# Warning + continuation
for i in 0..1500000:     # Warns at 1M, continues
    compute(i)

# Configurable limits
# Set ORUS_MAX_LOOP_ITERATIONS=0 for unlimited loops
```

**Compile-time Detection:**
```orus
while true:     # ❌ Detected as infinite loop
    print("infinite")

for i in 10..0:  # ❌ Invalid direction detected
    print(i)
```

**Direction Validation:**
```orus
# Step direction must match range direction
for i in 0..10..1:   # ✅ Valid: positive step for forward range
    print(i)

for i in 10..0..-1:  # ✅ Valid: negative step for backward range
    print(i)

for i in 0..10..-1:  # ❌ Invalid: negative step for forward range
    print(i)
```

---

## 📊 Functions

```orus
fn add(a, b) -> i32:
    a + b

fn greet(name: string):
    print("Hello,", name, "!")

# Format specifiers
pi = 3.14159
print("Pi rounded: @.2f", pi)
print("Hello,", name)

# Format specifiers
pi = 3.14159
print("Pi rounded:", pi)

```

---

## 🪛 Structs and Methods

```orus
struct Point:
    x: i32
    y: i32

impl Point:
    fn new(x: i32, y: i32) -> Point:
        Point{ x: x, y: y }

    fn move_by(self, dx: i32, dy: i32):
        self.x = self.x + dx
        self.y = self.y + dy
```

---

## 🛠️ Enums

```orus
enum Status:
    Ok
    NotFound
    Error(message: string)

impl Status:
    fn is_ok(self): self matches Status.Ok
    fn unwrap(self):
        match self:
            Status.Ok(v): v
            Status.Error(msg): panic("Unwrapped error:", msg)
```

---

## 🔄 Pattern Matching

```orus
match value:
    0: print("zero")
    1: print("one")
    _: print("other")
```

---

## 🚨 Error Handling

```orus
try:
    x = 10 / 0
catch err:
    print("Error:", err)
```

---

## 📒 Arrays

```orus
nums: [i32; 3] = [1, 2, 3]
zeros = [0; 5]
slice = nums[0..2]

dynamic: [i32] = []
push(dynamic, 42)
pop(dynamic)

for val in nums:
    print(val)

evens = [x for x in nums if x % 2 == 0]
```

---

## 📐 Generics

```orus
fn identity<T>(x: T) -> T:
    x

struct Box<T>:
    value: T

fn main:
    a = identity<i32>(5)
    b: Box<string> = Box{ value: "hi" }
```

With constraints:

```orus
fn add<T: Numeric>(a: T, b: T) -> T:
    a + b

fn min<T: Comparable>(a: T, b: T) -> T:
    a if a < b else b
```

---

## 📂 Modules

Importing entire modules:

```orus
use math
use datetime as dt

dt.now()
math.pi
```

Wildcard import:

```orus
use math:*
sin(0.5)
cos(1.0)
```

Selective import:

```orus
use math: sin, cos, tan
print(sin(0.5))
```

Module aliases:

```orus
use utils.helpers as h
h.do_something()
```

Public function or struct in module:

```orus
# utils.orus
pub fn helper():
    print("from helper")

# main.orus
use utils

fn main:
    utils.helper()
```

---

## 🔧 Built-in Functions

### Printing
Orus supports a minimal formatting syntax using @ as a prefix in strings to indicate formatting of the next argument:

```orus
pi = 3.14159
print("Pi = @.2f", pi)
```

```orus
num = 255
print("Decimal =", num)
print("Hex = @x", num)      // hex output
print("Binary = @b", num)   // binary output
print("Octal = @o", num)    // octal output
```

The format specifier @ applies to the next value in the argument list. You can mix formatted and unformatted values freely.

For float precision:
print("rounded:", round(3.14159, 2))

```orus
print("Hello")
print("x =", x)

=======
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
timestamp()       # returns milliseconds
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
  result = x > 0 ? a : b and c  # wrong: `b and c` is grouped
  result = x > 0 ? a : (b and c)  # ✅ correct
  ```

* **Mixing `not` with comparisons without parentheses**:

  ```orus
  ok = not x == 1      # wrong: means `(not x) == 1`
  ok = not (x == 1)    # ✅ correct
  ```

* **Chained comparisons don't work like in Python**:

  ```orus
  if 0 < x < 10: ...        # ❌ invalid
  if x > 0 and x < 10: ...  # ✅ correct
  ```

* **Unintended precedence between `or` and ternary**:

  ```orus
  res = cond ? a : b or c   # actually means `cond ? a : (b or c)`
  ```


### Precedence Error Example

```orus
# Misleading: 'x > 0 ? a : b and c' actually binds as:
x > 0 ? a : (b and c)

# Better: use parentheses
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
x = 10          # i32
y = 10000000000u  # u64
z = 3.14        # f64
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
