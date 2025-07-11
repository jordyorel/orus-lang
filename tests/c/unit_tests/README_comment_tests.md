# Comment Testing Documentation

This directory contains comprehensive tests for comment parsing in the Orus language lexer.

## Test Files

### `test_indented_comments.c`
Tests basic indented comment functionality:
- Line comments at various indentation levels
- Block comments with indentation
- Comment-only lines not affecting indentation context
- Mixed comments and code within blocks

### `test_comment_edge_cases.c`
Tests advanced comment scenarios and edge cases:

#### **Basic Comment Scenarios**
- Comments at the end of lines
- Multiple consecutive comment lines
- Empty comments (`//`)
- Comments with special characters and quotes

#### **Block Comment Features**
- Multi-line block comments
- Nested block comments (`/* outer /* inner */ outer */`)
- Unterminated block comments
- Block comments between tokens

#### **Indentation and Whitespace**
- Comments at very deep indentation levels
- Comments with mixed tabs and spaces
- Comments not affecting indentation tracking

#### **Parser Integration**
- Comments mixed with division operators (`/`)
- Comments that could be confused with other tokens
- Comments preserving proper token sequence

## Key Features Tested

### 1. **Nested Block Comments**
The lexer properly handles nested `/* */` comments by tracking nesting levels:
```orus
/* Outer comment /* inner comment */ still in outer */
```

### 2. **Indentation Independence**
Comment-only lines don't affect the indentation stack:
```orus
if true:
    // This comment doesn't create an indent level
    x = 1  // Proper indentation is tracked here
```

### 3. **Comment Placement**
Comments can appear anywhere and are properly skipped:
```orus
x/*comment*/=/*comment*/1/*comment*/+/*comment*/2
```

### 4. **Multi-line Comments**
Block comments spanning multiple lines preserve line/column tracking:
```orus
/* This comment
   spans multiple
   lines */
```

## Test Coverage

- **75 individual test assertions** across all edge cases
- **40 test assertions** for basic indented comment functionality
- **Total: 115+ test assertions** ensuring robust comment parsing

## Running the Tests

```bash
cd tests/c/unit_tests

# Basic indented comments
gcc -o test_indented_comments test_indented_comments.c ../../../src/compiler/lexer.c -I../../../ -I../framework
./test_indented_comments

# Comment edge cases  
gcc -o test_comment_edge_cases test_comment_edge_cases.c ../../../src/compiler/lexer.c -I../../../ -I../framework
./test_comment_edge_cases
```

## Implementation Details

The comment parsing logic is implemented in `/src/compiler/lexer.c`:

- **Line comments**: `skip_whitespace()` function handles `//` comments
- **Block comments**: Nested comment support with nesting level tracking
- **Indentation**: Comment-only lines are treated as blank lines for indentation purposes

## Edge Cases Covered

1. **Nested Comments**: Full support for arbitrary nesting depth
2. **Unterminated Comments**: Graceful handling (consumes to EOF)
3. **Comment Interference**: Comments don't break token sequences  
4. **Whitespace Mixing**: Tabs and spaces in comment indentation
5. **Special Characters**: All ASCII characters supported in comments
6. **Empty Comments**: Minimal `//` and `/* */` cases handled
7. **Multi-line Tracking**: Line and column numbers maintained through comments

This comprehensive test suite ensures the Orus lexer handles all practical comment scenarios correctly while maintaining proper indentation-based parsing.