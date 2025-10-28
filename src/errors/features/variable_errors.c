//  Orus Language Project

#include "errors/features/variable_errors.h"
#include "errors/error_interface.h"
#include <string.h>
#include <ctype.h>
#include <limits.h>

// Variable error definitions with friendly, mentor-like messages
static const FeatureErrorInfo variable_errors[] = {
    {
        .code = E1010_UNDEFINED_VARIABLE,
        .title = "Can't find this variable",
        .help = "Make sure you've declared the variable before using it, or check the spelling.",
        .note = "Variables must be declared before they can be used in Orus."
    },
    {
        .code = E1011_VARIABLE_REDEFINITION,
        .title = "This variable name is already taken",
        .help = "Choose a different name for this variable, or use assignment to change the existing one.",
        .note = "Each variable can only be declared once in the same scope."
    },
    {
        .code = E1012_SCOPE_VIOLATION,
        .title = "This variable isn't available here",
        .help = "Check if the variable is declared in the current scope or an outer scope.",
        .note = "Variables are only accessible within their scope and inner scopes."
    },
    {
        .code = E1013_INVALID_VARIABLE_NAME,
        .title = "This isn't a valid variable name",
        .help = "Variable names should start with a letter or underscore, followed by letters, numbers, or underscores.",
        .note = "Good variable names are descriptive and follow snake_case convention."
    },
    {
        .code = E1014_MUTABLE_REQUIRED,
        .title = "This variable needs to be mutable",
        .help = "Add 'mut' before the variable name when declaring it: 'mut variable_name = value'",
        .note = "Variables are immutable by default in Orus for safety and predictability."
    },
    {
        .code = E1015_INVALID_MULTIPLE_DECLARATION,
        .title = "Something's wrong with this variable declaration",
        .help = "Check the syntax for multiple variable declarations: 'var1 = value1, var2 = value2'",
        .note = "Multiple variables can be declared on one line, separated by commas."
    },
    {
        .code = E1016_LOOP_VARIABLE_MODIFICATION,
        .title = "Loop variables can't be modified inside the loop",
        .help = "Use a different variable inside the loop, or restructure your logic.",
        .note = "Loop variables are automatically managed and shouldn't be changed manually."
    },
    {
        .code = E1017_IMMUTABLE_COMPOUND_ASSIGNMENT,
        .title = "Can't use compound assignment on immutable variables",
        .help = "Declare the variable as mutable with 'mut' if you need to modify it: 'mut var = value'",
        .note = "Compound assignments like += require the variable to be mutable."
    },
    {
        .code = E1018_VARIABLE_NOT_INITIALIZED,
        .title = "This variable hasn't been given a value yet",
        .help = "Initialize the variable with a value when declaring it: 'variable_name = value'",
        .note = "All variables must be initialized when declared in Orus."
    }
};

// Initialize variable errors
ErrorReportResult init_variable_errors(void) {
    return register_error_category("VARIABLE", variable_errors, 
                                   sizeof(variable_errors) / sizeof(variable_errors[0]));
}

// === Core Variable Errors ===

ErrorReportResult report_undefined_variable(SrcLocation location, const char* variable_name) {
    return report_feature_error_f(E1010_UNDEFINED_VARIABLE, location, 
                                 "Undefined variable '%s'. Variables must be declared before use.", 
                                 variable_name);
}

ErrorReportResult report_variable_redefinition(SrcLocation location, const char* variable_name, int previous_line) {
    if (previous_line > 0) {
        return report_feature_error_f(E1011_VARIABLE_REDEFINITION, location, 
                                     "Variable '%s' is already defined on line %d", 
                                     variable_name, previous_line);
    } else {
        return report_feature_error_f(E1011_VARIABLE_REDEFINITION, location, 
                                     "Variable '%s' is already defined in this scope", 
                                     variable_name);
    }
}

ErrorReportResult report_scope_violation(SrcLocation location, const char* variable_name, const char* scope_context) {
    if (scope_context) {
        return report_feature_error_f(E1012_SCOPE_VIOLATION, location, 
                                     "Variable '%s' is not accessible here. It's only available %s", 
                                     variable_name, scope_context);
    } else {
        return report_feature_error_f(E1012_SCOPE_VIOLATION, location, 
                                     "Variable '%s' is not in scope at this location", 
                                     variable_name);
    }
}

// === Mutability Errors ===

ErrorReportResult report_immutable_variable_assignment(SrcLocation location, const char* variable_name) {
    return report_feature_error_f(E1014_MUTABLE_REQUIRED, location, 
                                 "Cannot assign to immutable variable '%s'. Add 'mut' when declaring it.", 
                                 variable_name);
}

ErrorReportResult report_immutable_compound_assignment(SrcLocation location, const char* variable_name, const char* operator) {
    return report_feature_error_f(E1017_IMMUTABLE_COMPOUND_ASSIGNMENT, location, 
                                 "Cannot use '%s' on immutable variable '%s'. Declare it as 'mut %s = ...' instead.", 
                                 operator, variable_name, variable_name);
}

ErrorReportResult report_mutable_required(SrcLocation location, const char* variable_name, const char* operation) {
    return report_feature_error_f(E1014_MUTABLE_REQUIRED, location, 
                                 "Operation '%s' requires variable '%s' to be mutable. Add 'mut' when declaring it.", 
                                 operation, variable_name);
}

// === Declaration Errors ===

ErrorReportResult report_invalid_variable_name(SrcLocation location, const char* variable_name, const char* reason) {
    if (reason) {
        return report_feature_error_f(E1013_INVALID_VARIABLE_NAME, location, 
                                     "Invalid variable name '%s': %s", 
                                     variable_name, reason);
    } else {
        return report_feature_error_f(E1013_INVALID_VARIABLE_NAME, location, 
                                     "Invalid variable name '%s'", 
                                     variable_name);
    }
}

