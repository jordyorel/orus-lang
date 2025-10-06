## 🎯 Orus Error Reporting Style Guide (v0.2.0+)

Orus error messages aim to combine the **precision and structure of Rust** with the **readability, empathy, and human-centered friendliness of Elm and Scala**. The compiler doesn't just report an issue — it talks to you like a mentor who wants to help you fix it.

---

## 🧱 General Format

Each error follows this consistent multi-section format:

```
-- {CATEGORY}: {short title} ------------------------------ {Module.orus}:{line}:{col}

{line} | {source line}
       |         ^^^^ {inline message}
       |
       = {main error explanation}
       = help: {optional hint or suggestion}
       = note: {optional context or background}
```

### 🗣️ Human-Centered Touch

Whenever possible, error messages:

* Use kind, conversational phrasing
* Assume user is learning, not at fault
* Avoid "you did this wrong" in favor of "this might not be what you meant"
* Offer next steps clearly and calmly

---

## 🔍 Example Errors

### 🧨 Type Mismatch

```
-- TYPE MISMATCH: This value isn't what we expected ------- main.orus:3:15

3 | x: i32 = "hello"
              |   ^^^^^^ this is a `string`, but `i32` was expected
              |
              = Orus expected an integer here, but found a text value instead.
              = help: You can convert a string to an integer using `int("...")`, if appropriate.
              = note: Strings and integers can't be mixed directly.
```

### 💥 Runtime Error

```
-- RUNTIME PANIC: Oh no! You tried to divide by zero ------ main.orus:10:18

10 | z = 10 / 0
               |  ^ can't divide by zero — it's undefined!
               |
               = This computation stopped because dividing by zero is mathematically invalid.
               = help: Add a check before dividing to make sure the number isn't zero.
```

### ❌ Syntax Error

```
-- SYNTAX ERROR: Something’s missing here ----------------- main.orus:2:16

2 | fn hello(name)
                |  ^ it looks like you're starting a function, but missing a ':' to begin the body
                |
                = Orus expects a ':' after function headers.
                = help: Try adding a ':' at the end of this line.
```

---

## 📚 Error Categories

| Code Range | Category       | Example                         |
| ---------- | -------------- | ------------------------------- |
| E0000–0999 | Runtime errors | Division by zero, null access   |
| E1000–1999 | Syntax errors  | Missing colon, unexpected token |
| E2000–2999 | Type errors    | Mismatched types                |
| E3000–3999 | Module/use     | File not found, cyclic use      |
| E9000–9999 | Internal bugs  | Internal panic, VM crash        |

---

## 🎨 CLI Presentation Features

* ✅ Colors: red for errors, yellow for warnings, green for notes
* ✅ Unicode framing for visual clarity
* ✅ Inline caret + vertical line for positioning
* ✅ Optional compact mode: `file:line:col: error msg`
* ✅ Optional backtrace for `panic!()`

---

## 📌 Common Mistakes to Catch Early

* Type mismatches with clear "found vs expected"
* Syntax missing required colon or indent
* Out-of-range indexing
* Unused `use` clauses, unreachable code (optional warnings)

---

## 📎 Future Plans

* [ ] Add suggestion fixes in REPL (e.g. "Did you mean: ...?")
* [ ] Emit warnings with `warning[...]` formatting
* [ ] Link error codes to docs (e.g. [https://orus-lang.org/errors/E2101](https://orus-lang.org/errors/E2101))
* [ ] Include emoji in friendly REPL feedback mode (opt-in)

