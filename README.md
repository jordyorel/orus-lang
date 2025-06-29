# Orus Programming Language

A register-based virtual machine implementation for the Orus programming language.

## Current Status

✅ **Core Components Integrated and Working:**

- **Register-based Virtual Machine**: Complete implementation with 256 registers
- **Lexer**: Full lexical analysis with token support
- **Parser**: Basic AST generation (currently supports integer literals)
- **Compiler**: AST to bytecode compilation pipeline
- **Bytecode Execution**: VM executes compiled bytecode
- **Debug Support**: Instruction tracing and disassembly

## Features Currently Working

### 1. Integer Literals
- Parse and execute simple integer literals
- Example: `42`, `123`, `789`

### 2. REPL Mode
```bash
./orus
```
Interactive mode where you can enter expressions and see results immediately.

### 3. File Execution
```bash
./orus filename.orus
```
Execute code from a file.

### 4. Debug Tracing
```bash
./orus --trace filename.orus
```
Shows register states and instruction execution.

## Build Instructions

```bash
make clean
make
```

## Testing

### Basic Integer Test
```bash
echo "42" > test.orus
./orus test.orus
# Output: 42
```

### REPL Test
```bash
./orus
# Enter: 123
# Output: 123
# Enter: exit
```

### Debug Trace
```bash
echo "42" > test.orus  
./orus --trace test.orus
# Shows detailed execution trace
```

## Architecture

### Components

1. **Lexer** (`src/compiler/lexer.c`): Tokenizes source code
2. **Parser** (`src/compiler/parser.c`): Generates AST (basic implementation)
3. **Compiler** (`src/compiler/compiler.c`): Compiles AST to bytecode
4. **VM** (`src/vm/vm.c`): Executes bytecode on register-based architecture
5. **Debug** (`src/vm/vm.c`): Disassembly and tracing utilities

### Bytecode Example

For input `42`, the compiler generates:
```
0000    LOAD_CONST    R0, #0 '42'
0003    PRINT         R0
0005    HALT
```

## Performance Options

The VM is compiled with several optimizations enabled by default:

- **Computed Goto Dispatch** – eliminates the dispatch switch and uses a jump
  table for faster instruction decoding.
- **Fast Arithmetic** – skips overflow checks for integer math.
- **Memory Pool** – reuses freed VM objects to reduce allocation overhead.

Simply run `make` to build the optimized VM:

```bash
make clean
make
```

## Next Steps for Improvement

The foundation is solid and ready for enhancement:

1. **Enhanced Parser**: 
   - Binary expressions (`1 + 2`)
   - Variables and assignments
   - Function calls

2. **More Data Types**:
   - Strings, booleans, floats
   - Arrays and objects

3. **Control Flow**:
   - If/else statements
   - Loops (for, while)

4. **Functions**:
   - Function definitions
   - Parameter passing
   - Return values

5. **Advanced Features**:
   - Error handling
   - Module system
   - Standard library

## Project Structure

```
orus-reg-vm/
├── include/           # Header files
├── src/
│   ├── compiler/      # Lexer, parser, compiler
│   ├── vm/           # Virtual machine implementation
│   └── main.c        # Entry point and REPL
├── makefile          # Build configuration
└── *.orus           # Test files
```

## Command Line Options

- `--help` or `-h`: Show usage information
- `--version` or `-v`: Show version
- `--trace` or `-t`: Enable execution tracing
- `--debug` or `-d`: Enable debug mode

The project successfully demonstrates a complete language implementation pipeline from source code to execution!
