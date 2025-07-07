# Orus Language Comment Tests

This directory contains comprehensive test files for comment functionality in the Orus programming language.

## Test Files Overview

### 1. `basic_comments.orus`
Tests fundamental comment features:
- Line comments (`//`)
- Comments at start of line, end of line, and standalone
- Basic comment parsing and skipping

**Expected Output**: `3`

### 2. `block_comments.orus`
Tests block comment functionality:
- Basic block comments (`/* */`)
- Multi-line block comments
- Block comments with special characters
- Inline block comments

**Expected Output**: `6`

### 3. `indented_comments.orus`
Tests that comments work correctly within indented code blocks:
- Comments between control structures
- Comments at different indentation levels
- Comments not affecting program flow

**Expected Output**: 
```
positive
y positive
done
```

### 4. `nested_block_comments.orus`
Tests multiple separate block comments:
- Sequential block comments
- Multi-line block comments with complex content
- Block comments between statements

**Expected Output**: `6`

**Note**: True nested block comments (`/* outer /* inner */ outer */`) are not currently supported in the Orus lexer.

### 5. `comments_with_indentation.orus`
Tests comment behavior within indented control structures:
- Comments in nested if statements
- Block comments between statements in indented blocks
- Proper indentation handling with comments

**Expected Output**:
```
6
done
finished
```

### 6. `comments_in_expressions.orus`
Tests comments within and around expressions:
- Comments after variable assignments
- Comments between operators
- Block comments in complex expressions
- Comments in arithmetic operations

**Expected Output**:
```
21
18
```

### 7. `empty_and_minimal_comments.orus`
Tests edge cases with minimal comment content:
- Empty line comments (`//`)
- Empty block comments (`/* */` and `/**/`)
- Multiple consecutive empty comments
- Comments with no content

**Expected Output**: `6`

### 8. `comments_with_special_chars.orus`
Tests comments containing various special characters:
- Comments with quotes and apostrophes
- Comments with symbols and punctuation
- Comments with number and letter sequences
- Comments with escape-like sequences
- Comments with URL-like and path-like text

**Expected Output**: `15`

### 9. `comments_in_control_flow.orus`
Tests comments within various control flow structures:
- Comments around if/elif/else statements
- Comments in for and while loops
- Comments with break and continue statements
- Comments in nested control structures

**Expected Output**:
```
positive
0
1
2
0
1
2
0
1
```

### 10. `mixed_comment_types.orus`
Tests mixing line and block comments:
- Line comments followed by block comments
- Block comments followed by line comments
- Multiple comment types in single expressions
- Traditional C-style commented sections
- Comment dividers and headers

**Expected Output**:
```
3
6
```

### 11. `deeply_nested_comments.orus`
Tests complex multi-line block comments:
- Large block comments with detailed content
- Multiple separate block comments
- Comments with varied indentation
- Multi-line comments with complex formatting

**Expected Output**: `6`

## Running the Tests

To run all comment tests:

```bash
# Individual tests
./orus tests/comments/basic_comments.orus
./orus tests/comments/block_comments.orus
./orus tests/comments/indented_comments.orus
./orus tests/comments/nested_block_comments.orus
./orus tests/comments/comments_with_indentation.orus
./orus tests/comments/comments_in_expressions.orus
./orus tests/comments/empty_and_minimal_comments.orus
./orus tests/comments/comments_with_special_chars.orus
./orus tests/comments/comments_in_control_flow.orus
./orus tests/comments/mixed_comment_types.orus
./orus tests/comments/deeply_nested_comments.orus

# Or run all at once
for file in tests/comments/*.orus; do
    echo "Testing $file"
    ./orus "$file"
    echo "---"
done
```

## Comment Features Tested

### ✅ **Supported Features**
- **Line comments**: `// comment text`
- **Block comments**: `/* comment text */`
- **Multi-line block comments**: Comments spanning multiple lines
- **Comments in expressions**: Comments between operators and operands
- **Comments with special characters**: All ASCII characters supported
- **Empty comments**: Minimal `//` and `/* */` cases
- **Mixed comment types**: Line and block comments together
- **Comments in control flow**: Comments within if/for/while structures
- **Indentation independence**: Comments don't affect indentation parsing

### ❌ **Unsupported Features**
- **Nested block comments**: `/* outer /* inner */ outer */` not supported
- **Comments inside indented blocks**: Some edge cases with indented comments cause parsing issues

### 🔍 **Edge Cases Covered**
- Empty and minimal comments
- Comments with quotes, symbols, and special characters
- Comments at various indentation levels
- Comments between tokens in expressions
- Comments in complex control flow structures
- Multi-line comments with varied formatting

## Implementation Notes

The comment parsing is handled in the Orus lexer (`src/compiler/lexer.c`):
- Line comments are processed in `skip_whitespace()` function
- Block comments are also handled in `skip_whitespace()` function
- Comments are completely skipped during tokenization
- Comment-only lines are treated as blank lines for indentation purposes

These tests ensure the Orus language handles all practical comment scenarios correctly while maintaining proper program execution and indentation-based parsing.