#include "../../../include/errors/features/type_errors.h"
#include "../../../include/errors/error_interface.h"
#include <string.h>

// Type error definitions with friendly messages
static const FeatureErrorInfo type_errors[] = {
    {
        .code = E2001_TYPE_MISMATCH,
        .title = "This value isn't what we expected",
        .help = "You can convert between types using conversion functions if appropriate.",
        .note = "Different types can't be mixed directly for safety reasons."
    },
    {
        .code = E2002_INCOMPATIBLE_TYPES,
        .title = "These types don't work together",
        .help = "Check if you meant to use the same type for both values.",
        .note = "Type compatibility ensures your program behaves predictably."
    },
    {
        .code = E2003_UNDEFINED_TYPE,
        .title = "This type doesn't exist",
        .help = "Check the spelling or make sure the type is available in this scope.",
        .note = "Orus supports: i32, i64, u32, u64, f64, bool, string."
    },
    {
        .code = E2004_MIXED_ARITHMETIC,
        .title = "Can't mix these number types directly",
        .help = "Use explicit conversion like (value as i64) or (value as f64) to make your intent clear.",
        .note = "Explicit type conversion prevents accidental precision loss."
    },
    {
        .code = E2005_INVALID_CAST,
        .title = "Can't convert to this type",
        .help = "Check if this conversion is supported, or try a different approach.",
        .note = "Some type conversions aren't allowed to prevent data loss or confusion."
    },
    {
        .code = E2006_TYPE_ANNOTATION_REQUIRED,
        .title = "Need to specify the type here",
        .help = "Add a type annotation like ': i32' to clarify what type you want.",
        .note = "Type annotations help both you and the compiler understand your intent."
    },
    {
        .code = E2007_UNSUPPORTED_OPERATION,
        .title = "This operation isn't supported for this type",
        .help = "Check the documentation for supported operations on this type.",
        .note = "Different types support different operations for safety and clarity."
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

// Get type-specific error suggestions
const char* get_type_error_suggestion(ErrorCode code, const char* context) {
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
        default:
            return "Check the Orus documentation for type system rules.";
    }
}

// Check if error code is a type error
bool is_type_error(ErrorCode code) {
    return code >= 2000 && code <= 2999;
}