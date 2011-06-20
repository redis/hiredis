/*
Special config for MSVC build of hiredis

Does absolutely nothing for Unix!
*/
#if defined(_MSC_VER)
#define HIREDIS_WIN 1

#ifndef va_copy
/* WARNING - DANGER - ASSUMES TYPICAL STACK MACHINE */
#define va_copy(dst, src) ((void)((dst) = (src)))
#endif

#define inline __inline

#define strcasecmp strcmp
#define strncasecmp _strnicmp
#define snprintf sprintf_s

#include <winsock2.h>
#include <ws2tcpip.h>
#define HIREDIS_USE_WINSOCK2	1
#define close closesocket
#define SETERRNO errnox = WSAGetLastError()
#define EINPROGRESS WSAEINPROGRESS
#define ETIMEDOUT WSAETIMEDOUT
#define strerror_r(errorno, buf, len) strerror_s(buf, len, errorno)

#else

#define SETERRNO

#endif

