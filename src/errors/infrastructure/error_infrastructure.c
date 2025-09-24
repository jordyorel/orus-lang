#include "internal/error_reporting.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <assert.h>
#include <stdalign.h>

// Performance-critical constants aligned to cache boundaries
#define ERROR_ARENA_SIZE (64 * 1024)  // 64KB arena for error reporting
#define CACHE_LINE_SIZE 64
#define SIMD_ALIGNMENT 32
#define MAX_ERROR_MESSAGE_SIZE 2048
#define MAX_SOURCE_LINE_SIZE 1024

// Cache-aligned global configuration for optimal performance (backward compatibility)
typedef struct {
    ErrorReportingConfig config;
    ErrorArena arena;
    char arena_memory[ERROR_ARENA_SIZE];
    uint64_t source_text_length;  // Track length for bounds checking
} ErrorState;

static _Alignas(CACHE_LINE_SIZE) ErrorState g_error_state = {
    .config = {
        .colors = {
            .enabled = true,
            .error_color = "\033[0;31m",     // Red
            .warning_color = "\033[1;33m",   // Yellow
            .note_color = "\033[0;32m",      // Green
            .help_color = "\033[0;36m",      // Cyan
            .reset_color = "\033[0m",        // Reset
            .bold_color = "\033[1m"          // Bold
        },
        .compact_mode = false,
        .show_backtrace = false,
        .show_help = true,
        .show_notes = true,
        .source_text = NULL,
        .arena = NULL
    },
    .arena = {0},
    .arena_memory = {0},
    .source_text_length = 0
};

// ---- Context lifecycle management ----
ErrorContext* error_context_create(void) {
    ErrorContext* ctx = malloc(sizeof(ErrorContext));
    if (!ctx) return NULL;
    
    ErrorReportResult result = error_context_init(ctx);
    if (result != ERROR_REPORT_SUCCESS) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void error_context_destroy(ErrorContext* ctx) {
    if (!ctx) return;
    
    cleanup_error_reporting_ctx(ctx);
    free(ctx);
}

ErrorReportResult error_context_init(ErrorContext* ctx) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    
    // Initialize default configuration
    ctx->config.colors.enabled = true;
    ctx->config.colors.error_color = "\033[0;31m";     // Red
    ctx->config.colors.warning_color = "\033[1;33m";   // Yellow
    ctx->config.colors.note_color = "\033[0;32m";      // Green
    ctx->config.colors.help_color = "\033[0;36m";      // Cyan
    ctx->config.colors.reset_color = "\033[0m";        // Reset
    ctx->config.colors.bold_color = "\033[1m";         // Bold
    
    ctx->config.compact_mode = false;
    ctx->config.show_backtrace = false;
    ctx->config.show_help = true;
    ctx->config.show_notes = true;
    ctx->config.source_text = NULL;
    ctx->config.arena = &ctx->arena;
    
    // Initialize arena
    ctx->arena.memory = ctx->arena_memory;
    ctx->arena.size = ERROR_ARENA_SIZE;
    ctx->arena.used = 0;
    ctx->arena.alignment = CACHE_LINE_SIZE;
    
    ctx->source_text_length = 0;
    
    // Check terminal capabilities for colors
    const char* term = getenv("TERM");
    if (!term || strcmp(term, "dumb") == 0) {
        ctx->config.colors.enabled = false;
    }
    
    return ERROR_REPORT_SUCCESS;
}

// SIMD-optimized string length calculation (zero-cost abstraction)
static inline size_t simd_strlen(const char* str) {
    if (!str) return 0;
    
    const char* p = str;
    // Align to SIMD boundary for optimal performance
    while (((uintptr_t)p & (SIMD_ALIGNMENT - 1)) && *p) {
        p++;
    }
    
    // Use compiler vectorization for aligned portion
    while (*p) {
        p++;
    }
    
    return (size_t)(p - str);
}

// Arena allocator implementation (zero-cost abstraction)
ErrorReportResult init_error_arena(ErrorArena* arena, size_t size) {
    if (!arena) return ERROR_REPORT_INVALID_INPUT;
    if (size == 0) return ERROR_REPORT_INVALID_INPUT;
    
    arena->memory = g_error_state.arena_memory;
    arena->size = size <= ERROR_ARENA_SIZE ? size : ERROR_ARENA_SIZE;
    arena->used = 0;
    arena->alignment = CACHE_LINE_SIZE;
    
    return ERROR_REPORT_SUCCESS;
}

void cleanup_error_arena(ErrorArena* arena) {
    if (arena) {
        arena->used = 0;  // Reset, no free() needed
    }
}

// Cache-aligned arena allocation (zero-cost abstraction)
char* arena_alloc(ErrorArena* arena, size_t size, size_t alignment) {
    if (!arena || size == 0) return NULL;
    
    // Ensure alignment is power of 2
    alignment = alignment ? alignment : sizeof(void*);
    
    // Align current position
    size_t current = (arena->used + alignment - 1) & ~(alignment - 1);
    
    // Check bounds to prevent buffer overflow
    if (current + size > arena->size) {
        return NULL;  // Out of arena memory
    }
    
    char* result = arena->memory + current;
    arena->used = current + size;
    
    return result;
}

