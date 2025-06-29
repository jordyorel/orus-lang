# Contributing to Orus

We welcome contributions to the Orus programming language! This document provides guidelines for contributing.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/orus-lang.git`
3. Set up the development environment
4. Create a feature branch: `git checkout -b feature-name`

## Development Environment

### Prerequisites

- C compiler (GCC or Clang)
- CMake 3.16+ (recommended) or Make
- Python 3.6+ (for build scripts)
- Git

### Building

```bash
# Using CMake (recommended)
mkdir build && cd build
cmake ..
make

# Using legacy Makefile
make

# Using build script
./build.py build
```

### Testing

```bash
# Run all tests
./build.py test

# Or manually
make test
```

## Code Style

### C Code Style

We follow a consistent C style:

```c
// Function names: camelCase
void initCompiler(Compiler* compiler);

// Variables: camelCase
int registerCount = 0;

// Constants: UPPER_CASE
#define MAX_REGISTERS 256

// Types: PascalCase
typedef struct {
    int value;
} ValueType;

// Enums: UPPER_CASE with prefix
typedef enum {
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_EOF
} TokenType;
```

### Formatting

Use `clang-format` to format code:

```bash
# Format all code
./build.py format

# Or manually
make format
```

### Comments

- Use `//` for single-line comments
- Use `/* */` for multi-line comments
- Document all public functions
- Include examples for complex algorithms

```c
/**
 * Allocate a new register for temporary values.
 * 
 * @param compiler The compiler context
 * @return Register index, or 0 if allocation failed
 */
uint8_t allocateRegister(Compiler* compiler);
```

## Code Organization

### File Structure

- `src/core/`: Core data structures (AST, values, etc.)
- `src/compiler/`: Lexer, parser, and compiler
- `src/vm/`: Virtual machine implementation
- `include/`: Public header files
- `tests/`: Test files
- `docs/`: Documentation
- `examples/`: Example programs

### Header Files

- Keep headers minimal - only include what's needed
- Use include guards or `#pragma once`
- Forward declare when possible
- Document public APIs

### Naming Conventions

- Files: snake_case (e.g., `register_allocator.c`)
- Functions: camelCase (e.g., `compileExpression`)
- Variables: camelCase (e.g., `localCount`)
- Constants: UPPER_CASE (e.g., `MAX_LOCALS`)
- Types: PascalCase (e.g., `ASTNode`)

## Testing

### Writing Tests

- Add tests for new features
- Test edge cases and error conditions
- Keep tests focused and independent
- Use descriptive test names

Example test structure:
```c
void test_register_allocation() {
    // Setup
    Compiler compiler;
    initCompiler(&compiler);
    
    // Test
    uint8_t reg1 = allocateRegister(&compiler);
    uint8_t reg2 = allocateRegister(&compiler);
    
    // Verify
    assert(reg1 != reg2);
    assert(reg1 < REGISTER_COUNT);
    
    // Cleanup
    freeCompiler(&compiler);
}
```

### Test Categories

1. **Unit Tests**: Test individual functions
2. **Integration Tests**: Test component interactions
3. **Language Tests**: Test language features end-to-end
4. **Performance Tests**: Benchmark critical paths

## Documentation

### API Documentation

Document public functions with:
- Purpose and behavior
- Parameter descriptions
- Return value description
- Example usage
- Error conditions

### Architecture Documentation

- Explain design decisions
- Include diagrams where helpful
- Document algorithms and data structures
- Keep documentation up-to-date

## Submitting Changes

### Pull Request Process

1. Ensure all tests pass
2. Update documentation if needed
3. Add test cases for new features
4. Follow the commit message format
5. Submit pull request with clear description

### Commit Messages

Use conventional commits format:

```
type(scope): description

[optional body]

[optional footer]
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code formatting
- `refactor`: Code restructuring
- `test`: Adding tests
- `chore`: Build/tooling changes

Examples:
```
feat(compiler): add support for generic functions

Implements generic function declarations and calls with
type parameter inference and constraint checking.

Closes #123
```

### Code Review

All submissions require code review:
- Be respectful and constructive
- Focus on code quality and maintainability
- Test thoroughly before reviewing
- Provide specific, actionable feedback

## Bug Reports

### Before Reporting

1. Check existing issues
2. Test with latest version
3. Create minimal reproduction case

### Bug Report Template

```
**Description**
Brief description of the bug.

**To Reproduce**
Steps to reproduce the behavior:
1. Go to '...'
2. Click on '....'
3. See error

**Expected Behavior**
Clear description of what you expected to happen.

**Environment**
- OS: [e.g. macOS, Linux, Windows]
- Compiler: [e.g. GCC 11.2]
- Version: [e.g. v0.1.0]

**Additional Context**
Any other context about the problem.
```

## Feature Requests

### Template

```
**Is your feature request related to a problem?**
Clear description of the problem.

**Describe the solution you'd like**
Clear description of what you want to happen.

**Describe alternatives you've considered**
Other solutions you've considered.

**Additional context**
Any other context or screenshots.
```

## Performance Considerations

### Benchmarking

- Use consistent test environment
- Measure multiple runs
- Test on different platforms
- Document performance characteristics

### Optimization Guidelines

- Profile before optimizing
- Focus on hot paths
- Consider algorithmic improvements
- Maintain code readability

## Release Process

### Versioning

We use semantic versioning (SemVer):
- MAJOR: Breaking changes
- MINOR: New features (backwards compatible)
- PATCH: Bug fixes

### Release Checklist

1. Update version numbers
2. Update CHANGELOG
3. Run full test suite
4. Update documentation
5. Tag release
6. Build distribution packages

## Community

### Communication

- GitHub Issues for bugs and features
- GitHub Discussions for general questions
- Pull requests for code changes

### Code of Conduct

We follow a standard code of conduct:
- Be respectful and inclusive
- Focus on constructive feedback
- Help create a welcoming environment
- Report inappropriate behavior

## Getting Help

- Read the documentation
- Check existing issues
- Ask in GitHub Discussions
- Look at example code

Thank you for contributing to Orus!
