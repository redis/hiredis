/*
Special config for MSVC build of hiredis

Does absolutely nothing for Unix!
*/
#ifndef __CONFIG_H
#define __CONFIG_H

#include "hiredis.h"
#ifdef _WIN32

#ifndef va_copy
/* WARNING - DANGER - ASSUMES TYPICAL STACK MACHINE */
#define va_copy(dst, src) ((void)((dst) = (src)))
#endif

#if defined( _MSC_VER ) && !defined( __cplusplus )
#define inline __inline
#endif

#define EINPROGRESS WSAEINPROGRESS

#define snprintf sprintf_s
#define strcasecmp strcmp
#define strncasecmp _strnicmp
#define strerror_r(errorno, buf, len) strerror_s(buf, len, errorno)

#endif

#endif