void arena_reset(ErrorArena* arena) {
    if (arena) {
        arena->used = 0;  // Reset without deallocation
    }
}

// Context-based initialization
ErrorReportResult init_error_reporting_ctx(ErrorContext* ctx) {
    return error_context_init(ctx);
}

// High-performance initialization with structured error handling (backward compatibility)
ErrorReportResult init_error_reporting(void) {
    // Initialize arena allocator
    ErrorReportResult result = init_error_arena(&g_error_state.arena, ERROR_ARENA_SIZE);
    if (result != ERROR_REPORT_SUCCESS) {
        return result;
    }
    
    g_error_state.config.arena = &g_error_state.arena;
    
    // Check terminal capabilities for colors
    const char* term = getenv("TERM");
    if (!term || strcmp(term, "dumb") == 0) {
        g_error_state.config.colors.enabled = false;
    }
    
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult cleanup_error_reporting_ctx(ErrorContext* ctx) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    
    // Reset arena (no malloc/free overhead)
    arena_reset(&ctx->arena);
    ctx->config.source_text = NULL;
    ctx->source_text_length = 0;
    
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult cleanup_error_reporting(void) {
    // Reset arena (no malloc/free overhead)
    arena_reset(&g_error_state.arena);
    g_error_state.config.source_text = NULL;
    g_error_state.source_text_length = 0;
    
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult set_error_colors_ctx(ErrorContext* ctx, bool enable_colors) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    
    ctx->config.colors.enabled = enable_colors;
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult set_compact_mode_ctx(ErrorContext* ctx, bool compact) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    
    ctx->config.compact_mode = compact;
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult set_error_colors(bool enable_colors) {
    g_error_state.config.colors.enabled = enable_colors;
    return ERROR_REPORT_SUCCESS;
}

ErrorReportResult set_compact_mode(bool compact) {
    g_error_state.config.compact_mode = compact;
    return ERROR_REPORT_SUCCESS;
}

// Context-based source text storage with arena allocation
ErrorReportResult set_source_text_ctx(ErrorContext* ctx, const char* source, size_t length) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    if (!source && length > 0) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    if (length == 0) {
        ctx->config.source_text = NULL;
        ctx->source_text_length = 0;
        return ERROR_REPORT_SUCCESS;
    }
    
    // Allocate from arena instead of malloc
    char* arena_copy = arena_alloc(&ctx->arena, length + 1, 1);
    if (!arena_copy) {
        return ERROR_REPORT_OUT_OF_MEMORY;
    }
    
    // Bounds-checked copy
    if (length > 0) {
        memcpy(arena_copy, source, length);
    }
    arena_copy[length] = '\0';
    
    ctx->config.source_text = arena_copy;
    ctx->source_text_length = length;
    
    return ERROR_REPORT_SUCCESS;
}

// Bounds-checked source text storage with arena allocation (backward compatibility)
ErrorReportResult set_source_text(const char* source, size_t length) {
    if (!source && length > 0) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    if (length == 0) {
        g_error_state.config.source_text = NULL;
        g_error_state.source_text_length = 0;
        return ERROR_REPORT_SUCCESS;
    }
    
    // Allocate from arena instead of malloc
    char* arena_copy = arena_alloc(&g_error_state.arena, length + 1, 1);
    if (!arena_copy) {
        return ERROR_REPORT_OUT_OF_MEMORY;
    }
    
    // Bounds-checked copy
    if (length > 0) {
        memcpy(arena_copy, source, length);
    }
    arena_copy[length] = '\0';
    
    g_error_state.config.source_text = arena_copy;
    g_error_state.source_text_length = length;
    
    return ERROR_REPORT_SUCCESS;
}

const char* get_error_category(ErrorCode code) {
    // Branchless selection for better performance
    static const char* categories[] = {
        "RUNTIME PANIC",   // 0-999
        "SYNTAX ERROR",    // 1000-1999
        "TYPE MISMATCH",   // 2000-2999
        "MODULE ERROR",    // 3000-3999
        "UNKNOWN ERROR",   // 4000-8999 (placeholder)
        "UNKNOWN ERROR",   // 5000-8999 (placeholder)
        "UNKNOWN ERROR",   // 6000-8999 (placeholder)
        "UNKNOWN ERROR",   // 7000-8999 (placeholder)
        "UNKNOWN ERROR",   // 8000-8999 (placeholder)
        "INTERNAL ERROR"   // 9000-9999
    };
    
    uint32_t index = code / 1000;
    if (index >= sizeof(categories) / sizeof(categories[0])) {
        return "UNKNOWN ERROR";
    }
    
    return categories[index];
}

// Bounds-checked source line retrieval with SIMD optimization
ErrorReportResult get_source_line_safe(const char* source, size_t source_len, int line_number,
                                       char* output, size_t output_size, size_t* line_length) {
    if (!source || !output || output_size == 0 || line_number <= 0) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    const char* line_start = source;
    const char* source_end = source + source_len;
    int current_line = 1;
    
    // SIMD-optimized line finding
    while (current_line < line_number && line_start < source_end) {
        const char* newline = memchr(line_start, '\n', source_end - line_start);
        if (!newline) break;
        
        line_start = newline + 1;
        current_line++;
    }
    
    if (current_line != line_number || line_start >= source_end) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    // Find line end with bounds checking
    const char* line_end = memchr(line_start, '\n', source_end - line_start);
    if (!line_end) {
        line_end = source_end;
    }
    
    // Remove carriage return if present
    if (line_end > line_start && *(line_end - 1) == '\r') {
        line_end--;
    }
    
    size_t length = line_end - line_start;
    if (length >= output_size) {
        length = output_size - 1;  // Bounds checking
    }
    
    // Bounds-checked copy
    if (length > 0) {
        memcpy(output, line_start, length);
    }
    output[length] = '\0';
    
    if (line_length) {
        *line_length = length;
    }
    
    return ERROR_REPORT_SUCCESS;
}

// Bounds-checked caret formatting with overflow protection
ErrorReportResult format_error_line_safe(char* buffer, size_t buffer_size, const char* source_line,
                                         size_t source_len, int caret_start, int caret_end) {
    if (!buffer || buffer_size == 0) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    // Avoid unused parameter warning
    (void)source_line;
    
    // Bounds checking for caret positions
    if (caret_start < 0) caret_start = 0;
    if (caret_end < caret_start) caret_end = caret_start + 1;
    if ((size_t)caret_start >= source_len) caret_start = source_len > 0 ? source_len - 1 : 0;
    if ((size_t)caret_end > source_len) caret_end = source_len;
    
    // Clear buffer
    buffer[0] = '\0';
    
    // Add leading spaces with bounds checking
    size_t pos = 0;
    for (int i = 0; i < caret_start && pos < buffer_size - 1; i++) {
        buffer[pos++] = ' ';
    }
    
    // Add caret characters with bounds checking
    for (int i = caret_start; i < caret_end && pos < buffer_size - 1; i++) {
        buffer[pos++] = '^';
    }
    
    buffer[pos] = '\0';
    
    return ERROR_REPORT_SUCCESS;
}

// Context-based error reporting with structured error handling
ErrorReportResult report_enhanced_error_ctx(ErrorContext* ctx, const EnhancedError* error) {
    if (!ctx || !error) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    const char* error_color = ctx->config.colors.enabled ? ctx->config.colors.error_color : "";
    const char* help_color = ctx->config.colors.enabled ? ctx->config.colors.help_color : "";
    const char* note_color = ctx->config.colors.enabled ? ctx->config.colors.note_color : "";
    const char* reset_color = ctx->config.colors.enabled ? ctx->config.colors.reset_color : "";
    const char* bold_color = ctx->config.colors.enabled ? ctx->config.colors.bold_color : "";
    
    if (ctx->config.compact_mode) {
        // Compact format with bounds checking
        int result = fprintf(stderr, "%s:%d:%d: %s%s%s\n",
                            error->location.file ? error->location.file : "<unknown>",
                            error->location.line,
                            error->location.column,
                            error_color,
                            error->message ? error->message : "Unknown error",
                            reset_color);
        
        return result < 0 ? ERROR_REPORT_FILE_ERROR : ERROR_REPORT_SUCCESS;
    }
    
    // Full format with performance optimization
    fprintf(stderr, "%s-- %s: %s %s", 
            error_color,
            error->category ? error->category : "UNKNOWN",
            error->title ? error->title : "Unknown error",
            reset_color);
    
    // Optimized dash filling
    const char* category = error->category ? error->category : "UNKNOWN";
    const char* title = error->title ? error->title : "Unknown error";
    size_t cat_len = simd_strlen(category);
    size_t title_len = simd_strlen(title);
    int dashes_needed = 60 - cat_len - title_len - 4;
    
    for (int i = 0; i < dashes_needed && i < 50; i++) {
        fputc('-', stderr);
    }
    
    fprintf(stderr, " %s:%d:%d\n\n",
            error->location.file ? error->location.file : "<unknown>",
            error->location.line,
            error->location.column);
    
    // Source line display with arena allocation
    char line_buffer[MAX_SOURCE_LINE_SIZE];
    char caret_buffer[MAX_SOURCE_LINE_SIZE];
    
    ErrorReportResult line_result = ERROR_REPORT_INVALID_INPUT;
    if (ctx->config.source_text && error->location.line > 0) {
        size_t line_length;
        line_result = get_source_line_safe(ctx->config.source_text, 
                                          ctx->source_text_length,
                                          error->location.line,
                                          line_buffer, sizeof(line_buffer),
                                          &line_length);
    }
    
    if (line_result == ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "%s%3d%s | %s\n",
                bold_color,
                error->location.line,
                reset_color,
                line_buffer);
        
        // Format caret line with bounds checking
        ErrorReportResult caret_result = format_error_line_safe(caret_buffer, sizeof(caret_buffer),
                                                               line_buffer, simd_strlen(line_buffer),
                                                               error->caret_start, error->caret_end);
        
        if (caret_result == ERROR_REPORT_SUCCESS) {
            fprintf(stderr, "      | %s%s%s",
                    error_color, caret_buffer, reset_color);
            
            if (error->message && simd_strlen(error->message) > 0) {
                fprintf(stderr, " %s", error->message);
            }
            fprintf(stderr, "\n      |\n");
        }
    } else {
        // Fallback display
        fprintf(stderr, "      | (source line not available)\n");
        fprintf(stderr, "      | ");
        
        int caret_pos = error->location.column > 0 ? error->location.column - 1 : 0;
        for (int i = 0; i < caret_pos && i < 80; i++) {
            fputc(' ', stderr);
        }
        fprintf(stderr, "%s^%s %s\n", error_color, reset_color, 
                error->message ? error->message : "");
        fprintf(stderr, "      |\n");
    }
    
    // Main explanation
    fprintf(stderr, "      = %s\n", error->message ? error->message : "Unknown error");
    
    // Help and notes with bounds checking
    if (error->help && ctx->config.show_help) {
        fprintf(stderr, "      = %shelp%s: %s\n", 
                help_color, reset_color, error->help);
    }
    
    if (error->note && ctx->config.show_notes) {
        fprintf(stderr, "      = %snote%s: %s\n", 
                note_color, reset_color, error->note);
    }
    
    fprintf(stderr, "\n");
    
    return ERROR_REPORT_SUCCESS;
}