ErrorReportResult report_invalid_multiple_declaration(SrcLocation location, const char* variable_name, const char* issue) {
    return report_feature_error_f(E1015_INVALID_MULTIPLE_DECLARATION, location, 
                                 "Invalid multiple declaration for '%s': %s", 
                                 variable_name, issue);
}

ErrorReportResult report_variable_not_initialized(SrcLocation location, const char* variable_name) {
    return report_feature_error_f(E1018_VARIABLE_NOT_INITIALIZED, location, 
                                 "Variable '%s' must be initialized when declared", 
                                 variable_name);
}

// === Loop Variable Errors ===

ErrorReportResult report_loop_variable_modification(SrcLocation location, const char* variable_name, const char* loop_type) {
    return report_feature_error_f(E1016_LOOP_VARIABLE_MODIFICATION, location, 
                                 "Cannot modify %s loop variable '%s' inside the loop", 
                                 loop_type, variable_name);
}

// === Utility Functions ===

const char* get_variable_error_suggestion(ErrorCode code, const char* context) {
    (void)context; // Unused parameter
    const FeatureErrorInfo* error = get_error_info(code);
    if (error && error->help) {
        return error->help;
    }
    
    // Fallback suggestions based on code
    switch (code) {
        case E1010_UNDEFINED_VARIABLE:
            return "Check spelling or make sure the variable is declared before use.";
        case E1011_VARIABLE_REDEFINITION:
            return "Use a different name or assign to the existing variable.";
        case E1012_SCOPE_VIOLATION:
            return "Move the variable declaration to a broader scope if needed.";
        case E1013_INVALID_VARIABLE_NAME:
            return "Use letters, numbers, and underscores. Start with a letter or underscore.";
        case E1014_MUTABLE_REQUIRED:
        case E1017_IMMUTABLE_COMPOUND_ASSIGNMENT:
            return "Add 'mut' when declaring the variable to make it changeable.";
        case E1015_INVALID_MULTIPLE_DECLARATION:
            return "Check syntax: var1 = value1, var2 = value2";
        case E1016_LOOP_VARIABLE_MODIFICATION:
            return "Use a different variable name inside the loop.";
        case E1018_VARIABLE_NOT_INITIALIZED:
            return "Provide an initial value: variable_name = value";
        default:
            return "Check the Orus documentation for variable declaration rules.";
    }
}

bool is_variable_error(ErrorCode code) {
    return (code >= E1010_UNDEFINED_VARIABLE && code <= E1018_VARIABLE_NOT_INITIALIZED);
}

const char* get_variable_scope_info(const char* variable_name, int current_scope) {
    (void)variable_name; // Unused parameter
    (void)current_scope; // Unused parameter
    
    // This could be enhanced to provide more specific scope information
    // based on the symbol table and current compilation context
    return "in the current scope or an outer scope";
}

// Simple string distance calculation for variable name suggestions
static int levenshtein_distance(const char* s1, const char* s2) {
    int len1 = strlen(s1);
    int len2 = strlen(s2);
    
    if (len1 == 0) return len2;
    if (len2 == 0) return len1;
    
    // For simplicity, just check if first characters match
    // A full implementation would use dynamic programming
    if (s1[0] == s2[0]) {
        return 0; // Close enough for suggestion
    }
    
    return len1 + len2; // Very different
}

const char* suggest_variable_name(const char* wrong_name, const char** available_vars, size_t count) {
    if (!wrong_name || !available_vars || count == 0) {
        return NULL;
    }
    
    const char* best_match = NULL;
    int best_distance = INT_MAX;
    
    for (size_t i = 0; i < count; i++) {
        if (available_vars[i]) {
            int distance = levenshtein_distance(wrong_name, available_vars[i]);
            if (distance < best_distance && distance <= 2) { // Only suggest close matches
                best_distance = distance;
                best_match = available_vars[i];
            }
        }
    }
    
    return best_match;
}

// Check if variable name follows Orus naming conventions (public function)
bool is_valid_variable_name(const char* name) {
    if (!name || *name == '\0') {
        return false;
    }

    // Must start with letter or underscore
    if (!isalpha(*name) && *name != '_') {
        return false;
    }
    
    // Rest must be letters, digits, or underscores
    for (const char* p = name + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return false;
        }
    }
    
    return true;
}

// Get specific reason why a variable name is invalid
const char* get_variable_name_violation_reason(const char* name) {
    if (!name || *name == '\0') {
        return "name cannot be empty";
    }
    
    if (isdigit(*name)) {
        return "name cannot start with a digit";
    }
    
    if (!isalpha(*name) && *name != '_') {
        return "name must start with a letter or underscore";
    }
    
    for (const char* p = name + 1; *p; p++) {
        if (!isalnum(*p) && *p != '_') {
            return "name can only contain letters, digits, and underscores";
        }
    }

    return NULL; // Name is valid
}

bool is_valid_constant_name(const char* name) {
    if (!name || *name == '\0') {
        return false;
    }

    if (!isupper(*name)) {
        return false;
    }

    for (const char* p = name + 1; *p; p++) {
        if (!isupper(*p) && !isdigit(*p) && *p != '_') {
            return false;
        }
    }

    return true;
}

const char* get_constant_name_violation_reason(const char* name) {
    if (!name || *name == '\0') {
        return "module constants must have a non-empty name";
    }

    if (!isupper(*name)) {
        return "module constants must start with an uppercase letter";
    }

    for (const char* p = name + 1; *p; p++) {
        if (!isupper(*p) && !isdigit(*p) && *p != '_') {
            return "module constants must use SCREAMING_SNAKE_CASE (uppercase letters, digits, and underscores)";
        }
    }

    return NULL;
}
