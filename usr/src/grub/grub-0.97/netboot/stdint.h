#ifndef STDINT_H
#define STDINT_H
/* 
 * I'm architecture depended. Check me before port GRUB
 */
typedef unsigned           size_t;

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned long      uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed long        int32_t;
typedef signed long long   int64_t;

#endif /* STDINT_H */