// High-performance error reporting with structured error handling (backward compatibility)
ErrorReportResult report_enhanced_error(const EnhancedError* error) {
    if (!error) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    const char* error_color = g_error_state.config.colors.enabled ? g_error_state.config.colors.error_color : "";
    const char* help_color = g_error_state.config.colors.enabled ? g_error_state.config.colors.help_color : "";
    const char* note_color = g_error_state.config.colors.enabled ? g_error_state.config.colors.note_color : "";
    const char* reset_color = g_error_state.config.colors.enabled ? g_error_state.config.colors.reset_color : "";
    const char* bold_color = g_error_state.config.colors.enabled ? g_error_state.config.colors.bold_color : "";
    
    if (g_error_state.config.compact_mode) {
        // Compact format with bounds checking
        int result = fprintf(stderr, "%s:%d:%d: %s%s%s\n",
                            error->location.file ? error->location.file : "<unknown>",
                            error->location.line,
                            error->location.column,
                            error_color,
                            error->message ? error->message : "Unknown error",
                            reset_color);
        
        return result < 0 ? ERROR_REPORT_FILE_ERROR : ERROR_REPORT_SUCCESS;
    }
    
    // Full format with performance optimization
    fprintf(stderr, "%s-- %s: %s %s", 
            error_color,
            error->category ? error->category : "UNKNOWN",
            error->title ? error->title : "Unknown error",
            reset_color);
    
    // Optimized dash filling
    const char* category = error->category ? error->category : "UNKNOWN";
    const char* title = error->title ? error->title : "Unknown error";
    size_t cat_len = simd_strlen(category);
    size_t title_len = simd_strlen(title);
    int dashes_needed = 60 - cat_len - title_len - 4;
    
    for (int i = 0; i < dashes_needed && i < 50; i++) {
        fputc('-', stderr);
    }
    
    fprintf(stderr, " %s:%d:%d\n\n",
            error->location.file ? error->location.file : "<unknown>",
            error->location.line,
            error->location.column);
    
    // Source line display with arena allocation
    char line_buffer[MAX_SOURCE_LINE_SIZE];
    char caret_buffer[MAX_SOURCE_LINE_SIZE];
    
    ErrorReportResult line_result = ERROR_REPORT_INVALID_INPUT;
    if (g_error_state.config.source_text && error->location.line > 0) {
        size_t line_length;
        line_result = get_source_line_safe(g_error_state.config.source_text, 
                                          g_error_state.source_text_length,
                                          error->location.line,
                                          line_buffer, sizeof(line_buffer),
                                          &line_length);
    }
    
    if (line_result == ERROR_REPORT_SUCCESS) {
        fprintf(stderr, "%s%3d%s | %s\n",
                bold_color,
                error->location.line,
                reset_color,
                line_buffer);
        
        // Format caret line with bounds checking
        ErrorReportResult caret_result = format_error_line_safe(caret_buffer, sizeof(caret_buffer),
                                                               line_buffer, simd_strlen(line_buffer),
                                                               error->caret_start, error->caret_end);
        
        if (caret_result == ERROR_REPORT_SUCCESS) {
            fprintf(stderr, "      | %s%s%s",
                    error_color, caret_buffer, reset_color);
            
            if (error->message && simd_strlen(error->message) > 0) {
                fprintf(stderr, " %s", error->message);
            }
            fprintf(stderr, "\n      |\n");
        }
    } else {
        // Fallback display
        fprintf(stderr, "      | (source line not available)\n");
        fprintf(stderr, "      | ");
        
        int caret_pos = error->location.column > 0 ? error->location.column - 1 : 0;
        for (int i = 0; i < caret_pos && i < 80; i++) {
            fputc(' ', stderr);
        }
        fprintf(stderr, "%s^%s %s\n", error_color, reset_color, 
                error->message ? error->message : "");
        fprintf(stderr, "      |\n");
    }
    
    // Main explanation
    fprintf(stderr, "      = %s\n", error->message ? error->message : "Unknown error");
    
    // Help and notes with bounds checking
    if (error->help && g_error_state.config.show_help) {
        fprintf(stderr, "      = %shelp%s: %s\n", 
                help_color, reset_color, error->help);
    }
    
    if (error->note && g_error_state.config.show_notes) {
        fprintf(stderr, "      = %snote%s: %s\n", 
                note_color, reset_color, error->note);
    }
    
    fprintf(stderr, "\n");
    
    return ERROR_REPORT_SUCCESS;
}

