add_executable(mangen
    src/main.c
    src/cli.c
    src/runner.c
)

target_link_libraries(mangen
    PRIVATE
        mangen_matcher
        mangen_catalog
        mangen_hash
        mangen_hash_pool
        mangen_walker
        mangen_manifest
)

target_compile_features(mangen
    PRIVATE
        c_std_17
)

install(TARGETS mangen
    RUNTIME DESTINATION bin
)
