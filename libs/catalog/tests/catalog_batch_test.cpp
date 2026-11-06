#include <gtest/gtest.h>

#include "mangen/catalog.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace {

struct BatchDeleter {
    void operator()(mangen_catalog_batch_t *batch) const noexcept {
        mangen_catalog_batch_destroy(batch);
    }
};

using BatchPtr = std::unique_ptr<
    mangen_catalog_batch_t,
    BatchDeleter
>;

BatchPtr make_batch() {
    mangen_catalog_batch_t *raw = nullptr;

    EXPECT_EQ(
        mangen_catalog_batch_create(&raw),
        MANGEN_CATALOG_OK
    );

    return BatchPtr(raw);
}

char *copy_path(const std::string &value) {
    char *path = static_cast<char *>(
        std::malloc(value.size() + 1)
    );

    if (path != nullptr) {
        std::memcpy(
            path,
            value.c_str(),
            value.size() + 1
        );
    }

    return path;
}

void append_path(
    mangen_catalog_batch_t *batch,
    const std::string &value
) {
    char *path = copy_path(value);

    ASSERT_NE(path, nullptr);

    const auto status = mangen_catalog_batch_append(
        batch,
        path
    );

    if (status != MANGEN_CATALOG_OK) {
        std::free(path);
    }

    ASSERT_EQ(status, MANGEN_CATALOG_OK);
}

TEST(CatalogBatchTest, CreateEmptyBatch) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        0U
    );
}

TEST(CatalogBatchTest, AppendSinglePath) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "documents/report.txt");

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        1U
    );
}

TEST(CatalogBatchTest, AppendMultiplePaths) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "a.txt");
    append_path(batch.get(), "b.txt");
    append_path(batch.get(), "c.txt");

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        3U
    );
}

TEST(CatalogBatchTest, GrowDynamicArray) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    constexpr size_t item_count = 1024;

    for (size_t i = 0; i < item_count; ++i) {
        append_path(
            batch.get(),
            "directory/file_" + std::to_string(i)
        );
    }

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        item_count
    );
}

TEST(CatalogBatchTest, IndependentBatches) {
    auto first = make_batch();
    auto second = make_batch();

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    for (size_t i = 0; i < 100; ++i) {
        append_path(
            first.get(),
            "first/" + std::to_string(i)
        );
    }

    for (size_t i = 0; i < 25; ++i) {
        append_path(
            second.get(),
            "second/" + std::to_string(i)
        );
    }

    EXPECT_EQ(
        mangen_catalog_batch_size(first.get()),
        100U
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(second.get()),
        25U
    );
}

TEST(CatalogBatchTest, RejectNullOutput) {
    EXPECT_EQ(
        mangen_catalog_batch_create(nullptr),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );
}

TEST(CatalogBatchTest, RejectNullBatch) {
    char *path = copy_path("example.txt");

    ASSERT_NE(path, nullptr);

    EXPECT_EQ(
        mangen_catalog_batch_append(nullptr, path),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    std::free(path);
}

TEST(CatalogBatchTest, RejectNullPath) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    EXPECT_EQ(
        mangen_catalog_batch_append(
            batch.get(),
            nullptr
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        0U
    );
}

TEST(CatalogBatchTest, RejectEmptyPathWithoutTakingOwnership) {
    auto batch = make_batch();

    ASSERT_NE(batch, nullptr);

    char *path = copy_path("");

    ASSERT_NE(path, nullptr);

    EXPECT_EQ(
        mangen_catalog_batch_append(
            batch.get(),
            path
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        0U
    );

    std::free(path);
}

TEST(CatalogBatchTest, NullOperationsAreSafe) {
    EXPECT_EQ(
        mangen_catalog_batch_size(nullptr),
        0U
    );

    mangen_catalog_batch_destroy(nullptr);
}

} // namespace
