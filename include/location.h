#ifndef ORUS_LOCATION_H
#define ORUS_LOCATION_H

typedef struct {
    const char* file;
    int line;
    int column;
} SrcLocation;

#endif // ORUS_LOCATION_H