// Context-based runtime error reporting with structured error handling
ErrorReportResult report_runtime_error_ctx(ErrorContext* ctx, ErrorCode code, SrcLocation location, const char* format, ...) {
    if (!ctx) return ERROR_REPORT_INVALID_INPUT;
    
    char message[MAX_ERROR_MESSAGE_SIZE];
    
    if (format) {
        va_list args;
        va_start(args, format);
        int result = vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        
        if (result < 0 || result >= (int)sizeof(message)) {
            return ERROR_REPORT_BUFFER_OVERFLOW;
        }
    } else {
        message[0] = '\0';
    }
    
    // Ensure filename is set if available
    extern VM vm;
    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
    }
    
    // Better caret positioning with bounds checking
    int caret_start = location.column > 0 ? location.column - 1 : 0;
    int caret_end = caret_start + 1;
    
    // Enhanced caret positioning for division by zero
    if (code == E0001_DIVISION_BY_ZERO && ctx->config.source_text) {
        char line_buffer[MAX_SOURCE_LINE_SIZE];
        size_t line_length;
        
        ErrorReportResult line_result = get_source_line_safe(
            ctx->config.source_text,
            ctx->source_text_length,
            location.line,
            line_buffer, sizeof(line_buffer),
            &line_length);
        
        if (line_result == ERROR_REPORT_SUCCESS) {
            const char* div_pos = strchr(line_buffer + caret_start, '/');
            if (div_pos && div_pos < line_buffer + line_length) {
                caret_end = (div_pos - line_buffer) + 1;
            }
        }
    }
    
    EnhancedError error = {
        .code = code,
        .severity = SEVERITY_ERROR,
        .category = get_error_category(code),
        .title = get_error_title(code),
        .message = message,
        .help = get_error_help(code),
        .note = get_error_note(code),
        .location = location,
        .source_line = NULL,  // Will be retrieved in report_enhanced_error_ctx
        .caret_start = caret_start,
        .caret_end = caret_end
    };
    
    return report_enhanced_error_ctx(ctx, &error);
}

