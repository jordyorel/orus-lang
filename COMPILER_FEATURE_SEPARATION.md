# Compiler Feature Separation Specification

## Single-Pass Compiler (singlepass.c)
**Purpose**: Handle ONLY the simplest constructs for maximum compilation speed

### ✅ SUPPORTED Features:
- `NODE_LITERAL` - Basic literals (numbers, strings, booleans)
- `NODE_IDENTIFIER` - Simple variable references
- `NODE_BINARY` - Basic arithmetic (+, -, *, /, %, ==, !=, <, >, <=, >=)
- `NODE_ASSIGN` - Simple variable assignments
- `NODE_VAR_DECL` - Simple variable declarations
- `NODE_PRINT` - Simple print statements
- `NODE_BLOCK` - Simple statement blocks
- `NODE_IF` - Simple if/else (ONLY single level, no nesting)
- `NODE_FOR_RANGE` - Simple for loops (ONLY single level, NO break/continue)
- `NODE_WHILE` - Simple while loops (ONLY single level, NO break/continue)
- `NODE_PROGRAM` - Program root
- `NODE_TIME_STAMP` - Time operations

### ❌ NOT SUPPORTED (Must redirect to multi-pass):
- `NODE_BREAK` - Break statements
- `NODE_CONTINUE` - Continue statements  
- `NODE_FUNCTION` - Function definitions
- `NODE_CALL` - Function calls
- `NODE_RETURN` - Return statements
- Any nested loops
- Any nested if/else
- Complex expressions
- Type casting (`as` expressions)

## Multi-Pass Compiler (multipass.c) 
**Purpose**: Handle ALL complex constructs with proper analysis

### ✅ SUPPORTED Features:
- ALL features that single-pass supports
- `NODE_BREAK` - Break statements with proper scoping
- `NODE_CONTINUE` - Continue statements with proper scoping
- `NODE_FUNCTION` - Function definitions
- `NODE_CALL` - Function calls
- `NODE_RETURN` - Return statements
- Nested loops of any depth
- Nested if/else statements
- Complex expressions and type casting
- Advanced scoping and variable lifetime management

## Routing Logic in hybride_compiler.c

The hybrid compiler will analyze the AST and route to:
- **Single-pass**: Only if NO complex features are present
- **Multi-pass**: If ANY complex feature is detected

This ensures clean separation and prevents the state corruption we discovered.