add_executable(mangen_matcher_tests
    utf8_test.cpp
    pattern_test.cpp
    nfa_test.cpp
    dfa_test.cpp
    min_dfa_test.cpp
    matcher_match_test.cpp
    graphviz_test.cpp
)

target_compile_features(mangen_matcher_tests
    PRIVATE cxx_std_17
)

target_include_directories(mangen_matcher_tests
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../src"
)

target_compile_definitions(mangen_matcher_tests
    PRIVATE
        "MANGEN_GRAPHVIZ_OUTPUT_DIR=\"${PROJECT_BINARY_DIR}/graphviz\""
)

target_link_libraries(mangen_matcher_tests
    PRIVATE
        mangen_matcher
        utf8proc::utf8proc
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(mangen_matcher_tests)
