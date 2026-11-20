cmake_minimum_required(VERSION 3.20)
project(search_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(engine SearchEngine.cpp)
target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(engine PUBLIC nlohmann_json::nlohmann_json)

add_executable(search_engine main.cpp)
target_link_libraries(search_engine PRIVATE engine)

include(CTest)
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_Declare(googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_executable(search_tests tests.cpp)
    target_link_libraries(search_tests PRIVATE engine GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(search_tests)
endif()
