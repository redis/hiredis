CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
INCLUDE(GNUInstallDirs)
PROJECT(redisearch)

SET(ENABLE_EXAMPLES OFF CACHE BOOL "Enable building hiredis examples")

# Get the version numbers
MACRO(getVersionBit name)

EXECUTE_PROCESS(
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMAND grep "${name}" hiredis.h COMMAND awk "{print $3}"
    OUTPUT_VARIABLE "${name}"
    OUTPUT_STRIP_TRAILING_WHITESPACE)
ENDMACRO(getVersionBit)

getVersionBit(HIREDIS_MAJOR)
getVersionBit(HIREDIS_MINOR)
getVersionBit(HIREDIS_PATCH)

SET(VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")
MESSAGE("Detected version: ${VERSION}")

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR})

ADD_LIBRARY(hiredis SHARED
    async.c
    dict.c
    hiredis.c
    net.c
    read.c
    sds.c)

SET_TARGET_PROPERTIES(hiredis
    PROPERTIES
    VERSION "${VERSION}")

CONFIGURE_FILE(hiredis.pc.in hiredis.pc @ONLY)

INSTALL(TARGETS hiredis
    DESTINATION "${CMAKE_INSTALL_LIBDIR}")

INSTALL(FILES hiredis.h read.h sds.h async.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

INSTALL(FILES ${CMAKE_CURRENT_BINARY_DIR}/hiredis.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

ENABLE_TESTING()
ADD_EXECUTABLE(hiredis-test
    test.c)
TARGET_LINK_LIBRARIES(hiredis-test hiredis)
ADD_TEST(NAME hiredis-test
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh)

# Add examples
IF(ENABLE_EXAMPLES)
  ADD_SUBDIRECTORY(examples)
ENDIF(ENABLE_EXAMPLES)
