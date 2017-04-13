add_executable(example example.c)
target_link_libraries(example ${LIBHIREDIS_STATIC})
