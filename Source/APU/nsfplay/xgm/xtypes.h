#ifndef _XTYPES_H_
#define _XTYPES_H_

#if defined(_MSC_VER)
#define INLINE __forceinline
#elif defined(__GNUC__)
#define INLINE __inline__
#elif defined(_MWERKS_)
#define INLINE inline
#else
#define INLINE
#endif

namespace xgm
{
/** s */
typedef unsigned char UINT8;
/** s */
typedef unsigned short UINT16;
/** 32bit */
typedef unsigned int UINT32;
/** 64bit unsigned */
typedef unsigned long long UINT64;
/** f */
typedef signed char INT8;
/** f */
typedef signed short INT16;
/** f */
typedef signed int INT32;
/** 64bit signed */
typedef signed long long INT64;
}

#endif