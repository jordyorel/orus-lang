// Orus Language Project
// ---------------------------------------------------------------------------
// File: include/compiler/error_reporter.h
// Author: Jordy Orel KONDA
// Copyright (c) 2022 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Declares utilities for tracking, formatting, and reporting compiler diagnostics.


#ifndef ORUS_COMPILER_ERROR_REPORTER_H
#define ORUS_COMPILER_ERROR_REPORTER_H

#include <stdbool.h>
#include <stddef.h>
#include "internal/error_reporting.h"
#include "errors/error_types.h"
#include "vm/vm.h"

typedef struct {
    ErrorCode code;
    ErrorSeverity severity;
    SrcLocation location;
    char* message;
    char* help;
    char* note;
} CompilerDiagnostic;

typedef struct ErrorReporter ErrorReporter;

ErrorReporter* error_reporter_create(void);
void error_reporter_destroy(ErrorReporter* reporter);
void error_reporter_reset(ErrorReporter* reporter);
bool error_reporter_add(ErrorReporter* reporter,
                        ErrorCode code,
                        ErrorSeverity severity,
                        SrcLocation location,
                        const char* message,
                        const char* help,
                        const char* note);
bool error_reporter_add_feature_error(ErrorReporter* reporter,
                                      ErrorCode code,
                                      SrcLocation location,
                                      const char* format,
                                      ...);
bool error_reporter_add_formatted(ErrorReporter* reporter,
                                  ErrorCode code,
                                  ErrorSeverity severity,
                                  SrcLocation location,
                                  const char* format,
                                  ...);
bool error_reporter_has_errors(const ErrorReporter* reporter);
size_t error_reporter_count(const ErrorReporter* reporter);
const CompilerDiagnostic* error_reporter_diagnostics(const ErrorReporter* reporter);
void error_reporter_set_use_colors(ErrorReporter* reporter, bool use_colors);
void error_reporter_set_compact_mode(ErrorReporter* reporter, bool compact_mode);
bool error_reporter_use_colors(const ErrorReporter* reporter);
bool error_reporter_compact_mode(const ErrorReporter* reporter);

#endif // ORUS_COMPILER_ERROR_REPORTER_H
