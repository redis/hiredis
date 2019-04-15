INCLUDE(CTest)
INCLUDE_DIRECTORIES(${gtest_SOURCE_DIR}/include)
INCLUDE_DIRECTORIES(PROJECT_SOURCE_DIR)
INCLUDE_DIRECTORIES(${LIBEVENT_INCLUDES})

ADD_EXECUTABLE(hiredis-gtest t_basic.cpp t_client.cpp t_async.cpp
    common.cpp)
TARGET_LINK_LIBRARIES(hiredis-gtest gtest_main hiredis event)
ADD_TEST(NAME hiredis-test COMMAND hiredis-test)
SET_PROPERTY(TARGET hiredis-gtest PROPERTY CXX_STANDARD 11)