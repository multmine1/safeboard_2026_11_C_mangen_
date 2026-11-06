#include <gtest/gtest.h>

#include "mangen/catalog.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace {

struct CatalogDeleter {
    void operator()(mangen_catalog_t *catalog) const noexcept {
        mangen_catalog_destroy(catalog);
    }
};

struct BatchDeleter {
    void operator()(mangen_catalog_batch_t *batch) const noexcept {
        mangen_catalog_batch_destroy(batch);
    }
};

using CatalogPtr = std::unique_ptr<
    mangen_catalog_t,
    CatalogDeleter
>;

using BatchPtr = std::unique_ptr<
    mangen_catalog_batch_t,
    BatchDeleter
>;

class CatalogStateTest : public ::testing::Test {
protected:
    CatalogPtr catalog;
    BatchPtr batch;
    mangen_catalog_entry_t *entry = nullptr;

    void SetUp() override {
        mangen_catalog_t *raw_catalog = nullptr;

        ASSERT_EQ(
            mangen_catalog_create(&raw_catalog),
            MANGEN_CATALOG_OK
        );

        catalog.reset(raw_catalog);

        mangen_catalog_batch_t *raw_batch = nullptr;

        ASSERT_EQ(
            mangen_catalog_batch_create(&raw_batch),
            MANGEN_CATALOG_OK
        );

        batch.reset(raw_batch);

        const char name[] = "file.txt";

        char *path = static_cast<char *>(
            std::malloc(sizeof(name))
        );

        ASSERT_NE(path, nullptr);

        std::memcpy(path, name, sizeof(name));

        const auto append_status =
            mangen_catalog_batch_append(
                batch.get(),
                path
            );

        if (append_status != MANGEN_CATALOG_OK) {
            std::free(path);
        }

        ASSERT_EQ(
            append_status,
            MANGEN_CATALOG_OK
        );

        mangen_catalog_batch_t *batches[] = {
            batch.get()
        };

        ASSERT_EQ(
            mangen_catalog_finalize(
                catalog.get(),
                batches,
                1
            ),
            MANGEN_CATALOG_OK
        );

        entry = mangen_catalog_entry_at(
            catalog.get(),
            0
        );

        ASSERT_NE(entry, nullptr);
    }
};

TEST_F(CatalogStateTest, InitialStateIsPending) {
    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(result.state, MANGEN_CATALOG_PENDING);
    EXPECT_EQ(result.digest_size, 0U);
    EXPECT_EQ(result.error, 0);
}

TEST_F(CatalogStateTest, PublishSuccessfulDigest) {
    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    std::array<uint8_t, 32> digest{};

    for (size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<uint8_t>(i);
    }

    ASSERT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest.data(),
            digest.size()
        ),
        MANGEN_CATALOG_OK
    );

    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(result.state, MANGEN_CATALOG_SUCCESS);
    EXPECT_EQ(result.digest_size, digest.size());
    EXPECT_EQ(result.error, 0);

    EXPECT_EQ(
        std::memcmp(
            result.digest,
            digest.data(),
            digest.size()
        ),
        0
    );
}

TEST_F(CatalogStateTest, PublishError) {
    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    constexpr int error_code = 42;

    ASSERT_EQ(
        mangen_catalog_publish_error(
            entry,
            error_code
        ),
        MANGEN_CATALOG_OK
    );

    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(result.state, MANGEN_CATALOG_ERROR);
    EXPECT_EQ(result.digest_size, 0U);
    EXPECT_EQ(result.error, error_code);
}

TEST_F(CatalogStateTest, RejectInvalidTransitions) {
    const uint8_t digest[] = {1, 2, 3, 4};

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest,
            sizeof(digest)
        ),
        MANGEN_CATALOG_INVALID_STATE
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(entry, 42),
        MANGEN_CATALOG_INVALID_STATE
    );

    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_INVALID_STATE
    );

    ASSERT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest,
            sizeof(digest)
        ),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_INVALID_STATE
    );

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest,
            sizeof(digest)
        ),
        MANGEN_CATALOG_INVALID_STATE
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(entry, 42),
        MANGEN_CATALOG_INVALID_STATE
    );
}

TEST_F(CatalogStateTest, ValidateDigestSize) {
    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    std::array<
        uint8_t,
        MANGEN_CATALOG_DIGEST_CAPACITY
    > digest{};

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            nullptr,
            digest.size()
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest.data(),
            0
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest.data(),
            digest.size() + 1
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest.data(),
            digest.size()
        ),
        MANGEN_CATALOG_OK
    );
}

TEST_F(CatalogStateTest, RejectZeroErrorCode) {
    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(entry, 0),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(entry, 42),
        MANGEN_CATALOG_OK
    );
}

TEST_F(CatalogStateTest, CancellationStopsProcessing) {
    ASSERT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_OK
    );

    mangen_catalog_cancel(catalog.get());

    const uint8_t digest[] = {1, 2, 3, 4};

    EXPECT_EQ(
        mangen_catalog_publish_success(
            entry,
            digest,
            sizeof(digest)
        ),
        MANGEN_CATALOG_CANCELLED
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(entry, 42),
        MANGEN_CATALOG_CANCELLED
    );

    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        result.state,
        MANGEN_CATALOG_PROCESSING
    );
}

TEST_F(CatalogStateTest, CancellationPreventsNewProcessing) {
    mangen_catalog_cancel(catalog.get());

    EXPECT_EQ(
        mangen_catalog_mark_processing(entry),
        MANGEN_CATALOG_CANCELLED
    );
}

TEST_F(CatalogStateTest, RejectInvalidArguments) {
    mangen_catalog_result_t result{};

    EXPECT_EQ(
        mangen_catalog_mark_processing(nullptr),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_success(
            nullptr,
            result.digest,
            32
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_publish_error(nullptr, 42),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_get_result(nullptr, &result),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_get_result(entry, nullptr),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_wait_result(nullptr, &result),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_wait_result(entry, nullptr),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );
}

} // namespace
