## ✨ Simplified Orus Syntax Guide (v0.2.0+)

This guide introduces a simplified and elegant Orus syntax inspired by Python, V, and Rust, using indentation-based blocks and optional return statements. It assumes Orus v0.2.0+.

---

## 🚀 Getting Started

```orus
fn main:
    print("Hello, Orus!")
```

---

## 🧳 Variables and Mutability

All variables default to `i32` unless an explicit type is provided.

```orus
let number = 5           # inferred as i32
let flag: bool = true    # explicitly bool
let mut count = 0        # mutable, inferred as i32
```

---

## 🔢 Constants

```orus
pub const LIMIT: i32 = 10

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
let label = x > 0 ? "positive" : "non-positive"
```

print("ok") if x == 1 elif x == 2 else print("fallback")

```
```

````

### Loops

```orus
for i in 0..5:
    print(i)

while condition:
    print("looping")

break     # exits the nearest loop
continue  # skips to the next iteration
````

---

## 📊 Functions

```orus
fn add(a: i32, b: i32) -> i32:
    a + b

fn greet(name: string):
    print("Hello, {}!", name)

# Format specifiers
let pi = 3.14159
print("Pi rounded: {:.2}", pi)
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
            Status.Error(msg): panic("Unwrapped error: {}", msg)
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
    let x = 10 / 0
catch err:
    print("Error: {}", err)
```

---

## 📒 Arrays

```orus
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

---

## 📐 Generics

```orus
fn identity<T>(x: T) -> T:
    x

struct Box<T>:
    value: T

fn main:
    let a = identity<i32>(5)
    let b: Box<string> = Box{ value: "hi" }
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

```orus
print("Hello")
print("x = {}", x)
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
let x = 1
let y = 2
let result = x > 0 ? x + y : x + y * 2
print(result)
```

**Hint:** Multiplication binds tighter than addition, and ternary has the lowest precedence.

Try rewriting with parentheses to clarify behavior.

### 🧠 Type Inference Exercise

```orus
let a = 10
let b = 3.0
let c = a + b
print(type_of(c))
```

What will `type_of(c)` print? Why?

### ❗Casting Safety

Guess whether each line is valid or will error:

```orus
let good = 42 as string
let fail = "abc" as i32
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
let a = -5
let b = not true
let c = !false
```

### Short-Circuit Behavior

Logical operators `and` and `or` short-circuit:

* `and` stops at the first false
* `or` stops at the first true

```orus
let result = expensive_call() and false  # never runs
let check = true or expensive_call()     # never runs
```

**Arithmetic:**

```orus
+   -   *   /   %   //   # floor division
```

**Comparison:**

```orus
<   <=   >   >=   ==   !=
```

**Logical:**

```orus
and   or   not
```

**Bitwise:**

```orus
&   |   ^   <<   >>
```

### Operator Precedence (highest to lowest)

> **Note:** The ternary conditional operator `? :` has lower precedence than logical operators `and` and `or`. Use parentheses to clarify when mixing ternary and logical expressions.

\| Precedence | Operators           | Description            | Associativity        | Description            | Associativity        |
\|------------|---------------------|------------------------|
\| 1          | `()`                | Grouping               | left-to-right        |
\| 2          | `!`, `not`          | Unary                  | right-to-left        |
\| 3          | `*`, `/`, `%`, `//` | Arithmetic             | left-to-right        |
\| 4          | `+`, `-`            | Arithmetic             | left-to-right        |
\| 5          | `<<`, `>>`          | Bitwise shift          | left-to-right        |
\| 6          | `&`                 | Bitwise AND            | left-to-right        |
\| 7          | `^`                 | Bitwise XOR            | left-to-right        |
\| 8          | `|`                 | Bitwise OR             | left-to-right        |
\| 9          | `<`, `>`, `<=`, `>=`, `==`, `!=` | Comparison | left-to-right |
\| 10         | `and`               | Logical AND            | left-to-right        |
\| 11         | `or`                | Logical OR             | left-to-right        |
\| 12         | `? :`               | Ternary conditional     | right-to-left        |

### Primitive Types

* `i32`, `i64` – signed integers
* `u32`, `u64` – unsigned integers
* `f64` – floating-point
* `bool` – `true` or `false`
* `string` – UTF-8 text
* `void` – no value (function return)
* `nil` – explicit null value

### Parentheses and Grouping

### Visual Operator Hierarchy (from tightest to loosest)

```
 ┌─────────────────────────────┐
 │       () grouping          │
 │     !, not (unary)         │
 │   *, /, %, //              │
 │   +, -                     │
 │   <<, >>                   │
 │   &, ^, | (bitwise ops)    │
 │   ==, !=, <, >, <=, >=     │
 │   and                      │
 │   or                       │
 │   ? : (ternary)            │
 └─────────────────────────────┘
```

### Common Operator Mistakes

* **Ternary binds looser than logical operators**:

  ```orus
  let result = x > 0 ? a : b and c  # wrong: `b and c` is grouped
  let result = x > 0 ? a : (b and c)  # ✅ correct
  ```

* **Mixing `not` with comparisons without parentheses**:

  ```orus
  let ok = not x == 1      # wrong: means `(not x) == 1`
  let ok = not (x == 1)    # ✅ correct
  ```

* **Chained comparisons don't work like in Python**:

  ```orus
  if 0 < x < 10: ...        # ❌ invalid
  if x > 0 and x < 10: ...  # ✅ correct
  ```

* **Unintended precedence between `or` and ternary**:

  ```orus
  let res = cond ? a : b or c   # actually means `cond ? a : (b or c)`
  ```

### Precedence Error Example

```orus
# Misleading: 'x > 0 ? a : b and c' actually binds as:
x > 0 ? a : (b and c)

# Better: use parentheses
let result = x > 0 ? a : (b and c)
```

Parentheses can override default precedence and ensure clarity:

```orus
let result = (a > 0 and b > 0) ? "ok" : "fail"
let safe = not (x == 1 or y == 2)
```

Without parentheses, expressions follow the precedence table above.

### Type Inference

Variables default to `i32` unless otherwise specified. Literal suffixes (`u`, `f64`, etc.) can override the default.

```orus
let x = 10          # i32
let y = 10000000000u  # u64
let z = 3.14        # f64
```

### Type Casting

Use `as` to convert between types:

```orus
let a: i32 = -5
let b: u32 = a as u32
let c: f64 = a as f64
let d: string = a as string
```

### Casting Rules

* **Int to float**: valid, may lose precision
* **Float to int**: truncates toward zero
* **Int to bool**: 0 is `false`, non-zero is `true`
* **Bool to int**: `true` → `1`, `false` → `0`
* **All types** can be cast to `string` using `as string`
* **Invalid casts** (e.g. `string` → `i32`) raise runtime errors

```orus
let b: bool = 0 as bool
let s = 123 as string
```

---

This version reflects all updated features including enums, arrays, generics, modules, loops, built-ins, and the type system with casting rules using indentation-based syntax.
