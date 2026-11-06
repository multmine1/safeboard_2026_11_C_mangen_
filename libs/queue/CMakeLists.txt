set(CK_SOURCE_DIR
    "${PROJECT_SOURCE_DIR}/libs/third_party/ck"
)

set(CK_BUILD_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/ck"
)

if(NOT EXISTS "${CK_SOURCE_DIR}/configure")
    message(FATAL_ERROR
        "Concurrency Kit 0.7.2 not found: ${CK_SOURCE_DIR}"
    )
endif()

file(MAKE_DIRECTORY "${CK_BUILD_DIR}")

if(NOT EXISTS "${CK_BUILD_DIR}/include/ck_md.h")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -E env
            "CC=${CMAKE_C_COMPILER}"
            sh "${CK_SOURCE_DIR}/configure"

        WORKING_DIRECTORY "${CK_BUILD_DIR}"

        RESULT_VARIABLE CK_CONFIGURE_RESULT
        OUTPUT_VARIABLE CK_CONFIGURE_OUTPUT
        ERROR_VARIABLE CK_CONFIGURE_ERROR
    )

    if(NOT CK_CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Concurrency Kit configuration failed.\n"
            "${CK_CONFIGURE_OUTPUT}\n"
            "${CK_CONFIGURE_ERROR}"
        )
    endif()
endif()

add_library(mangen_queue STATIC
    src/queue.c
    src/queue_lifecycle.c
)

target_compile_features(mangen_queue
    PUBLIC c_std_17
)

target_include_directories(mangen_queue
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"

    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${CK_SOURCE_DIR}/include"
        "${CK_BUILD_DIR}/include"
)

if(BUILD_TESTING)
    if(NOT TARGET GTest::gtest_main)
        message(FATAL_ERROR
            "GoogleTest must be initialized before libs/queue"
        )
    endif()

    find_package(Threads REQUIRED)

    add_executable(mangen_queue_tests
        tests/queue_test.cpp
        tests/queue_mpmc_test.cpp
    )

    target_compile_features(mangen_queue_tests
        PRIVATE cxx_std_17
    )

    target_link_libraries(mangen_queue_tests
        PRIVATE
            mangen_queue
            GTest::gtest_main
            Threads::Threads
    )

    include(GoogleTest)

    gtest_discover_tests(mangen_queue_tests)
endif()
