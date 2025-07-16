#include "../../../include/errors/error_interface.h"
#include "../../../include/errors/error_types.h"
#include "../../../include/error_reporting.h"  // For backward compatibility
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Maximum number of error categories
#define MAX_ERROR_CATEGORIES 8
#define MAX_ERRORS_PER_CATEGORY 100

// Error registry for feature-based lookup
typedef struct {
    const char* category_name;
    const FeatureErrorInfo* errors;
    size_t error_count;
    ErrorFeature feature;
} ErrorCategory;

// Global error registry
static ErrorCategory error_registry[MAX_ERROR_CATEGORIES];
static size_t registered_categories = 0;
static bool error_system_initialized = false;

// Initialize the feature-based error system
ErrorReportResult init_feature_errors(void) {
    if (error_system_initialized) {
        return ERROR_REPORT_SUCCESS;
    }
    
    // Initialize the legacy error system for backward compatibility
    ErrorReportResult legacy_result = init_error_reporting();
    if (legacy_result != ERROR_REPORT_SUCCESS) {
        return legacy_result;
    }
    
    // Clear the registry
    memset(error_registry, 0, sizeof(error_registry));
    registered_categories = 0;
    
    error_system_initialized = true;
    return ERROR_REPORT_SUCCESS;
}

// Cleanup the error system
void cleanup_feature_errors(void) {
    if (!error_system_initialized) {
        return;
    }
    
    // Cleanup legacy system
    cleanup_error_reporting();
    
    // Clear registry
    memset(error_registry, 0, sizeof(error_registry));
    registered_categories = 0;
    error_system_initialized = false;
}

// Set source text for error context
ErrorReportResult set_error_source_text(const char* source, size_t length) {
    if (!error_system_initialized) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    // Delegate to legacy system for now
    return set_source_text(source, length);
}

// Register a feature's error category
ErrorReportResult register_error_category(const char* category_name, 
                                          const FeatureErrorInfo* errors, 
                                          size_t count) {
    if (!error_system_initialized) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    if (!category_name || !errors || count == 0) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    if (registered_categories >= MAX_ERROR_CATEGORIES) {
        return ERROR_REPORT_OUT_OF_MEMORY;
    }
    
    if (count > MAX_ERRORS_PER_CATEGORY) {
        return ERROR_REPORT_BUFFER_OVERFLOW;
    }
    
    // Determine feature type from category name
    ErrorFeature feature = ERROR_FEATURE_RUNTIME;
    if (strcmp(category_name, "TYPE") == 0) {
        feature = ERROR_FEATURE_TYPE;
    } else if (strcmp(category_name, "SYNTAX") == 0) {
        feature = ERROR_FEATURE_SYNTAX;
    } else if (strcmp(category_name, "MODULE") == 0) {
        feature = ERROR_FEATURE_MODULE;
    } else if (strcmp(category_name, "INTERNAL") == 0) {
        feature = ERROR_FEATURE_INTERNAL;
    }
    
    // Register the category
    ErrorCategory* category = &error_registry[registered_categories];
    category->category_name = category_name;
    category->errors = errors;
    category->error_count = count;
    category->feature = feature;
    
    registered_categories++;
    return ERROR_REPORT_SUCCESS;
}

// Get error information by code
const FeatureErrorInfo* get_error_info(ErrorCode code) {
    if (!error_system_initialized) {
        return NULL;
    }
    
    // Search through registered categories
    for (size_t i = 0; i < registered_categories; i++) {
        const ErrorCategory* category = &error_registry[i];
        for (size_t j = 0; j < category->error_count; j++) {
            if (category->errors[j].code == code) {
                return &category->errors[j];
            }
        }
    }
    
    return NULL;
}

// Get error feature by code
ErrorFeature get_error_feature(ErrorCode code) {
    if (code >= 0 && code <= 999) return ERROR_FEATURE_RUNTIME;
    if (code >= 1000 && code <= 1999) return ERROR_FEATURE_SYNTAX;
    if (code >= 2000 && code <= 2999) return ERROR_FEATURE_TYPE;
    if (code >= 3000 && code <= 3999) return ERROR_FEATURE_MODULE;
    if (code >= 9000 && code <= 9999) return ERROR_FEATURE_INTERNAL;
    
    return ERROR_FEATURE_RUNTIME; // Default
}

// Get error category name by feature
const char* get_error_category_name(ErrorFeature feature) {
    switch (feature) {
        case ERROR_FEATURE_RUNTIME: return "RUNTIME PANIC";
        case ERROR_FEATURE_SYNTAX: return "SYNTAX ERROR";
        case ERROR_FEATURE_TYPE: return "TYPE MISMATCH";
        case ERROR_FEATURE_MODULE: return "MODULE ERROR";
        case ERROR_FEATURE_INTERNAL: return "INTERNAL ERROR";
        default: return "UNKNOWN ERROR";
    }
}

// Check if error code is valid
bool is_error_code_valid(ErrorCode code) {
    return get_error_info(code) != NULL;
}

// Core error reporting function
ErrorReportResult report_feature_error(ErrorCode code, SrcLocation location, 
                                      const char* expected, const char* found) {
    if (!error_system_initialized) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    // For now, delegate to the legacy system for actual reporting
    // This maintains compatibility while we transition
    return report_type_error(code, location, expected, found);
}

// Formatted error reporting
ErrorReportResult report_feature_error_f(ErrorCode code, SrcLocation location, 
                                         const char* format, ...) {
    if (!error_system_initialized) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    char message[2048];
    va_list args;
    va_start(args, format);
    int result = vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    if (result < 0 || result >= (int)sizeof(message)) {
        return ERROR_REPORT_BUFFER_OVERFLOW;
    }
    
    // For now, delegate to legacy system
    return report_compile_error(code, location, "%s", message);
}