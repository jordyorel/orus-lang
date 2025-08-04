#include "debug/debug_config.h"
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// Global debug configuration
DebugConfig g_debug_config = {
    .enabled_categories = DEBUG_NONE,
    .use_colors = true,
    .show_timestamps = false,
    .show_thread_id = false,
    .show_file_location = false,
    .output_stream = NULL,  // Will be set to stdout in debug_init
    .verbosity_level = 1
};

// Category names for string parsing and display
static const struct {
    DebugCategory category;
    const char* name;
    const char* color;
} category_info[] = {
    {DEBUG_CODEGEN, "codegen", DEBUG_COLOR_CYAN},
    {DEBUG_CONSTANTFOLD, "constantfold", DEBUG_COLOR_GREEN},
    {DEBUG_TYPE_INFERENCE, "type", DEBUG_COLOR_BLUE},
    {DEBUG_PARSER, "parser", DEBUG_COLOR_YELLOW},
    {DEBUG_LEXER, "lexer", DEBUG_COLOR_MAGENTA},
    {DEBUG_VM, "vm", DEBUG_COLOR_RED},
    {DEBUG_VM_DISPATCH, "dispatch", DEBUG_COLOR_RED},
    {DEBUG_REGISTER_ALLOC, "regalloc", DEBUG_COLOR_CYAN},
    {DEBUG_OPTIMIZER, "optimizer", DEBUG_COLOR_GREEN},
    {DEBUG_PEEPHOLE, "peephole", DEBUG_COLOR_GREEN},
    {DEBUG_SYMBOL_TABLE, "symbols", DEBUG_COLOR_YELLOW},
    {DEBUG_MEMORY, "memory", DEBUG_COLOR_RED},
    {DEBUG_GC, "gc", DEBUG_COLOR_RED},
    {DEBUG_RUNTIME, "runtime", DEBUG_COLOR_WHITE},
    {DEBUG_PROFILING, "profiling", DEBUG_COLOR_WHITE},
    {DEBUG_ERROR, "error", DEBUG_COLOR_RED},
    {DEBUG_CONFIG, "config", DEBUG_COLOR_BLUE},
    {DEBUG_MAIN, "main", DEBUG_COLOR_WHITE},
    {DEBUG_REPL, "repl", DEBUG_COLOR_CYAN},
    {DEBUG_LOGGING, "logging", DEBUG_COLOR_YELLOW},
    {DEBUG_TYPED_AST, "typed_ast", DEBUG_COLOR_BLUE},
};

static const size_t category_count = sizeof(category_info) / sizeof(category_info[0]);

void debug_init(void) {
    if (g_debug_config.output_stream == NULL) {
        g_debug_config.output_stream = stdout;
    }
    
    // Check environment variables for default configuration
    const char* debug_env = getenv("ORUS_DEBUG");
    if (debug_env) {
        g_debug_config.enabled_categories = debug_parse_categories(debug_env);
    }
    
    const char* debug_colors = getenv("ORUS_DEBUG_COLORS");
    if (debug_colors) {
        g_debug_config.use_colors = (strcmp(debug_colors, "1") == 0 || 
                                    strcmp(debug_colors, "true") == 0 ||
                                    strcmp(debug_colors, "yes") == 0);
    }
    
    const char* debug_timestamps = getenv("ORUS_DEBUG_TIMESTAMPS");
    if (debug_timestamps) {
        g_debug_config.show_timestamps = (strcmp(debug_timestamps, "1") == 0 ||
                                         strcmp(debug_timestamps, "true") == 0 ||
                                         strcmp(debug_timestamps, "yes") == 0);
    }
    
    const char* debug_verbosity = getenv("ORUS_DEBUG_VERBOSITY");
    if (debug_verbosity) {
        g_debug_config.verbosity_level = atoi(debug_verbosity);
    }
}

void debug_shutdown(void) {
    if (g_debug_config.output_stream && g_debug_config.output_stream != stdout && 
        g_debug_config.output_stream != stderr) {
        fclose(g_debug_config.output_stream);
    }
    g_debug_config.output_stream = NULL;
}

void debug_enable_category(DebugCategory category) {
    g_debug_config.enabled_categories |= category;
}

void debug_disable_category(DebugCategory category) {
    g_debug_config.enabled_categories &= ~category;
}

void debug_set_categories(unsigned int categories) {
    g_debug_config.enabled_categories = categories;
}

bool debug_is_enabled(DebugCategory category) {
    return (g_debug_config.enabled_categories & category) != 0;
}

void debug_set_colors(bool enable) {
    g_debug_config.use_colors = enable;
}

