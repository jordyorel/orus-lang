#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <stddef.h>

// String utility functions
char* strdup_safe(const char* str);
char* substring(const char* str, size_t start, size_t length);
int count_occurrences(const char* str, char ch);
void trim_whitespace(char* str);

#endif // STRING_UTILS_H
