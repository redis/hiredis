#ifndef __HIREDIS_FMACRO_H
#define __HIREDIS_FMACRO_H

#if defined(__linux__)
#define _BSD_SOURCE
#define _DEFAULT_SOURCE
#endif

#if defined(__sun__)
#define _POSIX_C_SOURCE 200112L
#else
#define _XOPEN_SOURCE 600
#endif

#if __APPLE__ && __MACH__
#define _OSX
#endif

#endif
