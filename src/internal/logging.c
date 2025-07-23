// logging.c
// Author: Hierat
// Date: 2023-10-01
// Description: Implementation of configurable logging system

#include "internal/logging.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>  // for isatty

// Global logger configuration
LoggerConfig g_logger = {
    .level = LOG_INFO,
    .output = NULL,  // Will be set to stdout by default
    .enableColors = true,
    .enableTimestamp = false,
    .enableLocation = false,
    .format = NULL
};

// Color codes for different log levels
static const char* LOG_COLORS[] = {
    [LOG_DEBUG] = "\033[36m",  // Cyan
    [LOG_INFO]  = "\033[32m",  // Green
    [LOG_WARN]  = "\033[33m",  // Yellow
    [LOG_ERROR] = "\033[31m"   // Red
};

static const char* LOG_RESET = "\033[0m";

// Log level names
static const char* LOG_LEVEL_NAMES[] = {
    [LOG_DEBUG] = "DEBUG",
    [LOG_INFO]  = "INFO",
    [LOG_WARN]  = "WARN",
    [LOG_ERROR] = "ERROR"
};

// Initialize logger with default configuration
void initLogger(LogLevel level) {
    g_logger.level = level;
    if (g_logger.output == NULL) {
        g_logger.output = stdout;
    }
    
    // Enable colors if output is a terminal
    g_logger.enableColors = isatty(fileno(g_logger.output));
    
    // Load configuration from environment
    loadLoggerConfigFromEnv();
}

// Initialize logger with custom configuration
void initLoggerWithConfig(const LoggerConfig* config) {
    if (config) {
        g_logger = *config;
        if (g_logger.output == NULL) {
            g_logger.output = stdout;
        }
    }
}

// Check if log level is enabled
bool isLogLevelEnabled(LogLevel level) {
    return level >= g_logger.level;
}

// Set log level at runtime
void setLogLevel(LogLevel level) {
    g_logger.level = level;
}

// Set log output stream
void setLogOutput(FILE* output) {
    g_logger.output = output ? output : stdout;
}

// Enable/disable colored output
void setLogColors(bool enable) {
    g_logger.enableColors = enable;
}

// Enable/disable timestamps
void setLogTimestamp(bool enable) {
    g_logger.enableTimestamp = enable;
}

// Enable/disable location information
void setLogLocation(bool enable) {
    g_logger.enableLocation = enable;
}

// Get current timestamp string
static void getCurrentTimestamp(char* buffer, size_t size) {
    time_t now;
    struct tm* timeinfo;
    
    time(&now);
    timeinfo = localtime(&now);
    
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", timeinfo);
}

// Extract filename from full path
static const char* extractFilename(const char* filepath) {
    const char* filename = strrchr(filepath, '/');
    return filename ? filename + 1 : filepath;
}

