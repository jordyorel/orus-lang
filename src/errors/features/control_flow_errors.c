#include "errors/features/control_flow_errors.h"
#include "errors/error_interface.h"
#include <string.h>
#include <limits.h>

// Control flow error definitions with friendly, mentor-like messages
// Note: Reserved for future error infrastructure integration
static const FeatureErrorInfo control_flow_errors[] __attribute__((unused)) = {
    {
        .code = E1401_BREAK_OUTSIDE_LOOP,
        .title = "'break' can only be used inside a loop",
        .help = "Use 'break' inside a while loop, for loop, or other loop construct.",
        .note = "Break statements are used to exit loops early and have no meaning outside loop contexts."
    },
    {
        .code = E1402_CONTINUE_OUTSIDE_LOOP,
        .title = "'continue' can only be used inside a loop",
        .help = "Use 'continue' inside a while loop, for loop, or other loop construct.",
        .note = "Continue statements skip to the next iteration and only work inside loops."
    },
    {
        .code = E1403_NON_BOOLEAN_CONDITION,
        .title = "This condition isn't boolean",
        .help = "Use comparison operators like ==, !=, <, > to create boolean conditions.",
        .note = "Conditions in if/while statements must evaluate to true or false."
    },
    {
        .code = E1404_INVALID_RANGE_SYNTAX,
        .title = "This range syntax isn't valid",
        .help = "Use the format 'start..end' or 'start..end..step'. Example: 'for i in 0..10:' or 'for i in 0..10..2:'",
        .note = "Range syntax must follow Orus conventions for loop iteration."
    },
    {
        .code = E1405_MISSING_COLON,
        .title = "Missing colon after control statement",
        .help = "Add a colon (:) after control flow statements to indicate the start of a block.",
        .note = "Orus requires a colon after control flow statements like if, while, for."
    },
    {
        .code = E1406_UNREACHABLE_CODE,
        .title = "This code will never be reached",
        .help = "Remove this unreachable code or restructure your control flow.",
        .note = "Unreachable code may indicate logic errors or unnecessary complexity."
    },
    {
        .code = E1407_EMPTY_LOOP_BODY,
        .title = "This loop body is empty",
        .help = "Add some code inside the loop block, or use 'pass' if you need an empty block.",
        .note = "Empty blocks might indicate incomplete code or logic errors."
    },
    {
        .code = E1408_INFINITE_LOOP_DETECTED,
        .title = "This loop might run forever",
        .help = "Add a break condition or ensure the loop condition eventually becomes false.",
        .note = "Infinite loops can cause your program to hang or consume excessive resources."
    },
    {
        .code = E1409_INVALID_LOOP_VARIABLE,
        .title = "This loop variable isn't valid",
        .help = "Use a valid variable name for the loop variable following Orus naming conventions.",
        .note = "Loop variables must follow the same naming rules as regular variables."
    }
};

// ============================================================================
// Break/Continue Error Reporting
// ============================================================================

ErrorReportResult report_break_outside_loop(SrcLocation location) {
    return report_feature_error_f(E1401_BREAK_OUTSIDE_LOOP, location, 
        "'break' can only be used inside a loop");
}

ErrorReportResult report_continue_outside_loop(SrcLocation location) {
    return report_feature_error_f(E1402_CONTINUE_OUTSIDE_LOOP, location, 
        "'continue' can only be used inside a loop");
}

ErrorReportResult report_labeled_break_not_found(SrcLocation location, const char* label) {
    return report_feature_error_f(E1401_BREAK_OUTSIDE_LOOP, location, 
        "Label '%s' not found for break statement", label);
}

ErrorReportResult report_labeled_continue_not_found(SrcLocation location, const char* label) {
    return report_feature_error_f(E1402_CONTINUE_OUTSIDE_LOOP, location, 
        "Label '%s' not found for continue statement", label);
}

// ============================================================================
// Condition Error Reporting
// ============================================================================

ErrorReportResult report_non_boolean_condition(SrcLocation location, const char* condition_type, const char* statement_type) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "This %s condition is a `%s`, but boolean was expected", statement_type, condition_type);
}

