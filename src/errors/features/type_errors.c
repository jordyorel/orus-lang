#include "errors/features/type_errors.h"
#include "errors/error_interface.h"
#include <string.h>

// Type error definitions with friendly messages
static const FeatureErrorInfo type_errors[] = {
    {
        .code = E2001_TYPE_MISMATCH,
        .title = "Type mismatch found",
        .help = "Try converting one value to match the other's type, like `x as string` or `y as i32`.",
        .note = "Orus keeps types separate to help prevent unexpected behavior and bugs."
    },
    {
        .code = E2002_INCOMPATIBLE_TYPES,
        .title = "Types need to be compatible",
        .help = "Consider converting one value to match the other, or check if you meant to use the same type.",
        .note = "Type compatibility helps your program behave predictably."
    },
    {
        .code = E2003_UNDEFINED_TYPE,
        .title = "Type not recognized",
        .help = "Double-check the spelling, or choose from Orus's available types below.",
        .note = "Available types: i32, i64, u32, u64, f64, bool, string."
    },
    {
        .code = E2004_MIXED_ARITHMETIC,
        .title = "Number types need alignment",
        .help = "Use explicit conversion like (value as i64) or (value as f64) to make your intent clear.",
        .note = "Explicit type conversion helps prevent unexpected precision loss."
    },
    {
        .code = E2005_INVALID_CAST,
        .title = "This conversion isn't supported",
        .help = "Try a different conversion approach, or check if there's a safer way to transform this value.",
        .note = "Orus prevents certain conversions to help protect your data from loss or corruption."
    },
    {
        .code = E2006_TYPE_ANNOTATION_REQUIRED,
        .title = "Type annotation needed",
        .help = "Add a type annotation like ': i32' to help Orus understand what type you want.",
        .note = "Type annotations make your code clearer for both you and Orus."
    },
    {
        .code = E2007_UNSUPPORTED_OPERATION,
        .title = "This operation needs a different approach",
        .help = "Try a different operation, or check what operations work with this type.",
        .note = "Each type supports specific operations to keep your code safe and clear."
    },
    {
        .code = E2008_IMMUTABLE_ASSIGNMENT,
        .title = "This variable is protected from changes",
        .help = "To modify this variable, declare it as mutable with 'mut' when you first create it: 'mut variable_name = value'",
        .note = "Variables are immutable by default to help prevent bugs and make your code more predictable."
    }
};

// Initialize type errors
ErrorReportResult init_type_errors(void) {
    return register_error_category("TYPE", type_errors, 
                                   sizeof(type_errors) / sizeof(type_errors[0]));
}

// Type mismatch error
ErrorReportResult report_type_mismatch(SrcLocation location, const char* expected, const char* found) {
    return report_feature_error(E2001_TYPE_MISMATCH, location, expected, found);
}

// Mixed arithmetic error
ErrorReportResult report_mixed_arithmetic(SrcLocation location, const char* left_type, const char* right_type) {
    return report_feature_error(E2004_MIXED_ARITHMETIC, location, left_type, right_type);
}

// Invalid cast error
ErrorReportResult report_invalid_cast(SrcLocation location, const char* target_type, const char* source_type) {
    return report_feature_error(E2005_INVALID_CAST, location, target_type, source_type);
}

// Undefined type error
ErrorReportResult report_undefined_type(SrcLocation location, const char* type_name) {
    return report_feature_error(E2003_UNDEFINED_TYPE, location, "valid type", type_name);
}

// Incompatible types error
ErrorReportResult report_incompatible_types(SrcLocation location, const char* left_type, const char* right_type) {
    return report_feature_error(E2002_INCOMPATIBLE_TYPES, location, left_type, right_type);
}

// Type annotation required error
ErrorReportResult report_type_annotation_required(SrcLocation location, const char* context) {
    return report_feature_error_f(E2006_TYPE_ANNOTATION_REQUIRED, location, 
                                 "Type annotation needed for %s", context);
}

// Unsupported operation error
ErrorReportResult report_unsupported_operation(SrcLocation location, const char* operation, const char* type) {
    return report_feature_error_f(E2007_UNSUPPORTED_OPERATION, location, 
                                 "Operation '%s' not supported for type '%s'", operation, type);
}

// Immutable assignment error
ErrorReportResult report_immutable_assignment(SrcLocation location, const char* variable_name) {
    return report_feature_error_f(E2008_IMMUTABLE_ASSIGNMENT, location, 
                                 "Variable '%s' is immutable and cannot be changed", variable_name);
}

// Get type-specific error suggestions
const char* get_type_error_suggestion(ErrorCode code, const char* context) {
    (void)context; // Unused parameter
    const FeatureErrorInfo* error = get_error_info(code);
    if (error && error->help) {
        return error->help;
    }
    
    // Fallback suggestions based on code
    switch (code) {
        case E2001_TYPE_MISMATCH:
            return "Consider using explicit type conversion with 'as'.";
        case E2004_MIXED_ARITHMETIC:
            return "Cast one of the values to match the other's type.";
        case E2005_INVALID_CAST:
            return "Check if this type conversion is allowed in Orus.";
        case E2008_IMMUTABLE_ASSIGNMENT:
            return "Try declaring the variable with 'mut' to make it changeable.";
        default:
            return "Check the Orus documentation for type system rules.";
    }
}

// Check if error code is a type error
bool is_type_error(ErrorCode code) {
    return code >= 2000 && code <= 2999;
}