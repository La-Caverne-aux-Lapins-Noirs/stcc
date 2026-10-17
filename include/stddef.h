#ifndef STCC_STDDEF_H
#define STCC_STDDEF_H
#ifndef STCC_SIZE_T
#define STCC_SIZE_T
typedef unsigned long size_t;
#endif
typedef long ptrdiff_t;
typedef unsigned short wchar_t;
#define NULL ((void *)0)
#define offsetof(T, M) ((size_t) & (((T *)0)->M))
#endif
