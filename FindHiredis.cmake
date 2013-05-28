# FindHiredis.cmake - Try to find the Hiredis library
# Once done this will define
#
#  HIREDIS_FOUND - System has Hiredis
#  HIREDIS_INCLUDE_DIR - The Hiredis include directory
#  HIREDIS_LIBRARIES - The libraries needed to use Hiredis
#  HIREDIS_DEFINITIONS - Compiler switches required for using Hiredis


# use pkg-config to get the directories and then use these values
# in the FIND_PATH() and FIND_RARY() calls
FIND_PACKAGE(PkgConfig)
PKG_SEARCH_MODULE(PC_HIREDIS REQUIRED hiredis)

SET(HIREDIS_DEFINITIONS ${PC_HIREDIS_CFLAGS_OTHER})

FIND_PATH(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h
   HINTS
   ${PC_HIREDIS_INCLUDEDIR}
   ${PC_HIREDIS_INCLUDE_DIRS}
   PATH_SUFFIXES hiredis
   )

FIND_LIBRARY(HIREDIS_LIBRARIES NAMES hiredis
   HINTS
   ${PC_HIREDIS_DIR}
   ${PC_HIREDIS_LIBRARY_DIRS}
   )


INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Hiredis DEFAULT_MSG HIREDIS_LIBRARIES HIREDIS_INCLUDE_DIR)

MARK_AS_ADVANCED(HIREDIS_INCLUDE_DIR HIREDIS_LIBRARIES)
