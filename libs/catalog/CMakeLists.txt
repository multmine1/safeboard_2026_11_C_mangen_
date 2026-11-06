add_library(mangen_catalog STATIC
    src/catalog.c
    src/catalog_batch.c
    src/catalog_merge.c
    src/catalog_result.c
    src/catalog_wait.c
)

target_compile_features(mangen_catalog
    PUBLIC c_std_17
)

target_include_directories(mangen_catalog
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(Threads REQUIRED)

target_link_libraries(mangen_catalog
    PUBLIC Threads::Threads
)

if(BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest must be initialized before libs/catalog"
        )
    endif()

    add_executable(mangen_catalog_tests
        tests/catalog_batch_test.cpp
        tests/catalog_merge_test.cpp
        tests/catalog_state_test.cpp
        tests/catalog_mpmc_test.cpp
    )

    target_compile_features(mangen_catalog_tests
        PRIVATE cxx_std_17
    )

    target_link_libraries(mangen_catalog_tests
        PRIVATE
            mangen_catalog
            GTest::gtest_main
            Threads::Threads
    )

    include(GoogleTest)

    gtest_discover_tests(mangen_catalog_tests)
endif()
