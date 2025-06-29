#ifndef ORUS_COMMON_H
#define ORUS_COMMON_H

#include<stdbool.h>
#include<stddef.h>
#include<stdint.h>

// Maximum number of global variables (256 since we're using uint8_t for indices)
#define UINT8_COUNT 256

// Make sure UINT8_MAX is defined
#ifndef UINT8_MAX
#define UINT8_MAX 255
#endif

// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_PRINT_CODE

#endif