void debug_set_timestamps(bool enable) {
    g_debug_config.show_timestamps = enable;
}

void debug_set_file_location(bool enable) {
    g_debug_config.show_file_location = enable;
}

void debug_set_output_stream(FILE* stream) {
    if (g_debug_config.output_stream && g_debug_config.output_stream != stdout && 
        g_debug_config.output_stream != stderr) {
        fclose(g_debug_config.output_stream);
    }
    g_debug_config.output_stream = stream;
}

void debug_set_verbosity(int level) {
    g_debug_config.verbosity_level = level;
}

void debug_enable_compiler(void) {
    debug_enable_category(DEBUG_COMPILER);
}

void debug_enable_vm(void) {
    debug_enable_category(DEBUG_VM_ALL);
}

void debug_enable_all(void) {
    g_debug_config.enabled_categories = DEBUG_ALL;
}

void debug_disable_all(void) {
    g_debug_config.enabled_categories = DEBUG_NONE;
}

const char* debug_category_name(DebugCategory category) {
    for (size_t i = 0; i < category_count; i++) {
        if (category_info[i].category == category) {
            return category_info[i].name;
        }
    }
    return "unknown";
}

static const char* get_category_color(DebugCategory category) {
    for (size_t i = 0; i < category_count; i++) {
        if (category_info[i].category == category) {
            return category_info[i].color;
        }
    }
    return DEBUG_COLOR_WHITE;
}

unsigned int debug_parse_categories(const char* categories_str) {
    if (!categories_str) return DEBUG_NONE;
    
    unsigned int result = DEBUG_NONE;
    char* str_copy = strdup(categories_str);
    if (!str_copy) return DEBUG_NONE;
    
    // Handle special cases
    if (strcmp(str_copy, "all") == 0) {
        free(str_copy);
        return DEBUG_ALL;
    }
    if (strcmp(str_copy, "compiler") == 0) {
        free(str_copy);
        return DEBUG_COMPILER;
    }
    if (strcmp(str_copy, "vm") == 0) {
        free(str_copy);
        return DEBUG_VM_ALL;
    }
    
    // Parse comma-separated list
    char* token = strtok(str_copy, ",");
    while (token != NULL) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';
        
        // Find matching category
        for (size_t i = 0; i < category_count; i++) {
            if (strcmp(token, category_info[i].name) == 0) {
                result |= category_info[i].category;
                break;
            }
        }
        
        token = strtok(NULL, ",");
    }
    
    free(str_copy);
    return result;
}

void debug_printf(DebugCategory category, const char* format, ...) {
    va_list args;
    va_start(args, format);
    
    if (!debug_is_enabled(category) || g_debug_config.verbosity_level < 1) {
        va_end(args);
        return;
    }
    
    if (!g_debug_config.output_stream) {
        va_end(args);
        return;
    }
    
    // Print timestamp if enabled
    if (g_debug_config.show_timestamps) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        fprintf(g_debug_config.output_stream, "[%02d:%02d:%02d] ", 
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    
    // Print category with color if enabled
    if (g_debug_config.use_colors) {
        const char* color = get_category_color(category);
        fprintf(g_debug_config.output_stream, "%s[%s]%s ", 
                color, debug_category_name(category), DEBUG_COLOR_RESET);
    } else {
        fprintf(g_debug_config.output_stream, "[%s] ", debug_category_name(category));
    }
    
    // Print the actual message
    vfprintf(g_debug_config.output_stream, format, args);
    va_end(args);
    
    // Ensure output is flushed for immediate visibility
    fflush(g_debug_config.output_stream);
}

void debug_printf_verbose(DebugCategory category, int required_verbosity, const char* format, ...) {
    if (!debug_is_enabled(category) || g_debug_config.verbosity_level < required_verbosity) {
        return;
    }
    
    if (!g_debug_config.output_stream) {
        return;
    }
    
    // Print timestamp if enabled
    if (g_debug_config.show_timestamps) {
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        fprintf(g_debug_config.output_stream, "[%02d:%02d:%02d] ", 
                tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    }
    
    // Print category with color if enabled
    if (g_debug_config.use_colors) {
        const char* color = get_category_color(category);
        fprintf(g_debug_config.output_stream, "%s[%s]%s ", 
                color, debug_category_name(category), DEBUG_COLOR_RESET);
    } else {
        fprintf(g_debug_config.output_stream, "[%s] ", debug_category_name(category));
    }
    
    // Print the actual message
    va_list args;
    va_start(args, format);
    vfprintf(g_debug_config.output_stream, format, args);
    va_end(args);
    
    // Ensure output is flushed for immediate visibility
    fflush(g_debug_config.output_stream);
}