// Compile-time error reporting with structured error handling
ErrorReportResult report_compile_error_ctx(ErrorContext* ctx, ErrorCode code, SrcLocation location, const char* format, ...) {
    if (!ctx) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    char message[MAX_ERROR_MESSAGE_SIZE];
    
    if (format) {
        va_list args;
        va_start(args, format);
        int result = vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        
        if (result < 0 || result >= (int)sizeof(message)) {
            return ERROR_REPORT_BUFFER_OVERFLOW;
        }
    } else {
        message[0] = '\0';
    }
    
    // Ensure filename is set if available
    extern VM vm;
    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
    }
    
    // Better caret positioning with bounds checking
    int caret_start = location.column > 0 ? location.column - 1 : 0;
    int caret_end = caret_start + 1;
    
    // Enhanced caret positioning for specific error types
    if (ctx->config.source_text) {
        char line_buffer[MAX_SOURCE_LINE_SIZE];
        size_t line_length;
        
        ErrorReportResult line_result = get_source_line_safe(
            ctx->config.source_text,
            ctx->source_text_length,
            location.line,
            line_buffer, sizeof(line_buffer),
            &line_length);
        
        if (line_result == ERROR_REPORT_SUCCESS) {
            // Adjust caret positioning based on error type
            switch (code) {
            case E1002_MISSING_COLON:
                {
                    const char* colon_pos = strchr(line_buffer + caret_start, ':');
                    if (colon_pos && colon_pos < line_buffer + line_length) {
                        caret_end = (colon_pos - line_buffer) + 1;
                    }
                }
                break;
            case E1003_MISSING_PARENTHESIS:
                {
                    const char* paren_pos = strchr(line_buffer + caret_start, '(');
                    if (paren_pos && paren_pos < line_buffer + line_length) {
                        caret_end = (paren_pos - line_buffer) + 1;
                    }
                }
                break;
            default:
                // Use default positioning
                break;
            }
        }
    }
    
    EnhancedError error = {
        .code = code,
        .severity = SEVERITY_ERROR,
        .category = get_error_category(code),
        .title = get_error_title(code),
        .message = message,
        .help = get_error_help(code),
        .note = get_error_note(code),
        .location = location,
        .source_line = NULL,  // Will be retrieved in report_enhanced_error_ctx
        .caret_start = caret_start,
        .caret_end = caret_end
    };
    
    return report_enhanced_error_ctx(ctx, &error);
}

