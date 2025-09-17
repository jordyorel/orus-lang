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
- ⚠️ **Module system** – Runtime hooks exist (`interpret_module` and module
  loaders), yet the surface syntax and packaging workflow remain to be
  implemented and tested.
- 🚧 **Pattern matching** – Parser groundwork exists for `match` keywords, but
  expression lowering and exhaustiveness checks are still outstanding (see
  TODO below).

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
  propagation, and bytecode emission into VM chunks.
- Peephole optimizer deduplicates redundant loads and boolean immediates.
- Jump patching infrastructure and source-location metadata power reliable
  control-flow codegen and diagnostics.
- CI fixtures cover arithmetic, control flow, functions, arrays, strings, and
  polymorphic type behaviour across `tests/`.

### Runtime & Tooling
- 256-register VM with dedicated banks for globals, frames, temporaries, and
  future module imports; dispatch tables ship in both `goto` and `switch`
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
| Module packaging | Design | Module manager + loader stubs exist; surface syntax and tests still missing. |
| Print formatting polish | Backlog | Finish escape handling and numeric formatting for the print APIs. |

---

## 🎯 Near-Term Targets
1. Ship the structured diagnostic renderer with regression snapshots.
2. Complete variable lifecycle analysis and wire the results into the feature
   error reporters.
3. Extend the parser/typechecker/codegen to support iterator-style `for` loops
   using array iterators.
4. Define and implement module declarations/import syntax, backed by the
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
- **Advanced optimizations** – Loop-invariant code motion, loop unrolling, and
  strength reduction passes once diagnostics are solid.
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

   ```orus
   impl Point:
       fn magnitude(self):
           sqrt(self.x * self.x + self.y * self.y)

   fn demo():
       p = Point{ x: 3, y: 4 }
       print(p.magnitude())
   ```

4. **Pattern matching** – Provide a `match` expression with exhaustive checking
   for enums, tuples, and primitive guards while compiling each arm into linear
   control flow blocks.

  ```orus
  match parse_number(input):
      Result.Ok(value):
          print("parsed", value)
      Result.Err(msg):
          print("failed:", msg)
  ```

   - [ ] **TODO**: Implement match expressions with destructuring patterns.

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
