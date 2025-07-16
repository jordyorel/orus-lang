#ifndef ORUS_ERROR_INTERFACE_H
#define ORUS_ERROR_INTERFACE_H

#include "error_types.h"
#include <stdarg.h>

// Public API for error reporting
// This is the main interface other modules should use

// Initialize the error reporting system
ErrorReportResult init_feature_errors(void);

// Cleanup the error reporting system
void cleanup_feature_errors(void);

// Set source text for error context
ErrorReportResult set_error_source_text(const char* source, size_t length);

// Core error reporting function
ErrorReportResult report_feature_error(ErrorCode code, SrcLocation location, 
                                      const char* expected, const char* found);

// Formatted error reporting
ErrorReportResult report_feature_error_f(ErrorCode code, SrcLocation location, 
                                         const char* format, ...);

// Register a feature's error category
ErrorReportResult register_error_category(const char* category_name, 
                                          const FeatureErrorInfo* errors, 
                                          size_t count);

// Get error information by code
const FeatureErrorInfo* get_error_info(ErrorCode code);

// Get error category by code
ErrorFeature get_error_feature(ErrorCode code);

// Utility functions
const char* get_error_category_name(ErrorFeature feature);
bool is_error_code_valid(ErrorCode code);

#endif // ORUS_ERROR_INTERFACE_H