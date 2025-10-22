cmake_minimum_required(VERSION 3.10)
project(cpp_channels VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(test_channel_1 ${CMAKE_CURRENT_SOURCE_DIR}/test/test_channel_1.cpp)
target_include_directories(test_channel_1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

target_link_libraries(test_channel_1 PRIVATE Threads::Threads GTest::gtest_main)
target_compile_options(test_channel_1 PRIVATE -g -O0)

add_test(NAME test_channel_1 COMMAND test_channel_1)
