# 🚀 Feature-Based Error Organization Proposal

## Current State Analysis

Orus currently has a good foundation with categorized error codes:
- **E0xxx**: Runtime errors (division by zero, overflow, etc.)
- **E1xxx**: Syntax errors (missing colon, invalid syntax, etc.)  
- **E2xxx**: Type errors (mismatch, invalid cast, etc.)
- **E3xxx**: Module errors (import failures, etc.)
- **E9xxx**: Internal errors (compiler bugs, etc.)

**Problem**: All error messages are in one large `error_reporting.c` file with giant switch statements.

## 🎯 Proposed Architecture

### **Directory Structure**
```
src/
└── errors/
    ├── core/
    │   ├── error_base.c         # Core error reporting infrastructure
    │   ├── error_format.c       # Formatting and display logic
    │   └── error_registry.c     # Error code registry and lookup
    ├── features/
    │   ├── type_errors.c        # E2xxx - All type system errors
    │   ├── syntax_errors.c      # E1xxx - Parser and syntax errors
    │   ├── runtime_errors.c     # E0xxx - VM runtime errors
    │   ├── module_errors.c      # E3xxx - Import/module errors
    │   └── internal_errors.c    # E9xxx - Compiler internal errors
    └── utils/
        ├── error_suggestions.c  # Smart suggestions and fixes
        └── error_context.c      # Source context and highlighting
```

### **Header Structure**
```
include/
└── errors/
    ├── error_types.h           # ErrorCode enum and base types
    ├── error_interface.h       # Public API for error reporting
    └── features/
        ├── type_errors.h       # Type error function declarations
        ├── syntax_errors.h     # Syntax error function declarations
        ├── runtime_errors.h    # Runtime error function declarations
        ├── module_errors.h     # Module error function declarations
        └── internal_errors.h   # Internal error function declarations
```

## 🏗️ Implementation Strategy

### **Phase 1: Core Infrastructure**
1. **Create base error infrastructure** (`error_base.c`)
2. **Extract formatting logic** (`error_format.c`) 
3. **Build error registry system** (`error_registry.c`)

### **Phase 2: Feature Modules**
1. **Start with type errors** (most recent work)
2. **Migrate syntax errors** (parser-related)
3. **Move runtime errors** (VM-related)
4. **Organize module errors** (import system)
5. **Isolate internal errors** (compiler bugs)

### **Phase 3: Enhanced Features**
1. **Smart suggestions** (`error_suggestions.c`)
2. **Rich context** (`error_context.c`)
3. **Error recovery** (continue after errors)

## 🔧 Benefits

### **For Developers**
- **Easier maintenance**: Find type errors in `type_errors.c`
- **Cleaner code**: Smaller, focused files instead of giant switches
- **Better testing**: Test each feature's errors independently
- **Faster builds**: Only recompile changed error modules

### **For Users**  
- **Consistent experience**: Each feature has unified error style
- **Better messages**: Feature experts can craft better errors
- **Smarter suggestions**: Context-aware help for each feature
- **Progressive enhancement**: Add better errors per feature over time

## 🎨 Example: Type Errors Module

```c
// src/errors/features/type_errors.c
#include "../../include/errors/features/type_errors.h"
#include "../../include/errors/error_interface.h"

// Type error registry
static const TypeErrorInfo type_errors[] = {
    {
        .code = E2001_TYPE_MISMATCH,
        .title = "This value isn't what we expected",
        .help = "You can convert between types using conversion functions if appropriate.",
        .note = "Different types can't be mixed directly for safety reasons."
    },
    {
        .code = E2004_MIXED_ARITHMETIC, 
        .title = "Can't mix these number types directly",
        .help = "Use explicit conversion like (value as i64) or (value as f64) to make your intent clear.",
        .note = "Explicit type conversion prevents accidental precision loss."
    },
    // ... more type errors
};

// Feature-specific error reporting
ErrorResult report_type_mismatch(SrcLocation location, const char* expected, const char* found) {
    return report_feature_error(E2001_TYPE_MISMATCH, location, expected, found);
}

ErrorResult report_mixed_arithmetic(SrcLocation location, const char* left_type, const char* right_type) {
    return report_feature_error(E2004_MIXED_ARITHMETIC, location, left_type, right_type);
}

// Feature initialization
void init_type_errors(void) {
    register_error_category("TYPE", type_errors, ARRAY_SIZE(type_errors));
}
```

## 🔄 Migration Plan

### **Step 1**: Create infrastructure (1-2 hours)
- Set up directory structure
- Create base error system
- Define interface contracts

### **Step 2**: Migrate type errors (30 minutes)
- Extract all E2xxx errors to `type_errors.c`
- Update compiler to use new type error functions
- Test type error messages still work

### **Step 3**: Migrate other features (1 hour each)
- Syntax errors → `syntax_errors.c`
- Runtime errors → `runtime_errors.c`  
- Module errors → `module_errors.c`
- Internal errors → `internal_errors.c`

### **Step 4**: Enhance and optimize (ongoing)
- Add smart suggestions per feature
- Improve error context
- Add error recovery mechanisms

## 🎉 Inspiration from Best Practices

This design combines the best aspects of:
- **Rust's modular error system**: Feature-based organization
- **TypeScript's diagnostic categories**: Rich error information
- **Python's exception hierarchy**: Clear error relationships
- **Java's package structure**: Logical grouping and namespacing

The result is a maintainable, scalable error system that grows with Orus while providing excellent user experience.