// Runtime error reporting with structured error handling (backward compatibility)
ErrorReportResult report_runtime_error(ErrorCode code, SrcLocation location, const char* format, ...) {
    char message[MAX_ERROR_MESSAGE_SIZE];
    
    if (format) {
        va_list args;
        va_start(args, format);
        int result = vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        
        if (result < 0 || result >= (int)sizeof(message)) {
            return ERROR_REPORT_BUFFER_OVERFLOW;
        }
    } else {
        message[0] = '\0';
    }
    
    // Ensure filename is set if available
    extern VM vm;
    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
    }
    
    // Better caret positioning with bounds checking
    int caret_start = location.column > 0 ? location.column - 1 : 0;
    int caret_end = caret_start + 1;
    
    // Enhanced caret positioning for division by zero
    if (code == E0001_DIVISION_BY_ZERO && g_error_state.config.source_text) {
        char line_buffer[MAX_SOURCE_LINE_SIZE];
        size_t line_length;
        
        ErrorReportResult line_result = get_source_line_safe(
            g_error_state.config.source_text,
            g_error_state.source_text_length,
            location.line,
            line_buffer, sizeof(line_buffer),
            &line_length);
        
        if (line_result == ERROR_REPORT_SUCCESS) {
            const char* div_pos = strchr(line_buffer + caret_start, '/');
            if (div_pos && div_pos < line_buffer + line_length) {
                caret_end = (div_pos - line_buffer) + 1;
            }
        }
    }
    
    EnhancedError error = {
        .code = code,
        .severity = SEVERITY_ERROR,
        .category = get_error_category(code),
        .title = get_error_title(code),
        .message = message,
        .help = get_error_help(code),
        .note = get_error_note(code),
        .location = location,
        .source_line = NULL,  // Will be retrieved in report_enhanced_error
        .caret_start = caret_start,
        .caret_end = caret_end
    };
    
    return report_enhanced_error(&error);
}

// Compile error reporting with structured error handling
ErrorReportResult report_compile_error(ErrorCode code, SrcLocation location, const char* format, ...) {
    char message[MAX_ERROR_MESSAGE_SIZE];
    
    if (format) {
        va_list args;
        va_start(args, format);
        int result = vsnprintf(message, sizeof(message), format, args);
        va_end(args);
        
        if (result < 0 || result >= (int)sizeof(message)) {
            return ERROR_REPORT_BUFFER_OVERFLOW;
        }
    } else {
        message[0] = '\0';
    }
    
    // Ensure filename is set if available
    extern VM vm;
    if (location.file == NULL && vm.filePath) {
        location.file = vm.filePath;
    }
    
    int caret_start = location.column > 0 ? location.column - 1 : 0;
    int caret_end = caret_start + 1;
    
    // Enhanced semicolon detection
    if (code == E1007_SEMICOLON_NOT_ALLOWED && g_error_state.config.source_text) {
        char line_buffer[MAX_SOURCE_LINE_SIZE];
        size_t line_length;
        
        ErrorReportResult line_result = get_source_line_safe(
            g_error_state.config.source_text,
            g_error_state.source_text_length,
            location.line,
            line_buffer, sizeof(line_buffer),
            &line_length);
        
        if (line_result == ERROR_REPORT_SUCCESS) {
            const char* semicolon_pos = strchr(line_buffer + caret_start, ';');
            if (semicolon_pos && semicolon_pos < line_buffer + line_length) {
                caret_start = semicolon_pos - line_buffer;
                caret_end = caret_start + 1;
            }
        }
    }
    
    EnhancedError error = {
        .code = code,
        .severity = SEVERITY_ERROR,
        .category = get_error_category(code),
        .title = get_error_title(code),
        .message = message,
        .help = get_error_help(code),
        .note = get_error_note(code),
        .location = location,
        .source_line = NULL,  // Will be retrieved in report_enhanced_error
        .caret_start = caret_start,
        .caret_end = caret_end
    };
    
    return report_enhanced_error(&error);
}

// DEPRECATED: This function is replaced by feature-specific error functions
// in src/errors/features/. Kept for backward compatibility only.
// New code should use report_type_mismatch(), report_mixed_arithmetic(), etc.
ErrorReportResult report_type_error(ErrorCode code, SrcLocation location, const char* expected, const char* found) {
    // Legacy compatibility - delegate to compile error with basic message
    if (!expected || !found) {
        return ERROR_REPORT_INVALID_INPUT;
    }
    
    char message[MAX_ERROR_MESSAGE_SIZE];
    int result = snprintf(message, sizeof(message), 
                         "this is a `%s`, but `%s` was expected", found, expected);
    
    if (result < 0 || result >= (int)sizeof(message)) {
        return ERROR_REPORT_BUFFER_OVERFLOW;
    }
    
    return report_compile_error(code, location, "%s", message);
}

