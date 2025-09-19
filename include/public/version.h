#ifndef ORUS_VERSION_H
#define ORUS_VERSION_H

#define ORUS_VERSION_MAJOR 0
#define ORUS_VERSION_MINOR 5
#define ORUS_VERSION_PATCH 1

// Helpers to stringify numeric macros
#define ORUS_STR_HELPER(x) #x
#define ORUS_STR(x) ORUS_STR_HELPER(x)

// Canonical version string derived from MAJOR.MINOR.PATCH
#define ORUS_VERSION_STRING ORUS_STR(ORUS_VERSION_MAJOR) "." ORUS_STR(ORUS_VERSION_MINOR) "." ORUS_STR(ORUS_VERSION_PATCH)

#endif // ORUS_VERSION_H
