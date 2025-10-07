# Orus Language Keywords

This reference summarises every reserved keyword currently recognised by the Orus grammar (see `docs/grammar.ebnf`). The list below reflects the canonical `keyword` production and is kept in alphabetical order for quick lookup.

## Summary
- **Total keywords**: 26
- **Source**: `keyword` production in `docs/grammar.ebnf`

## Keyword List
| Keyword | Primary usage |
|---------|---------------|
| `and` | Boolean conjunction operator |
| `as` | Explicit type casting and import aliasing |
| `break` | Exit the nearest loop |
| `catch` | Begin an exception handler suite |
| `continue` | Skip to the next loop iteration |
| `elif` | Else-if branch in conditional statements |
| `else` | Default branch in conditional statements |
| `enum` | Declare an enumeration type |
| `fn` | Declare a function |
| `for` | Begin a counted or iterator-driven loop |
| `if` | Begin a conditional statement |
| `impl` | Define implementations associated with a type |
| `in` | Membership operator in `for` loops and pattern guards |
| `match` | Begin a pattern-matching expression |
| `matches` | Structural comparison operator within expressions |
| `mut` | Introduce a mutable binding |
| `not` | Boolean negation operator |
| `or` | Boolean disjunction operator |
| `pass` | Explicitly perform no operation |
| `print` | Invoke the standard output builtin |
| `pub` | Mark declarations as public |
| `return` | Exit from a function with optional values |
| `struct` | Declare a structure type |
| `try` | Begin a try block for exception handling |
| `use` | Import modules or symbols |
| `while` | Begin a condition-controlled loop |

> **Note:** Boolean literals `true` and `false` are not reserved keywords; they are classified as `boolean_literal` tokens in the grammar.

Keep this document updated if the grammar's `keyword` production changes so the keyword count remains accurate.