// Core logging function
void logMessage(LogLevel level, const char* file, int line, const char* format, ...) {
    if (!isLogLevelEnabled(level) || !g_logger.output) {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Color prefix
    if (g_logger.enableColors) {
        fprintf(g_logger.output, "%s", LOG_COLORS[level]);
    }
    
    // Timestamp
    if (g_logger.enableTimestamp) {
        char timestamp[32];
        getCurrentTimestamp(timestamp, sizeof(timestamp));
        fprintf(g_logger.output, "[%s] ", timestamp);
    }
    
    // Log level
    fprintf(g_logger.output, "[%s]", LOG_LEVEL_NAMES[level]);
    
    // Location information
    if (g_logger.enableLocation && file) {
        fprintf(g_logger.output, " %s:%d", extractFilename(file), line);
    }
    
    fprintf(g_logger.output, " ");
    
    // Message content
    vfprintf(g_logger.output, format, args);
    
    // Color reset
    if (g_logger.enableColors) {
        fprintf(g_logger.output, "%s", LOG_RESET);
    }
    
    fprintf(g_logger.output, "\n");
    fflush(g_logger.output);
    
    va_end(args);
}

// Utility functions
const char* logLevelToString(LogLevel level) {
    if (level >= 0 && level < sizeof(LOG_LEVEL_NAMES) / sizeof(LOG_LEVEL_NAMES[0])) {
        return LOG_LEVEL_NAMES[level];
    }
    return "UNKNOWN";
}

const char* logLevelToColorCode(LogLevel level) {
    if (level >= 0 && level < sizeof(LOG_COLORS) / sizeof(LOG_COLORS[0])) {
        return LOG_COLORS[level];
    }
    return "";
}

// Hex dump utility for debugging binary data
void logHexDump(LogLevel level, const char* description, const void* data, size_t size) {
    if (!isLogLevelEnabled(level) || !data || size == 0) {
        return;
    }
    
    const unsigned char* bytes = (const unsigned char*)data;
    
    logMessage(level, NULL, 0, "%s (%zu bytes):", description, size);
    
    for (size_t i = 0; i < size; i += 16) {
        // Print offset
        fprintf(g_logger.output, "%04zx: ", i);
        
        // Print hex bytes
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                fprintf(g_logger.output, "%02x ", bytes[i + j]);
            } else {
                fprintf(g_logger.output, "   ");
            }
        }
        
        fprintf(g_logger.output, " |");
        
        // Print ASCII representation
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            unsigned char c = bytes[i + j];
            fprintf(g_logger.output, "%c", (c >= 32 && c <= 126) ? c : '.');
        }
        
        fprintf(g_logger.output, "|\n");
    }
    
    fflush(g_logger.output);
}

// Load configuration from environment variables
void loadLoggerConfigFromEnv(void) {
    const char* env_level = getenv("ORUS_LOG_LEVEL");
    if (env_level) {
        if (strcmp(env_level, "DEBUG") == 0) {
            g_logger.level = LOG_DEBUG;
        } else if (strcmp(env_level, "INFO") == 0) {
            g_logger.level = LOG_INFO;
        } else if (strcmp(env_level, "WARN") == 0) {
            g_logger.level = LOG_WARN;
        } else if (strcmp(env_level, "ERROR") == 0) {
            g_logger.level = LOG_ERROR;
        }
    }
    
    const char* env_colors = getenv("ORUS_LOG_COLORS");
    if (env_colors) {
        g_logger.enableColors = (strcmp(env_colors, "1") == 0 || 
                                strcmp(env_colors, "true") == 0 ||
                                strcmp(env_colors, "yes") == 0);
    }
    
    const char* env_timestamp = getenv("ORUS_LOG_TIMESTAMP");
    if (env_timestamp) {
        g_logger.enableTimestamp = (strcmp(env_timestamp, "1") == 0 || 
                                   strcmp(env_timestamp, "true") == 0 ||
                                   strcmp(env_timestamp, "yes") == 0);
    }
    
    const char* env_location = getenv("ORUS_LOG_LOCATION");
    if (env_location) {
        g_logger.enableLocation = (strcmp(env_location, "1") == 0 || 
                                  strcmp(env_location, "true") == 0 ||
                                  strcmp(env_location, "yes") == 0);
    }
    
    const char* env_output = getenv("ORUS_LOG_OUTPUT");
    if (env_output) {
        if (strcmp(env_output, "stderr") == 0) {
            g_logger.output = stderr;
        } else if (strcmp(env_output, "stdout") == 0) {
            g_logger.output = stdout;
        } else {
            // Try to open as file
            FILE* logfile = fopen(env_output, "a");
            if (logfile) {
                g_logger.output = logfile;
                g_logger.enableColors = false; // Disable colors for file output
            }
        }
    }
}

// Cleanup function
void shutdownLogger(void) {
    if (g_logger.output && g_logger.output != stdout && g_logger.output != stderr) {
        fclose(g_logger.output);
        g_logger.output = stdout;
    }
}