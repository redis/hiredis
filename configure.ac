AC_INIT([hiredis], [0.11])
AM_INIT_AUTOMAKE([foreign])
AC_CONFIG_MACRO_DIR([m4])
LT_INIT

PKG_PROG_PKG_CONFIG

#AC_ARG_WITH([libev], AS_HELP_STRING([--with-libev], 
#                                    [Build with libev]))

PKG_CHECK_MODULES([LIBEV], [libev], [HAVE_LIBEV=1], [HAVE_LIBEV=0])

AM_CONDITIONAL([USE_LIBEV], [test $HAVE_LIBEV -eq 1])
#AC_SUBST(LIBEV)
#AC_SUBST(LIBEV_CFLAGS)
#AC_SUBST(LIBEV_LIBS)

#AC_ARG_WITH([libevent], AS_HELP_STRING([--with-libevent], 
#                                       [Build with libevent]))

PKG_CHECK_MODULES([LIBEVENT], [libevent], [HAVE_LIBEVENT=1], [HAVE_LIBEVENT=0])
AM_CONDITIONAL([USE_LIBEVENT], [test $HAVE_LIBEVENT -eq 1])
#AC_SUBST(LIBEVENT)
#AC_SUBST(LIBEVENT_CFLAGS)
#AC_SUBST(LIBEVENT_LIBS)

AM_CONDITIONAL([MYVAL], [test 1 -eq 1])
AM_CONDITIONAL([MYVAL2], [test foo = foo])
#AC_SUBST(MYVAL)
#AC_SUBST(MYVAL2)


AC_CONFIG_SRCDIR([hiredis.c])
AC_CONFIG_FILES([hiredis.pc
                 Makefile])
AC_OUTPUT