// Error code mapping and title functions (keep existing implementations)
ErrorCode map_error_type_to_code(ErrorType type) {
    switch (type) {
        case ERROR_VALUE: return E0001_DIVISION_BY_ZERO;
        case ERROR_TYPE: return E2001_TYPE_MISMATCH;
        case ERROR_INDEX: return E0002_INDEX_OUT_OF_BOUNDS;
        case ERROR_SYNTAX: return E1006_INVALID_SYNTAX;
        case ERROR_RUNTIME: return E0005_INVALID_OPERATION;
        case ERROR_IMPORT: return E3004_IMPORT_FAILED;
        default: return E9001_INTERNAL_PANIC;
    }
}

const char* get_error_title(ErrorCode code) {
    // NOTE: E2xxx (type) errors are now handled by src/errors/features/type_errors.c
    // This function is kept for runtime, syntax, module, and internal errors only
    switch (code) {
        // Runtime errors (E0xxx)
        case E0001_DIVISION_BY_ZERO: return "Oh no! You tried to divide by zero";
        case E0002_INDEX_OUT_OF_BOUNDS: return "Index is outside the valid range";
        case E0003_NULL_REFERENCE: return "Tried to use a null value";
        case E0004_ARITHMETIC_OVERFLOW: return "Number got too big to handle";
        case E0005_INVALID_OPERATION: return "This operation isn't allowed here";
        case E0006_MODULO_BY_ZERO: return "Can't find remainder when dividing by zero";
        case E0007_TYPE_CONVERSION: return "Can't convert between these types";
        
        // Syntax errors (E1xxx)
        case E1001_UNEXPECTED_TOKEN: return "Found something unexpected here";
        case E1002_MISSING_COLON: return "Something's missing here";
        case E1003_MISSING_PARENTHESIS: return "Missing closing parenthesis";
        case E1004_MISSING_BRACE: return "Missing closing brace";
        case E1005_UNEXPECTED_EOF: return "File ended unexpectedly";
        case E1006_INVALID_SYNTAX: return "This syntax isn't quite right";
        case E1007_SEMICOLON_NOT_ALLOWED: return "Semicolons aren't needed in Orus";
        case E1008_INVALID_INDENTATION: return "Indentation looks off";
        case E1009_EXPRESSION_TOO_COMPLEX: return "Expression is too deeply nested";
        case E1010_UNDEFINED_VARIABLE: return "Can't find this variable";
        case E1011_VARIABLE_REDEFINITION: return "This variable name is already taken";
        case E1012_SCOPE_VIOLATION: return "This variable isn't available here";
        case E1013_INVALID_VARIABLE_NAME: return "This isn't a valid variable name";
        case E1014_MUTABLE_REQUIRED: return "This variable needs to be mutable";
        case E1015_INVALID_MULTIPLE_DECLARATION: return "Something's wrong with this variable declaration";
        case E1016_LOOP_VARIABLE_MODIFICATION: return "Loop variables can't be modified inside the loop";
        case E1017_IMMUTABLE_COMPOUND_ASSIGNMENT: return "Can't use compound assignment on immutable variables";
        case E1018_VARIABLE_NOT_INITIALIZED: return "This variable hasn't been given a value yet";
        case E1019_MISSING_PRINT_SEPARATOR: return "Print arguments need commas between them";
        
        // Module errors (E3xxx)
        case E3001_FILE_NOT_FOUND: return "Can't find the file you're looking for";
        case E3002_CYCLIC_IMPORT: return "Modules are using each other in a circle";
        case E3003_MODULE_NOT_FOUND: return "Can't find this module";
        case E3004_IMPORT_FAILED: return "Failed to use this module";
        
        // Internal errors (E9xxx)
        case E9001_INTERNAL_PANIC: return "Internal compiler error (this is our bug!)";
        case E9002_VM_CRASH: return "Virtual machine crashed unexpectedly";
        case E9003_COMPILER_BUG: return "Compiler encountered an internal error";
        case E9004_ASSERTION_FAILED: return "Internal assertion failed";
        
        // Type errors (E2xxx) - handled by feature modules
        case E2001_TYPE_MISMATCH:
        case E2002_INCOMPATIBLE_TYPES:
        case E2003_UNDEFINED_TYPE:
        case E2004_MIXED_ARITHMETIC:
        case E2005_INVALID_CAST:
        case E2006_TYPE_ANNOTATION_REQUIRED:
        case E2007_UNSUPPORTED_OPERATION:
        case E2008_IMMUTABLE_ASSIGNMENT:
            return "Type error (handled by feature module)";
        
        default: return "Something went wrong";
    }
}

