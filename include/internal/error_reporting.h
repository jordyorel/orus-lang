#ifndef ERROR_REPORTING_H
#define ERROR_REPORTING_H

#include "vm/vm.h"
#include <stdbool.h>

// Error categories and codes (E0000-E9999)
typedef enum {
    // Runtime errors (E0000-E0999)
    E0001_DIVISION_BY_ZERO = 1,
    E0002_INDEX_OUT_OF_BOUNDS = 2,
    E0003_NULL_REFERENCE = 3,
    E0004_ARITHMETIC_OVERFLOW = 4,
    E0005_INVALID_OPERATION = 5,
    E0006_MODULO_BY_ZERO = 6,
    E0007_TYPE_CONVERSION = 7,
    E0008_STACK_OVERFLOW = 8,
    E0009_MEMORY_ALLOCATION = 9,
    
    // Syntax errors (E1000-E1999)
    E1001_UNEXPECTED_TOKEN = 1001,
    E1002_MISSING_COLON = 1002,
    E1003_MISSING_PARENTHESIS = 1003,
    E1004_MISSING_BRACE = 1004,
    E1005_UNEXPECTED_EOF = 1005,
    E1006_INVALID_SYNTAX = 1006,
    E1007_SEMICOLON_NOT_ALLOWED = 1007,
    E1008_INVALID_INDENTATION = 1008,
    E1009_EXPRESSION_TOO_COMPLEX = 1009,
    E1010_UNDEFINED_VARIABLE = 1010,
    E1011_VARIABLE_REDEFINITION = 1011,
    E1012_SCOPE_VIOLATION = 1012,
    E1013_INVALID_VARIABLE_NAME = 1013,
    E1014_MUTABLE_REQUIRED = 1014,
    E1015_INVALID_MULTIPLE_DECLARATION = 1015,
    E1016_LOOP_VARIABLE_MODIFICATION = 1016,
    E1017_IMMUTABLE_COMPOUND_ASSIGNMENT = 1017,
    E1018_VARIABLE_NOT_INITIALIZED = 1018,
    
    // Control flow errors (E1400-E1499)
    E1401_BREAK_OUTSIDE_LOOP = 1401,
    E1402_CONTINUE_OUTSIDE_LOOP = 1402,
    E1403_NON_BOOLEAN_CONDITION = 1403,
    E1404_INVALID_RANGE_SYNTAX = 1404,
    E1405_MISSING_COLON = 1405,
    E1406_UNREACHABLE_CODE = 1406,
    E1407_EMPTY_LOOP_BODY = 1407,
    E1408_INFINITE_LOOP_DETECTED = 1408,
    E1409_INVALID_LOOP_VARIABLE = 1409,
    
    // Type errors (E2000-E2999)
    E2001_TYPE_MISMATCH = 2001,
    E2002_INCOMPATIBLE_TYPES = 2002,
    E2003_UNDEFINED_TYPE = 2003,
    E2004_MIXED_ARITHMETIC = 2004,
    E2005_INVALID_CAST = 2005,
    E2006_TYPE_ANNOTATION_REQUIRED = 2006,
    E2007_UNSUPPORTED_OPERATION = 2007,
    E2008_IMMUTABLE_ASSIGNMENT = 2008,
    
    // Module/import errors (E3000-E3999)
    E3001_FILE_NOT_FOUND = 3001,
    E3002_CYCLIC_IMPORT = 3002,
    E3003_MODULE_NOT_FOUND = 3003,
    E3004_IMPORT_FAILED = 3004,
    
    // Internal errors (E9000-E9999)
    E9001_INTERNAL_PANIC = 9001,
    E9002_VM_CRASH = 9002,
    E9003_COMPILER_BUG = 9003,
    E9004_ASSERTION_FAILED = 9004
} ErrorCode;

// Error severity levels
typedef enum {
    SEVERITY_ERROR,
    SEVERITY_WARNING,
    SEVERITY_NOTE,
    SEVERITY_HELP
} ErrorSeverity;

// Enhanced error reporting structure
typedef struct {
    ErrorCode code;
    ErrorSeverity severity;
    const char* category;
    const char* title;
    const char* message;
    const char* help;
    const char* note;
    SrcLocation location;
    const char* source_line;
    int caret_start;
    int caret_end;
} EnhancedError;

