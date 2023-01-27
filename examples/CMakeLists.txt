INCLUDE(FindPkgConfig)
# Check for GLib

PKG_CHECK_MODULES(GLIB2 glib-2.0)
if (GLIB2_FOUND)
    INCLUDE_DIRECTORIES(${GLIB2_INCLUDE_DIRS})
    LINK_DIRECTORIES(${GLIB2_LIBRARY_DIRS})
    ADD_EXECUTABLE(example-glib example-glib.c)
    TARGET_LINK_LIBRARIES(example-glib ${HIREDIS_DEFAULT_LIBRARY} ${GLIB2_LIBRARIES})
ENDIF(GLIB2_FOUND)

FIND_PATH(LIBEV ev.h
    HINTS /usr/local /usr/opt/local
    ENV LIBEV_INCLUDE_DIR)

if (LIBEV)
    # Just compile and link with libev
    ADD_EXECUTABLE(example-libev example-libev.c)
    TARGET_LINK_LIBRARIES(example-libev ${HIREDIS_DEFAULT_LIBRARY} ev)
ENDIF()

FIND_PATH(LIBEVENT event.h)
if (LIBEVENT)
    ADD_EXECUTABLE(example-libevent example-libevent.c)
    TARGET_LINK_LIBRARIES(example-libevent ${HIREDIS_DEFAULT_LIBRARY} event)
ENDIF()

FIND_PATH(LIBHV hv/hv.h)
IF (LIBHV)
    ADD_EXECUTABLE(example-libhv example-libhv.c)
    TARGET_LINK_LIBRARIES(example-libhv ${HIREDIS_DEFAULT_LIBRARY} hv)
ENDIF()

FIND_PATH(LIBUV uv.h)
IF (LIBUV)
    ADD_EXECUTABLE(example-libuv example-libuv.c)
    TARGET_LINK_LIBRARIES(example-libuv ${HIREDIS_DEFAULT_LIBRARY} uv)
ENDIF()

FIND_PATH(LIBSDEVENT systemd/sd-event.h)
IF (LIBSDEVENT)
    ADD_EXECUTABLE(example-libsdevent example-libsdevent.c)
    TARGET_LINK_LIBRARIES(example-libsdevent ${HIREDIS_DEFAULT_LIBRARY} systemd)
ENDIF()

IF (APPLE)
    FIND_LIBRARY(CF CoreFoundation)
    ADD_EXECUTABLE(example-macosx example-macosx.c)
    TARGET_LINK_LIBRARIES(example-macosx ${HIREDIS_DEFAULT_LIBRARY} ${CF})
ENDIF()

IF (ENABLE_SSL)
    ADD_EXECUTABLE(example-ssl example-ssl.c)
    TARGET_LINK_LIBRARIES(example-ssl ${HIREDIS_DEFAULT_LIBRARY} ${HIREDIS_SSL_DEFAULT_LIBRARY})
ENDIF()

ADD_EXECUTABLE(example example.c)
TARGET_LINK_LIBRARIES(example ${HIREDIS_DEFAULT_LIBRARY})

ADD_EXECUTABLE(example-push example-push.c)
TARGET_LINK_LIBRARIES(example-push ${HIREDIS_DEFAULT_LIBRARY})
