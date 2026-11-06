cmake_minimum_required(VERSION 3.24)

project(mangen LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(CTest)

if(BUILD_TESTING)
    enable_language(CXX)

    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)

    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

    add_subdirectory(libs/third_party/googletest EXCLUDE_FROM_ALL)
endif()

add_subdirectory(libs/matcher)
add_subdirectory(libs/queue)
add_subdirectory(libs/catalog)
