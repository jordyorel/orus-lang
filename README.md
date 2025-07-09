# Orus Programming Language

**Orus** is a fast, elegant programming language that brings together the best of modern language design: Python's readability, Rust's safety, and blazing performance from a register-based virtual machine.

---

## 🎯 Why Orus?

### **Performance First**
Built on a register-based VM with computed-goto dispatch, Orus outperforms Python by **5×**, JavaScript by **11×**, and even competes with Lua. No JIT warmup time, just consistent speed.

### **Readable & Expressive**
Clean indentation-based syntax that feels natural. Write less boilerplate, express more meaning:

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: 
        n
    else: 
        fibonacci(n-1) + fibonacci(n-2)

let result = fibonacci(10)
print("Result: ", result)
```

### **Safe by Default**
Static typing with intelligent inference. Catch errors at compile time, not runtime:

```orus
let numbers: [i32] = [1, 2, 3, 4, 5]
let evens = [x for x in numbers if x % 2 == 0]  # Type-safe comprehensions
```

### **Modern Features**
Advanced type inference, functions, and type safety - with pattern matching and generics coming soon:

```orus
fn fibonacci(n: i32) -> i32:
    if n <= 1: 
        n
    else: 
        fibonacci(n-1) + fibonacci(n-2)

// Type inference in action
fn add(a, b):  // Types inferred from usage
    a + b

result = add(1, 2)        // Inferred as i32
float_result = add(1.0, 2.0)  // Inferred as f64
```

---

## 🚀 Language Overview

### **Variables & Types**
```orus
name = "Alice"           # inferred as string
age: i32 = 25           # explicit typing
mut score = 0           # mutable variable
```

### **Control Flow with Advanced Loop Safety**
```orus
if score > 100:
    print("High score!")
elif score > 50:
    print("Good job!")
else:
    print("Keep trying!")

# Advanced range syntax with steps
for i in 0..20..2:
    print("Even number: ", i)

# Progressive loop safety system
for i in 0..1500000:        # Warns at 1M, continues
    process_data(i)         # Automatic protection against runaway loops
```

### **Structs & Methods**
```orus
struct Player:
    name: string
    health: i32

impl Player:
    fn new(name: string) -> Player:
        Player{ name: name, health: 100 }
    
    fn take_damage(mut self, damage: i32):
        self.health -= damage
```

### **Error Handling**
```orus
try:
    let result = risky_operation()
    print("Success: ", result)
catch err:
    print("Error: ", err)
```

---

## ⚡ Performance Benchmarks

Orus delivers excellent performance, meeting or exceeding documented targets:

### Current Performance (Jul 2025)
| Benchmark Type | **Orus** | Python | Node.js | Lua | Status |
|----------------|----------|--------|---------|-----|---------|
| **Arithmetic** | ** 19.4ms** ⚡ | 69.5ms | 50.2ms |  29.5ms | ✅ **On Target** |
| **Control Flow** | **18.3m** ⚡ | 102.9ms |  52.2ms | 34.9ms | ✅ **On Target** |


### Performance Highlights
- **Zero JIT warmup time** - Consistent performance from first execution
- **Register-based VM** - Efficient bytecode execution with computed-goto dispatch
- **Loop optimization** - Advanced LICM and register allocation
- **Memory efficiency** - Mark-and-sweep GC with object pooling

---

## 🛠️ Getting Started

### **Build Orus**
```bash
git clone https://github.com/jordyorel/orus-lang.git
cd orus-lang
make clean && make
```

### **Try the REPL**
```bash
./orus
```
```
orus> let x = 42
orus> print("Answer: ", x)
Answer: 42
```

### **Run a Program**
```bash
echo 'print("Hello, Orus!")' > hello.orus
./orus hello.orus
```

### **Development Mode**
```bash
./orus --trace program.orus    # Trace VM execution
./orus --debug program.orus    # Enable debugging
```

---

### 🔄 **In Development**
- 🔄 Struct definitions and methods (foundation in place)
- 🔄 Pattern matching and enums (type system ready)
- 🔄 Arrays and collections (type inference complete)
- 🔄 Generic system completion (Hindley-Milner foundation implemented)

### 🔮 **Planned Features**
- 📅 Module system and imports
- 📅 Standard library expansion
- 📅 Advanced GC optimizations (generational, concurrent)
- 📅 Enhanced error handling and diagnostics
- 📅 Trait system and advanced type features

---

### Comprehensive Benchmarking
```bash
make benchmark
```
---

## 📦 Versioning

Orus follows [Semantic Versioning 2.0.0](docs/VERSIONING.md) to clearly
communicate API stability and compatibility. The current release is
`v0.2.2`, which reflects major advances in the type system including:

- **v0.2.2**: Advanced Hindley-Milner type inference system
- **v0.2.0**: Functions and control flow implementation  
- **v0.1.0**: Basic VM and language foundations

The language is in active development with a solid foundation for building advanced features.
