#ifndef STCC_STDARG_H
#define STCC_STDARG_H
/* stcc's word-aligned, caller-cleaned ABI. Fetch promoted types only. */
typedef char *va_list;
#define __STCC_SLOT(T) ((sizeof(T) + 1UL) & ~1UL)
#define va_start(ap, last) ((ap) = (char *)&(last) + __STCC_SLOT(last))
#define va_arg(ap, T) (*(T *)((ap += __STCC_SLOT(T)) - __STCC_SLOT(T)))
#define va_end(ap) ((ap) = (va_list)0)
#endif
