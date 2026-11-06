add_library(mangen_hash_pool STATIC
    src/hash_pool.c
    src/hash_pool_worker.c
)

target_compile_features(mangen_hash_pool
    PUBLIC c_std_17
)

target_include_directories(mangen_hash_pool
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

find_package(Threads REQUIRED)

target_link_libraries(mangen_hash_pool
    PUBLIC
        mangen_queue
        mangen_catalog
        mangen_hash
        Threads::Threads
)

if(BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest must be initialized before libs/hash_pool"
        )
    endif()

    add_executable(mangen_hash_pool_tests
        tests/hash_pool_test.cpp
    )

    target_compile_features(mangen_hash_pool_tests
        PRIVATE cxx_std_17
    )

    target_link_libraries(mangen_hash_pool_tests
        PRIVATE
            mangen_hash_pool
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(mangen_hash_pool_tests)
endif()