ErrorReportResult report_empty_condition(SrcLocation location, const char* statement_type) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "Missing condition in %s statement", statement_type);
}

ErrorReportResult report_unreachable_condition(SrcLocation location, const char* reason) {
    return report_feature_error_f(E1406_UNREACHABLE_CODE, location, 
        "This condition will never be reached: %s", reason);
}

ErrorReportResult report_assignment_in_condition(SrcLocation location, const char* statement_type) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "Did you mean '==' instead of '=' in this %s condition? Assignment (=) used where comparison (==) was likely intended", statement_type);
}

// ============================================================================
// Loop Range Error Reporting
// ============================================================================

ErrorReportResult report_invalid_range_syntax(SrcLocation location, const char* range_text, const char* issue) {
    return report_feature_error_f(E1404_INVALID_RANGE_SYNTAX, location, 
        "Range '%s' has invalid syntax: %s", range_text, issue);
}

ErrorReportResult report_descending_range_without_step(SrcLocation location, int start, int end) {
    return report_feature_error_f(E1404_INVALID_RANGE_SYNTAX, location, 
        "Range %d..%d goes backwards but has no step specified", start, end);
}

ErrorReportResult report_zero_step_range(SrcLocation location) {
    return report_feature_error_f(E1404_INVALID_RANGE_SYNTAX, location, 
        "Range step cannot be zero");
}

ErrorReportResult report_infinite_range(SrcLocation location, const char* range_text) {
    return report_feature_error_f(E1408_INFINITE_LOOP_DETECTED, location, 
        "Range '%s' would create infinite loop", range_text);
}

ErrorReportResult report_range_overflow(SrcLocation location, const char* range_text) {
    return report_feature_error_f(E1404_INVALID_RANGE_SYNTAX, location, 
        "Range '%s' exceeds numeric limits", range_text);
}

// ============================================================================
// Syntax Error Reporting
// ============================================================================

ErrorReportResult report_missing_colon(SrcLocation location, const char* statement_type) {
    return report_feature_error_f(E1405_MISSING_COLON, location, 
        "Expected ':' after %s statement", statement_type);
}

ErrorReportResult report_missing_condition(SrcLocation location, const char* statement_type) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "Missing condition in %s statement", statement_type);
}

ErrorReportResult report_invalid_indentation(SrcLocation location, const char* statement_type, int expected, int found) {
    return report_feature_error_f(E1405_MISSING_COLON, location, 
        "Incorrect indentation in %s block: expected %d spaces, found %d", statement_type, expected, found);
}

ErrorReportResult report_empty_block(SrcLocation location, const char* statement_type) {
    return report_feature_error_f(E1407_EMPTY_LOOP_BODY, location, 
        "Empty %s block", statement_type);
}

// ============================================================================
// Loop Variable Error Reporting
// ============================================================================

ErrorReportResult report_invalid_loop_variable(SrcLocation location, const char* variable_name, const char* issue) {
    return report_feature_error_f(E1409_INVALID_LOOP_VARIABLE, location, 
        "Invalid loop variable '%s': %s", variable_name, issue);
}

ErrorReportResult report_loop_variable_redeclaration(SrcLocation location, const char* variable_name) {
    return report_feature_error_f(E1409_INVALID_LOOP_VARIABLE, location, 
        "Loop variable '%s' shadows existing variable", variable_name);
}

ErrorReportResult report_loop_variable_type_mismatch(SrcLocation location, const char* variable_name, const char* expected_type, const char* found_type) {
    return report_feature_error_f(E1409_INVALID_LOOP_VARIABLE, location, 
        "Loop variable '%s' expected %s, found %s", variable_name, expected_type, found_type);
}

// ============================================================================
// Flow Control Analysis
// ============================================================================

ErrorReportResult report_unreachable_code(SrcLocation location, const char* reason) {
    return report_feature_error_f(E1406_UNREACHABLE_CODE, location, 
        "Unreachable code detected: %s", reason);
}

