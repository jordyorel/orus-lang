## ✨ Simplified Orus Syntax Guide (v0.2.0+)

This guide introduces a simplified and elegant Orus syntax inspired by Python, V, and Rust, using indentation-based blocks and optional return statements. It assumes Orus v0.2.0+.

---

## 🚀 Getting Started

A simple program prints text using the built-in `print` function:

```orus
fn main:
    print("Hello, Orus!")
```

Or as a one-liner:

```orus
fn main: print("Hello, Orus!")
```

---

## 🧳 Variables and Mutability

```orus
let number: i32 = 5       # immutable
let mut count = 0         # mutable, inferred type
```

```orus
fn demo:
    let mut value = 1
    value = 2       # ✅ allowed
    # value = 3.0   # ❌ type mismatch
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
```

Inline expression:

```orus
print("positive") if n > 0 else print("non-positive")
```

### Loops

```orus
for i in 0..5:
    print(i)

while condition:
    print("looping")
```

---

## 📊 Functions

### Multi-line:

```orus
fn add(a: i32, b: i32) -> i32:
    return a + b
```

Or without `return`:

```orus
fn add(a: i32, b: i32) -> i32:
    a + b
```

### One-liner:

```orus
fn square(x: i32) -> i32: x * x
```

### No return value:

```orus
fn greet(name: string):
    print("Hello, {}!", name)
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

fn check(val: i32) -> Status:
    if val > 0: Status.Ok
    elif val == 0: Status.NotFound
    else: Status.Error("Negative value")
```

---

## 🔄 Pattern Matching

```orus
match value:
    0:
        print("zero")
    1:
        print("one")
    _:
        print("other")
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

## 🧲 Best Practices

* Use `:` instead of `{}` for function and block headers.
* Indentation determines block scope.
* Favor immutable bindings; use `let mut` only when needed.
* Final expression in a function can be returned implicitly.
* Use inline `if` for concise conditions.
* Prefer clear module and type naming conventions.

---

## 🎉 Future Roadmap for Performance & Features

### ⌚ Immediate (1–2 weeks)

* ✅ String interning (`OP_STRING_INTERN_R`) for faster equality checks
* ✅ Fused loop opcodes (`OP_FOR_RANGE_R`) for tighter loops
* ✅ Object memory pooling for small-size structs

### ⏳ Short-Term (2–4 weeks)

* ⚖️ Inline caching for property access (hidden class model)
* ➕ Add shape-versioning to structs for fast field resolution
* ➕ Type-specialized opcodes (e.g., `OP_ARRAY_SUM_I32_R`) with SIMD

### ⚖️ Medium-Term (1–2 months)

* 🚀 Add method-based JIT compiler for hot paths
* ⚖️ Add concurrent generational GC with nursery promotion
* ⛏ Rope strings, SSO (small string optimization), and SIMD string ops
* 🧰 JSON parser in C for microbenchmarks

### 📈 Optimization per Benchmark

* **Numeric**: already 12.37x JS, continue adding SIMD vector ops
* **Strings**: ropes, SIMD search, interned strings
* **Objects**: inline caching + shape maps
* **Regex**: compile to DFA + SIMD matching

### 🔮 Strategic Features

* 🔄 Precompile `std` to memory-mapped bytecode
* ⚖️ Zero-init standard globals at launch
* ⚖️ Trace compilation or PGO for real-world speed
* 🌍 Dynamic types with `dyn` keyword only for scripting mode

---

This guide will continue evolving as Orus matures. See `docs/TUTORIAL.md` and `tests/` for more examples.
