# Orus Programming Language

A register-based virtual machine implementation for the Orus programming language.

## Features

- **Register-based VM**: More efficient than stack-based VMs for complex expressions
- **Static typing**: Built-in type system with type checking
- **Modern syntax**: Clean, readable syntax inspired by modern languages
- **Memory management**: Automatic garbage collection
- **Error handling**: Comprehensive error handling with try/catch
- **Module system**: Import/export functionality for code organization

## Project Structure

```
orus-lang/
├── src/                    # Source code
│   ├── core/              # Core AST and utilities
│   ├── compiler/          # Lexer, parser, and compiler
│   ├── vm/                # Virtual machine implementation
│   └── main.c             # Main interpreter executable
├── include/               # Header files
├── tests/                 # Test files
├── docs/                  # Documentation
├── examples/              # Example programs
└── tools/                 # Development tools
```

## Building

### Using CMake (Recommended)

```bash
mkdir build && cd build
cmake ..
make
```

### Using Make (Legacy)

```bash
make
```

## Running Tests

```bash
# Using CMake
cd build && ctest

# Using Make
make test
```

## Usage

```bash
# Run the interpreter
./orus program.orus

# Run tests
./test-register
./test-parser
```

## Language Overview

### Basic Syntax

```orus
// Variables
let x: i32 = 42;
let name: string = "Hello, World!";

// Functions
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

// Control flow
if x > 0 {
    print("Positive");
} else {
    print("Non-positive");
}

// Loops
for i in 0..10 {
    print(i);
}
```

## Architecture

### Register-based VM

Unlike stack-based VMs, our register-based design:
- Reduces instruction count for complex expressions
- Enables better optimization opportunities
- Simplifies register allocation
- Provides clearer execution model

### Compilation Pipeline

1. **Lexical Analysis**: Source code → Tokens
2. **Parsing**: Tokens → AST
3. **Type Checking**: AST → Typed AST
4. **Code Generation**: Typed AST → Bytecode
5. **Execution**: Bytecode → Results

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

MIT License - see LICENSE file for details.