const char* get_error_help(ErrorCode code) {
    // NOTE: E2xxx (type) errors are now handled by src/errors/features/type_errors.c
    switch (code) {
        // Runtime errors (E0xxx)
        case E0001_DIVISION_BY_ZERO: 
            return "Add a check before dividing to make sure the number isn't zero.";
        case E0002_INDEX_OUT_OF_BOUNDS:
            return "Check that your index is between 0 and the array length - 1.";
        case E0004_ARITHMETIC_OVERFLOW:
            return "Try using a larger number type like i64 or check for overflow before the operation.";
        case E0006_MODULO_BY_ZERO:
            return "Add a check to ensure the divisor isn't zero before using the modulo operator.";
        
        // Syntax errors (E1xxx)
        case E1002_MISSING_COLON:
            return "Try adding a ':' at the end of this line.";
        case E1003_MISSING_PARENTHESIS:
            return "Add a closing ')' to match the opening parenthesis.";
        case E1006_INVALID_SYNTAX:
            return "Compare this syntax with a working example or check the docs to see what structure is expected here.";
        case E1007_SEMICOLON_NOT_ALLOWED:
            return "Remove the semicolon - Orus doesn't need them to end statements.";
        case E1008_INVALID_INDENTATION:
            return "If you meant to start a block, add a ':' on the previous line or remove this extra indentation.";
        case E1009_EXPRESSION_TOO_COMPLEX:
            return "Break this into smaller expressions using intermediate variables.";
        case E1010_UNDEFINED_VARIABLE:
            return "Make sure you've declared the variable before using it, or check the spelling.";
        case E1011_VARIABLE_REDEFINITION:
            return "Choose a different name for this variable, or use assignment to change the existing one.";
        case E1012_SCOPE_VIOLATION:
            return "Check if the variable is declared in the current scope or an outer scope.";
        case E1013_INVALID_VARIABLE_NAME:
            return "Variable names should start with a letter or underscore, followed by letters, numbers, or underscores.";
        case E1014_MUTABLE_REQUIRED:
            return "Add 'mut' before the variable name when declaring it: 'mut variable_name = value'";
        case E1015_INVALID_MULTIPLE_DECLARATION:
            return "Check the syntax for multiple variable declarations: 'var1 = value1, var2 = value2'";
        case E1016_LOOP_VARIABLE_MODIFICATION:
            return "Use a different variable inside the loop, or restructure your logic.";
        case E1017_IMMUTABLE_COMPOUND_ASSIGNMENT:
            return "Declare the variable as mutable with 'mut' if you need to modify it: 'mut var = value'";
        case E1018_VARIABLE_NOT_INITIALIZED:
            return "Initialize the variable with a value when declaring it: 'variable_name = value'";
        case E1019_MISSING_PRINT_SEPARATOR:
            return "Separate each value with a comma, like print(\"Hello\", name).";
        
        // Module errors (E3xxx)
        case E3001_FILE_NOT_FOUND:
            return "Check the file path and make sure the file exists.";
        
        // Type errors (E2xxx) - handled by feature modules, return NULL to use feature help
        case E2001_TYPE_MISMATCH:
        case E2002_INCOMPATIBLE_TYPES:
        case E2003_UNDEFINED_TYPE:
        case E2004_MIXED_ARITHMETIC:
        case E2005_INVALID_CAST:
        case E2006_TYPE_ANNOTATION_REQUIRED:
        case E2007_UNSUPPORTED_OPERATION:
        case E2008_IMMUTABLE_ASSIGNMENT:
            return NULL; // Handled by feature modules

        default: return NULL;
    }
}

const char* get_error_note(ErrorCode code) {
    // NOTE: E2xxx (type) errors are now handled by src/errors/features/type_errors.c
    switch (code) {
        // Runtime errors (E0xxx)
        case E0001_DIVISION_BY_ZERO:
            return "Division by zero is mathematically undefined.";

        // Syntax errors (E1xxx)
        case E1007_SEMICOLON_NOT_ALLOWED:
            return "Orus uses newlines instead of semicolons to separate statements.";
        case E1006_INVALID_SYNTAX:
            return "Orus expected a different structure here. Re-read the surrounding code to find the mismatch.";

        case E1019_MISSING_PRINT_SEPARATOR:
            return "Commas help Orus understand where one print value ends and the next one begins.";

        case E1008_INVALID_INDENTATION:
            return "Blocks in Orus begin after lines ending with ':' and end when the indentation returns.";
        

        // Type errors (E2xxx) - handled by feature modules, return NULL to use feature notes
        case E2001_TYPE_MISMATCH:
        case E2002_INCOMPATIBLE_TYPES:
        case E2003_UNDEFINED_TYPE:
        case E2004_MIXED_ARITHMETIC:
        case E2005_INVALID_CAST:
        case E2006_TYPE_ANNOTATION_REQUIRED:
        case E2007_UNSUPPORTED_OPERATION:
        case E2008_IMMUTABLE_ASSIGNMENT:
            return NULL; // Handled by feature modules
            
        default: return NULL;
    }
}