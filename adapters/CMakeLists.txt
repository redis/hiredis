find_package(Qt5Core)
IF (Qt5Core_FOUND)
    set(CMAKE_AUTOMOC ON)
    ADD_LIBRARY(hiredis-qt-adapter SHARED qt.h)
    TARGET_COMPILE_DEFINITIONS(hiredis-qt-adapter PRIVATE ${Qt5Core_DEFINITIONS})
    TARGET_INCLUDE_DIRECTORIES(hiredis-qt-adapter PRIVATE ${Qt5Core_INCLUDES})
    TARGET_LINK_LIBRARIES(hiredis-qt-adapter hiredis ${Qt5Core_LIBRARIES})

    ADD_LIBRARY(hiredis-qt-adapter_static STATIC qt.h)
    TARGET_COMPILE_DEFINITIONS(hiredis-qt-adapter_static PRIVATE ${Qt5Core_DEFINITIONS})
    TARGET_INCLUDE_DIRECTORIES(hiredis-qt-adapter_static PRIVATE ${Qt5Core_INCLUDES})
    TARGET_LINK_LIBRARIES(hiredis-qt-adapter_static hiredis ${Qt5Core_LIBRARIES})

    INSTALL(TARGETS hiredis-qt-adapter hiredis-qt-adapter_static
        EXPORT hiredis-qt-adapter-targets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
ENDIF()
