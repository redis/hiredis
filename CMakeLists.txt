CMAKE_MINIMUM_REQUIRED(VERSION 3.0.0)
INCLUDE(GNUInstallDirs)
PROJECT(redisearch)

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

MESSAGE("Detected version: ${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")

ADD_LIBRARY(hiredis SHARED
    async.c
    dict.c
    hiredis.c
    net.c
    read.c
    sds.c)

SET_TARGET_PROPERTIES(hiredis
    PROPERTIES
    VERSION "${HIREDIS_MAJOR}.${HIREDIS_MINOR}.${HIREDIS_PATCH}")

INSTALL(TARGETS hiredis
    DESTINATION "${CMAKE_INSTALL_LIBDIR}")

INSTALL(FILES hiredis.h read.h sds.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hiredis)

# Add tests: Currently, I don't know how to make the tests actually run
# without hanging!
ENABLE_TESTING()
ADD_EXECUTABLE(hiredis-test
    test.c)
TARGET_LINK_LIBRARIES(hiredis-test hiredis)

# Add examples

