//  Orus Language Project
//  ---------------------------------------------------------------------------
//  File: include/public/version.h
//  Author: Jordy Orel KONDA
//  Copyright (c) 2025 Jordy Orel KONDA
//  License: MIT License (see LICENSE file in the project root)
//  Description: Exposes Orus semantic version metadata and helper routines.


#ifndef ORUS_VERSION_H
#define ORUS_VERSION_H

#define ORUS_VERSION_MAJOR 0
#define ORUS_VERSION_MINOR 7
#define ORUS_VERSION_PATCH 6

// Helpers to stringify numeric macros
#define ORUS_STR_HELPER(x) #x
#define ORUS_STR(x) ORUS_STR_HELPER(x)

// Canonical version string derived from MAJOR.MINOR.PATCH
#define ORUS_VERSION_STRING ORUS_STR(ORUS_VERSION_MAJOR) "." ORUS_STR(ORUS_VERSION_MINOR) "." ORUS_STR(ORUS_VERSION_PATCH)

#endif // ORUS_VERSION_H
