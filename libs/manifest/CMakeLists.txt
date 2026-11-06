add_library(mangen_manifest STATIC
    src/manifest.c
    src/manifest_path.c
    src/manifest_hex.c
)

target_include_directories(mangen_manifest
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mangen_manifest
    PUBLIC
        mangen_catalog
)

target_compile_features(mangen_manifest
    PUBLIC
        c_std_17
)

if(BUILD_TESTING)
    include(GoogleTest)

    add_executable(mangen_manifest_test
        tests/manifest_test.cpp
    )

    target_link_libraries(mangen_manifest_test
        PRIVATE
            mangen_manifest
            GTest::gtest_main
    )

    target_compile_features(mangen_manifest_test
        PRIVATE
            cxx_std_17
    )

    gtest_discover_tests(mangen_manifest_test)
endif()
