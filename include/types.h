
#ifndef _TYPES_H
#define _TYPES_H

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;

typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;


typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;
typedef int64_t             ptrdiff_t;

typedef enum { false = 0, true = 1 } bool;

#define NULL ((void*)0)

#define UINT8_MAX   0xFFU
#define UINT16_MAX  0xFFFFU
#define UINT32_MAX  0xFFFFFFFFU
#define UINT64_MAX  0xFFFFFFFFFFFFFFFFULL
#define SIZE_MAX    UINT64_MAX
#define INT64_MIN   (-9223372036854775807LL - 1)
#define INT64_MAX   9223372036854775807LL

#endif

