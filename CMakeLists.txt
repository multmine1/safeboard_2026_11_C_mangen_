cmake_minimum_required(VERSION 3.24)

project(mangen LANGUAGES C)

set(MANGEN_SOURCE_COMMIT "unknown")

if(EXISTS "${PROJECT_SOURCE_DIR}/VERSION-COMMIT")
    file(STRINGS
        "${PROJECT_SOURCE_DIR}/VERSION-COMMIT"
        MANGEN_SOURCE_COMMIT
        LIMIT_COUNT 1
    )
else()
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE MANGEN_GIT_RESULT
        OUTPUT_VARIABLE MANGEN_GIT_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    if(MANGEN_GIT_RESULT EQUAL 0)
        set(MANGEN_SOURCE_COMMIT "${MANGEN_GIT_OUTPUT}")
    endif()
endif()

if(PROJECT_VERSION)
    set(MANGEN_VERSION "${PROJECT_VERSION}")
else()
    set(MANGEN_VERSION "dev")
endif()

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
add_subdirectory(libs/hash)
add_subdirectory(libs/hash_pool)
add_subdirectory(libs/walker)
add_subdirectory(libs/manifest)

add_subdirectory(app)

target_compile_definitions(mangen PRIVATE
    "MANGEN_SOURCE_COMMIT=\"${MANGEN_SOURCE_COMMIT}\""
    "MANGEN_VERSION=\"${MANGEN_VERSION}\""
)