ErrorReportResult report_dead_code_after_break(SrcLocation location) {
    return report_feature_error_f(E1406_UNREACHABLE_CODE, location, 
        "Code after 'break' will never execute");
}

ErrorReportResult report_dead_code_after_continue(SrcLocation location) {
    return report_feature_error_f(E1406_UNREACHABLE_CODE, location, 
        "Code after 'continue' will never execute");
}

ErrorReportResult report_infinite_loop_warning(SrcLocation location, const char* loop_type) {
    return report_feature_error_f(E1408_INFINITE_LOOP_DETECTED, location, 
        "Potential infinite %s loop detected", loop_type);
}

// ============================================================================
// Control Flow Validation
// ============================================================================

ErrorReportResult report_missing_else_branch(SrcLocation location, const char* suggestion) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "Consider adding an else branch: %s", suggestion);
}

ErrorReportResult report_duplicate_else_clause(SrcLocation location) {
    return report_feature_error_f(E1405_MISSING_COLON, location, 
        "Duplicate else clause - only one else clause is allowed per if statement");
}

ErrorReportResult report_elif_after_else(SrcLocation location) {
    return report_feature_error_f(E1405_MISSING_COLON, location, 
        "'elif' cannot come after 'else' - move all elif clauses before else");
}

ErrorReportResult report_standalone_else(SrcLocation location) {
    return report_feature_error_f(E1405_MISSING_COLON, location, 
        "'else' clause found without a preceding 'if' statement");
}

// ============================================================================
// Advanced Control Flow Errors
// ============================================================================

ErrorReportResult report_nested_loop_depth_exceeded(SrcLocation location, int max_depth) {
    return report_feature_error_f(E1408_INFINITE_LOOP_DETECTED, location, 
        "Nested loop depth exceeds maximum (%d)", max_depth);
}

ErrorReportResult report_complex_condition_warning(SrcLocation location, const char* suggestion) {
    return report_feature_error_f(E1403_NON_BOOLEAN_CONDITION, location, 
        "Complex condition detected - consider simplifying: %s", suggestion);
}

ErrorReportResult report_loop_performance_warning(SrcLocation location, const char* issue, const char* suggestion) {
    return report_feature_error_f(E1408_INFINITE_LOOP_DETECTED, location, 
        "Performance issue: %s - consider: %s", issue, suggestion);
}

// ============================================================================
// Validation Helpers
// ============================================================================

// Note: These would need to integrate with the compiler's loop context tracking
bool is_valid_loop_context(void) {
    // This would check the compiler's loop depth or context stack
    // For now, return true as a placeholder
    return true;
}

bool is_valid_break_continue_context(void) {
    // This would check if we're inside a loop that allows break/continue
    // For now, always return true to avoid blocking valid break/continue in loops
    // TODO: Implement proper loop context tracking
    return true;
}

bool is_boolean_expression_type(const char* type_name) {
    return strcmp(type_name, "bool") == 0;
}

bool is_valid_range_bounds(int start, int end, int step) {
    if (step == 0) return false;
    if (step > 0 && start >= end) return false;
    if (step < 0 && start <= end) return false;
    
    // Check for potential overflow
    if (step > 0) {
        if (end > start && (INT_MAX - start) / step < (end - start)) return false;
    } else {
        if (start > end && (start - INT_MIN) / (-step) < (start - end)) return false;
    }
    
    return true;
}

const char* get_control_flow_suggestion(const char* error_type) {
    if (strcmp(error_type, "break_outside_loop") == 0) {
        return "Use 'break' inside a while or for loop";
    } else if (strcmp(error_type, "continue_outside_loop") == 0) {
        return "Use 'continue' inside a while or for loop";
    } else if (strcmp(error_type, "non_boolean_condition") == 0) {
        return "Use comparison operators like ==, !=, <, > to create boolean conditions";
    } else if (strcmp(error_type, "missing_colon") == 0) {
        return "Add a colon (:) after control flow statements";
    } else if (strcmp(error_type, "invalid_range") == 0) {
        return "Use format: start..end or start..end..step";
    }
    return "Check the documentation for correct syntax";
}