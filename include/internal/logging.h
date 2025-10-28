//  Orus Language Project

#ifndef ORUS_LOGGING_H
#define ORUS_LOGGING_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#if defined(__APPLE__)
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

// Log levels in order of increasing severity
typedef enum {
    LOG_DEBUG,   // Detailed debugging information
    LOG_INFO,    // General information about program execution
    LOG_WARN,    // Warning conditions
    LOG_ERROR    // Error conditions
} LogLevel;

// Logger configuration
typedef struct {
    LogLevel level;           // Minimum level to log
    FILE* output;            // Output stream (stdout, stderr, or file)
    bool enableColors;       // Enable colored output
    bool enableTimestamp;    // Include timestamps in log messages
    bool enableLocation;     // Include file:line information
    const char* format;      // Custom format string (optional)
} LoggerConfig;

// Global logger instance
extern LoggerConfig g_logger;

// Initialize logger with specified level and configuration
void initLogger(LogLevel level);
void initLoggerWithConfig(const LoggerConfig* config);

// Core logging function
void logMessage(LogLevel level, const char* file, int line, const char* format, ...);

// Log level checking (for performance optimization)
bool isLogLevelEnabled(LogLevel level);

// Set log level at runtime
void setLogLevel(LogLevel level);

// Set log output stream
void setLogOutput(FILE* output);

// Enable/disable features
void setLogColors(bool enable);
void setLogTimestamp(bool enable);
void setLogLocation(bool enable);

// Convenience macros for different log levels
#define LOG_DEBUG(fmt, ...) \
    do { \
        if (isLogLevelEnabled(LOG_DEBUG)) { \
            logMessage(LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_INFO(fmt, ...) \
    do { \
        if (isLogLevelEnabled(LOG_INFO)) { \
            logMessage(LOG_INFO, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_WARN(fmt, ...) \
    do { \
        if (isLogLevelEnabled(LOG_WARN)) { \
            logMessage(LOG_WARN, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (isLogLevelEnabled(LOG_ERROR)) { \
            logMessage(LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__); \
        } \
    } while(0)

// Specialized debug macros for compiler phases
#define LOG_COMPILER_DEBUG(phase, fmt, ...) \
    LOG_DEBUG("[%s] " fmt, phase, ##__VA_ARGS__)

#define LOG_VM_DEBUG(component, fmt, ...) \
    LOG_DEBUG("[VM:%s] " fmt, component, ##__VA_ARGS__)

// Conditional logging based on build configuration
#ifdef DEBUG_MODE
    #define LOG_DEBUG_ENABLED 1
#else
    #define LOG_DEBUG_ENABLED 0
#endif

// Performance-aware debug logging (compiled out in release builds)
#if LOG_DEBUG_ENABLED
    #define LOG_PERF_DEBUG(fmt, ...) LOG_DEBUG(fmt, ##__VA_ARGS__)
#else
    #define LOG_PERF_DEBUG(fmt, ...) ((void)0)
#endif

// Scoped logging for function entry/exit
#define LOG_FUNCTION_ENTRY() \
    LOG_DEBUG("Entering %s", __func__)

#define LOG_FUNCTION_EXIT() \
    LOG_DEBUG("Exiting %s", __func__)

// Scoped logger that automatically logs function entry/exit
typedef struct {
    const char* function_name;
} ScopedLogger;

static inline ScopedLogger createScopedLogger(const char* func) {
    LOG_DEBUG("Entering %s", func);
    return (ScopedLogger){.function_name = func};
}

static inline void destroyScopedLogger(ScopedLogger* logger) {
    LOG_DEBUG("Exiting %s", logger->function_name);
}

// RAII-style scoped logging
#define LOG_SCOPE() \
    ScopedLogger __scoped_logger __attribute__((cleanup(destroyScopedLogger))) = createScopedLogger(__func__)

// Utility functions
const char* logLevelToString(LogLevel level);
const char* logLevelToColorCode(LogLevel level);
void logHexDump(LogLevel level, const char* description, const void* data, size_t size);

// Configuration from environment variables
void loadLoggerConfigFromEnv(void);

// Cleanup function
void shutdownLogger(void);

#endif // ORUS_LOGGING_H
