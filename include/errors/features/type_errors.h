//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: include/errors/features/type_errors.h
//  Author: Jordy Orel KONDA
//  Copyright (c) 2025 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Declares error helpers dedicated to static and dynamic type diagnostics.


#ifndef ORUS_TYPE_ERRORS_H
#define ORUS_TYPE_ERRORS_H

#include "../error_types.h"
#include "../error_interface.h"

// Initialize type error system
ErrorReportResult init_type_errors(void);

// Type-specific error reporting functions
ErrorReportResult report_type_mismatch(SrcLocation location, const char* expected, const char* found);
ErrorReportResult report_mixed_arithmetic(SrcLocation location, const char* left_type, const char* right_type);
ErrorReportResult report_invalid_cast(SrcLocation location, const char* target_type, const char* source_type);
ErrorReportResult report_undefined_type(SrcLocation location, const char* type_name);
ErrorReportResult report_incompatible_types(SrcLocation location, const char* left_type, const char* right_type);
ErrorReportResult report_argument_count_mismatch(SrcLocation location,
                                                const char* function_name,
                                                int expected,
                                                int actual);
ErrorReportResult report_type_annotation_required(SrcLocation location, const char* context);
ErrorReportResult report_unsupported_operation(SrcLocation location, const char* operation, const char* type);
ErrorReportResult report_immutable_assignment(SrcLocation location, const char* variable_name);
ErrorReportResult report_argument_count_mismatch_simple(SrcLocation location, int expected, int found);

// Type error utilities
const char* get_type_error_suggestion(ErrorCode code, const char* context);
bool is_type_error(ErrorCode code);

#endif // ORUS_TYPE_ERRORS_H
