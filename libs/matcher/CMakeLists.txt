if(NOT TARGET utf8proc)
    set(BUILD_SHARED_LIBS OFF)
    set(UTF8PROC_INSTALL OFF CACHE BOOL "" FORCE)
    set(UTF8PROC_ENABLE_TESTING OFF CACHE BOOL "" FORCE)

    add_subdirectory(
        "${PROJECT_SOURCE_DIR}/libs/third_party/utf8proc"
        "${PROJECT_BINARY_DIR}/third_party/utf8proc"
        EXCLUDE_FROM_ALL
    )
endif()

add_library(mangen_matcher STATIC
    src/utf8.c
    src/pattern.c
    src/nfa.c
    src/dfa.c
    src/min_dfa.c
    src/match.c
    src/graphviz.c
)

target_compile_features(mangen_matcher
    PUBLIC c_std_17
)

target_include_directories(mangen_matcher
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(mangen_matcher
    PRIVATE
        utf8proc::utf8proc
)

if(BUILD_TESTING)
    enable_language(CXX)

    if(NOT TARGET GTest::gtest_main)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

        add_subdirectory(
            "${PROJECT_SOURCE_DIR}/libs/third_party/googletest"
            "${PROJECT_BINARY_DIR}/third_party/googletest"
            EXCLUDE_FROM_ALL
        )
    endif()

    add_subdirectory(tests)
endif()
