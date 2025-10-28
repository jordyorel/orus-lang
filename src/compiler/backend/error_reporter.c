//  Orus Language Projec

#include "compiler/error_reporter.h"
#include "errors/error_interface.h"
#include "internal/error_reporting.h"
#include "vm/vm.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ErrorReporter {
    CompilerDiagnostic* diagnostics;
    size_t count;
    size_t capacity;
    bool use_colors;
    bool compact_mode;
};

static char* duplicate_string(const char* text) {
    if (!text) {
        return NULL;
    }
    size_t length = strlen(text);
    char* copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

static void free_diagnostic(CompilerDiagnostic* diagnostic) {
    if (!diagnostic) {
        return;
    }
    free(diagnostic->message);
    free(diagnostic->help);
    free(diagnostic->note);
    diagnostic->message = NULL;
    diagnostic->help = NULL;
    diagnostic->note = NULL;
}

static bool strings_equal(const char* a, const char* b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    return strcmp(a, b) == 0;
}

static bool locations_equal(SrcLocation a, SrcLocation b) {
    bool files_equal = false;
    if (a.file == b.file) {
        files_equal = true;
    } else if (a.file && b.file) {
        files_equal = strcmp(a.file, b.file) == 0;
    }

    return files_equal && a.line == b.line && a.column == b.column;
}

static bool diagnostics_match(const CompilerDiagnostic* diagnostic,
                              ErrorCode code,
                              ErrorSeverity severity,
                              SrcLocation location,
                              const char* message,
                              const char* help,
                              const char* note) {
    if (!diagnostic) {
        return false;
    }

    return diagnostic->code == code && diagnostic->severity == severity &&
           locations_equal(diagnostic->location, location) &&
           strings_equal(diagnostic->message, message) &&
           strings_equal(diagnostic->help, help) &&
           strings_equal(diagnostic->note, note);
}

static bool ensure_capacity(ErrorReporter* reporter, size_t required) {
    if (!reporter) {
        return false;
    }
    if (reporter->capacity >= required) {
        return true;
    }

    size_t new_capacity = reporter->capacity == 0 ? 8 : reporter->capacity * 2;
    while (new_capacity < required) {
        new_capacity *= 2;
    }

    CompilerDiagnostic* new_diagnostics = realloc(reporter->diagnostics,
                                                  new_capacity * sizeof(CompilerDiagnostic));
    if (!new_diagnostics) {
        return false;
    }

    reporter->diagnostics = new_diagnostics;
    reporter->capacity = new_capacity;
    return true;
}

ErrorReporter* error_reporter_create(void) {
    ErrorReporter* reporter = malloc(sizeof(ErrorReporter));
    if (!reporter) {
        return NULL;
    }

    reporter->diagnostics = NULL;
    reporter->count = 0;
    reporter->capacity = 0;
    reporter->use_colors = false;
    reporter->compact_mode = false;
    return reporter;
}

void error_reporter_destroy(ErrorReporter* reporter) {
    if (!reporter) {
        return;
    }

    error_reporter_reset(reporter);
    free(reporter->diagnostics);
    free(reporter);
}

void error_reporter_reset(ErrorReporter* reporter) {
    if (!reporter) {
        return;
    }

    for (size_t i = 0; i < reporter->count; ++i) {
        free_diagnostic(&reporter->diagnostics[i]);
    }
    reporter->count = 0;
}

bool error_reporter_add(ErrorReporter* reporter,
                        ErrorCode code,
                        ErrorSeverity severity,
                        SrcLocation location,
                        const char* message,
                        const char* help,
                        const char* note) {
    if (!reporter) {
        return false;
    }

    for (size_t i = 0; i < reporter->count; ++i) {
        if (diagnostics_match(&reporter->diagnostics[i], code, severity, location,
                               message, help, note)) {
            return true;
        }
    }

    if (!ensure_capacity(reporter, reporter->count + 1)) {
        return false;
    }

    CompilerDiagnostic* diagnostic = &reporter->diagnostics[reporter->count];
    diagnostic->code = code;
    diagnostic->severity = severity;
    diagnostic->location = location;
    diagnostic->message = duplicate_string(message);
    diagnostic->help = duplicate_string(help);
    diagnostic->note = duplicate_string(note);

    if ((message && !diagnostic->message) ||
        (help && !diagnostic->help) ||
        (note && !diagnostic->note)) {
        free_diagnostic(diagnostic);
        return false;
    }

    reporter->count++;
    return true;
}

bool error_reporter_add_feature_error(ErrorReporter* reporter,
                                      ErrorCode code,
                                      SrcLocation location,
                                      const char* format,
                                      ...) {
    if (!reporter || !format) {
        return false;
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return false;
    }

    SrcLocation effective_location = location;
    if (!effective_location.file) {
        extern VM vm;
        if (vm.filePath) {
            effective_location.file = vm.filePath;
        }
    }

    const FeatureErrorInfo* info = get_error_info(code);
    const char* help = (info && info->help) ? info->help : get_error_help(code);
    const char* note = (info && info->note) ? info->note : get_error_note(code);

    return error_reporter_add(reporter, code, SEVERITY_ERROR,
                              effective_location, buffer, help, note);
}

bool error_reporter_add_formatted(ErrorReporter* reporter,
                                  ErrorCode code,
                                  ErrorSeverity severity,
                                  SrcLocation location,
                                  const char* format,
                                  ...) {
    if (!reporter || !format) {
        return false;
    }

    char buffer[1024];
    va_list args;
    va_start(args, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return false;
    }

    return error_reporter_add(reporter, code, severity, location, buffer, NULL, NULL);
}

bool error_reporter_has_errors(const ErrorReporter* reporter) {
    return reporter && reporter->count > 0;
}

size_t error_reporter_count(const ErrorReporter* reporter) {
    return reporter ? reporter->count : 0;
}

const CompilerDiagnostic* error_reporter_diagnostics(const ErrorReporter* reporter) {
    return reporter ? reporter->diagnostics : NULL;
}

void error_reporter_set_use_colors(ErrorReporter* reporter, bool use_colors) {
    if (!reporter) {
        return;
    }
    reporter->use_colors = use_colors;
}

void error_reporter_set_compact_mode(ErrorReporter* reporter, bool compact_mode) {
    if (!reporter) {
        return;
    }
    reporter->compact_mode = compact_mode;
}

bool error_reporter_use_colors(const ErrorReporter* reporter) {
    return reporter ? reporter->use_colors : false;
}

bool error_reporter_compact_mode(const ErrorReporter* reporter) {
    return reporter ? reporter->compact_mode : false;
}
