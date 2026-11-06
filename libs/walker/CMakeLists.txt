add_library(mangen_walker STATIC
    src/walker.c
    src/walker_queue.c
    src/walker_path.c
    src/walker_scan.c
    src/walker_worker.c
)

target_compile_features(mangen_walker
    PUBLIC c_std_17
)

target_include_directories(mangen_walker
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(Threads REQUIRED)

target_link_libraries(mangen_walker
    PUBLIC
        mangen_matcher
        mangen_queue
        mangen_catalog
        Threads::Threads
)

if(BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest must be initialized before libs/walker"
        )
    endif()

    add_executable(mangen_walker_tests
        tests/walker_test.cpp
    )

    target_compile_features(mangen_walker_tests
        PRIVATE cxx_std_17
    )

    target_link_libraries(mangen_walker_tests
        PRIVATE
            mangen_walker
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(mangen_walker_tests)
endif()
