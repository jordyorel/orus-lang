//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: include/internal/strutil.h
//  Author: Jordy Orel KONDA
//  Copyright (c) 2025 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: String utility declarations for internal formatting and manipulation helpers.

#ifndef ORUS_STRUTIL_H
#define ORUS_STRUTIL_H

#include <stdlib.h>
#include <string.h>

static inline char* orus_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

#endif // ORUS_STRUTIL_H