// Color configuration
typedef struct {
    bool enabled;
    const char* error_color;    // Red
    const char* warning_color;  // Yellow  
    const char* note_color;     // Green
    const char* help_color;     // Cyan
    const char* reset_color;    // Reset
    const char* bold_color;     // Bold
} ColorConfig;

// Error reporting result codes (structured error handling)
typedef enum {
    ERROR_REPORT_SUCCESS = 0,
    ERROR_REPORT_OUT_OF_MEMORY = 1,
    ERROR_REPORT_INVALID_INPUT = 2,
    ERROR_REPORT_BUFFER_OVERFLOW = 3,
    ERROR_REPORT_FILE_ERROR = 4
} ErrorReportResult;

// Arena allocator for error reporting memory management
typedef struct ErrorArena {
    char* memory;
    size_t size;
    size_t used;
    size_t alignment;
} ErrorArena;

// Global error reporting configuration
typedef struct {
    ColorConfig colors;
    bool compact_mode;
    bool show_backtrace;
    bool show_help;
    bool show_notes;
    const char* source_text;
    ErrorArena* arena;  // Arena allocator for zero-cost abstractions
} ErrorReportingConfig;

// Context-based error system state (replaces global state)
typedef struct ErrorContext {
    ErrorReportingConfig config;
    ErrorArena arena;
    char arena_memory[64 * 1024];  // ERROR_ARENA_SIZE
    uint64_t source_text_length;
} ErrorContext;

// Context lifecycle management
ErrorContext* error_context_create(void);
void error_context_destroy(ErrorContext* ctx);
ErrorReportResult error_context_init(ErrorContext* ctx);

// Context-based API
ErrorReportResult init_error_reporting_ctx(ErrorContext* ctx);
ErrorReportResult cleanup_error_reporting_ctx(ErrorContext* ctx);
ErrorReportResult set_error_colors_ctx(ErrorContext* ctx, bool enable_colors);
ErrorReportResult set_compact_mode_ctx(ErrorContext* ctx, bool compact);
ErrorReportResult set_source_text_ctx(ErrorContext* ctx, const char* source, size_t length);
ErrorReportResult report_enhanced_error_ctx(ErrorContext* ctx, const EnhancedError* error);
ErrorReportResult report_runtime_error_ctx(ErrorContext* ctx, ErrorCode code, SrcLocation location, const char* format, ...);
ErrorReportResult report_compile_error_ctx(ErrorContext* ctx, ErrorCode code, SrcLocation location, const char* format, ...);

// Arena allocator functions (zero-cost abstraction)
ErrorReportResult init_error_arena(ErrorArena* arena, size_t size);
void cleanup_error_arena(ErrorArena* arena);
char* arena_alloc(ErrorArena* arena, size_t size, size_t alignment);
void arena_reset(ErrorArena* arena);

// Function prototypes with structured error handling
ErrorReportResult init_error_reporting(void);
ErrorReportResult cleanup_error_reporting(void);
ErrorReportResult set_error_colors(bool enable_colors);
ErrorReportResult set_compact_mode(bool compact);
ErrorReportResult set_source_text(const char* source, size_t length);

// Enhanced error reporting functions with structured error handling
ErrorReportResult report_enhanced_error(const EnhancedError* error);
ErrorReportResult report_runtime_error(ErrorCode code, SrcLocation location, const char* format, ...);
ErrorReportResult report_compile_error(ErrorCode code, SrcLocation location, const char* format, ...);
ErrorReportResult report_type_error(ErrorCode code, SrcLocation location, const char* expected, const char* found);

// Utility functions with bounds checking
const char* get_error_category(ErrorCode code);
ErrorReportResult get_source_line_safe(const char* source, size_t source_len, int line_number, 
                                       char* output, size_t output_size, size_t* line_length);
ErrorReportResult format_error_line_safe(char* buffer, size_t buffer_size, const char* source_line, 
                                         size_t source_len, int caret_start, int caret_end);

// Error code mappings
ErrorCode map_error_type_to_code(ErrorType type);
const char* get_error_title(ErrorCode code);
const char* get_error_help(ErrorCode code);
const char* get_error_note(ErrorCode code);

#endif // ERROR_REPORTING_H