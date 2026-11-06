if(NOT TARGET mbedcrypto)
    set(ENABLE_TESTING OFF CACHE BOOL "Build Mbed TLS tests" FORCE)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "Build Mbed TLS programs" FORCE)
    set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "Build static Mbed TLS" FORCE)
    set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "Build shared Mbed TLS" FORCE)

    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mbedtls"
        "${CMAKE_BINARY_DIR}/third_party/mbedtls"
        EXCLUDE_FROM_ALL
    )
endif()

add_library(mangen_hash STATIC
    src/hash.c
    src/hash_file.c
)

target_compile_features(mangen_hash
    PUBLIC c_std_17
)

target_include_directories(mangen_hash
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/mbedtls/include"
)

target_link_libraries(mangen_hash
    PRIVATE mbedcrypto
)

if(BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest must be initialized before libs/hash"
        )
    endif()

    add_executable(mangen_hash_tests
        tests/hash_format_test.cpp
        tests/hash_file_test.cpp
    )

    target_compile_features(mangen_hash_tests
        PRIVATE cxx_std_17
    )

    target_link_libraries(mangen_hash_tests
        PRIVATE
            mangen_hash
            GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(mangen_hash_tests)
endif()
