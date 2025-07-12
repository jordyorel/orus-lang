# ✅✅✅ **What Should Pass** (Safe, Valid Orus Code)

| Expression                               | Why It Passes                                    |
| ---------------------------------------- | ------------------------------------------------ |
| `a: i32 = 10`                            | Literal matches declared type                    |
| `b: f64 = 3.14`                          | Literal matches float type                       |
| `c: bool = true`                         | Valid boolean                                    |
| `d: string = "hello"`                    | Valid string                                     |
| `e: i64 = 42i64`                         | Type-suffixed integer literal                    |
| `x: i32 = 5; y: i32 = x + 10`            | Same type addition                               |
| `result: string = (x as bool) as string` | Multi-step casting with intermediate parentheses |
| `n: u32 = 100u32; r = n * 2u32`          | Typed literal matches variable                   |
| `r: i64 = (x as i64) + 100i64`           | Explicit widening                                |
| `flag: bool = 0 as bool`                 | Explicit i32→bool                                |
| `result = flag as string`                | Explicit bool→string                             |
| `val: i64 = (some_i32 as i64)`           | Safe widening cast                               |
| `nullable: i32? = nil`                   | Nullable types allowed                           |
| `mut count: i32 = 0`                     | Mutability supported                             |
| `msg = "Hello, " + name`                 | String concatenation allowed                     |

---

# ❌❌❌ **What Should Fail** (Compile-Time Type Errors)

| Expression                                    | Why It Fails                                        |
| --------------------------------------------- | --------------------------------------------------- |
| `a: i32 = "hi"`                               | Type mismatch (string → i32)                        |
| `a = 10; b = "text"; r = a + b`               | Cannot add i32 and string                           |
| `x: i32 = true`                               | bool ≠ i32                                          |
| `result = a as bool as string`                | Chained casts without parentheses are disallowed    |
| `x: i64 = 42; y: u64 = 10u64; z = x + y`      | No implicit i64 + u64 allowed                       |
| `a: i32 = 50; b: i64 = 100 as i32; r = a + b` | `100 as i32` still i32; adding i32 + i64 is invalid |
| `x: u32 = 5; y = x * 2`                       | `2` is `i32`; mismatch with `u32`                   |
| `nullable: i32 = nil`                         | nil not allowed in non-nullable type                |
| `x = 10; y = "10"; x == y`                    | Cannot compare i32 and string                       |
| `x: i32 = 5.5`                                | Implicit float → int not allowed                    |
| `x = 3 + true`                                | Cannot add int and bool                             |
| `fn add(a: i32, b: string) -> i32: a + b`     | Mismatched operand types                            |
| `some_val as unknown_type`                    | Invalid cast target                                 |
| `x: i32 = some_i64`                           | i64 to i32 narrowing without cast disallowed        |

---

# ⚠️⚠️⚠️ **What Should Warn or Require Explicit Annotation**

| Expression            | Behavior                                                         |
| --------------------- | ---------------------------------------------------------------- |
| `result = x + 2`      | Warn if `2` inferred as `i32`, but `x` is `u32` — force cast     |
| `nullable: i32? = 42` | Valid, but compiler should confirm nullability is respected      |
| `if val:`             | Warn if `val` is not explicitly boolean                          |
| `a = 2147483648`      | Warn about int literal overflow for `i32`                        |
| `x = some_func()`     | Infer type, but recommend annotation if used in API/public scope |

---

# 🧩 Optional Flexibility (Decide Based on Philosophy)

| Expression                  | Recommendation                                                           |
| --------------------------- | ------------------------------------------------------------------------ |
| `x = 5 as string`           | Allow only if `as string` is defined as safe coercion like `to_string()` |
| `x: any = 5`                | Allow if `any` is part of the system, but require unwrapping for usage   |
| `result = x ? "yes" : "no"` | Allow ternary only if condition is guaranteed `bool`                     |
| `x = 10 + 20u32`            | Disallow unless both are explicitly same type                            |

---

## 🔐 Summary of Core Rules

### 🚫 Disallow:

* Implicit coercion between signed/unsigned integers
* Implicit widening or narrowing (`i32 ↔ i64`, `i32 ↔ f64`)
* Implicit string conversion
* Chained `as` casts without parentheses
* Arithmetic between mismatched types
* Using `nil` with non-nullable types

### ✅ Require:

* Explicit `as` for all type changes
* Matching types in arithmetic and comparisons
* Intermediate variables for multi-step casts (or parentheses)
* Type annotation for mutable, nullable, and public API variables
