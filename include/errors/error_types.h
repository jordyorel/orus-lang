#ifndef ORUS_ERROR_TYPES_H
#define ORUS_ERROR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "../error_reporting.h"  // Use existing error codes
#include "../vm.h"               // For SrcLocation

// Error feature categories
typedef enum {
    ERROR_FEATURE_RUNTIME,
    ERROR_FEATURE_SYNTAX,
    ERROR_FEATURE_TYPE,
    ERROR_FEATURE_MODULE,
    ERROR_FEATURE_INTERNAL
} ErrorFeature;

// Feature error information
typedef struct {
    ErrorCode code;
    const char* title;
    const char* help;
    const char* note;
} FeatureErrorInfo;

#endif // ORUS_ERROR_TYPES_H