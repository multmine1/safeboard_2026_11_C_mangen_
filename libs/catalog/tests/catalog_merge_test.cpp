#include <gtest/gtest.h>

#include "mangen/catalog.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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

CatalogPtr make_catalog() {
    mangen_catalog_t *raw = nullptr;

    EXPECT_EQ(
        mangen_catalog_create(&raw),
        MANGEN_CATALOG_OK
    );

    return CatalogPtr(raw);
}

BatchPtr make_batch() {
    mangen_catalog_batch_t *raw = nullptr;

    EXPECT_EQ(
        mangen_catalog_batch_create(&raw),
        MANGEN_CATALOG_OK
    );

    return BatchPtr(raw);
}

void append_path(
    mangen_catalog_batch_t *batch,
    const std::string &value
) {
    char *path = static_cast<char *>(
        std::malloc(value.size() + 1)
    );

    ASSERT_NE(path, nullptr);

    std::memcpy(
        path,
        value.c_str(),
        value.size() + 1
    );

    const auto status = mangen_catalog_batch_append(
        batch,
        path
    );

    if (status != MANGEN_CATALOG_OK) {
        std::free(path);
    }

    ASSERT_EQ(status, MANGEN_CATALOG_OK);
}

void expect_paths(
    mangen_catalog_t *catalog,
    const std::vector<std::string> &expected
) {
    ASSERT_EQ(
        mangen_catalog_size(catalog),
        expected.size()
    );

    for (size_t i = 0; i < expected.size(); ++i) {
        auto *entry = mangen_catalog_entry_at(
            catalog,
            i
        );

        ASSERT_NE(entry, nullptr);

        EXPECT_STREQ(
            mangen_catalog_entry_path(entry),
            expected[i].c_str()
        );
    }
}

TEST(CatalogMergeTest, FinalizeEmptyCatalog) {
    auto catalog = make_catalog();

    ASSERT_NE(catalog, nullptr);

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            nullptr,
            0
        ),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        mangen_catalog_size(catalog.get()),
        0U
    );

    EXPECT_EQ(
        mangen_catalog_entry_at(catalog.get(), 0),
        nullptr
    );
}

TEST(CatalogMergeTest, SortSingleBatch) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "z.txt");
    append_path(batch.get(), "a.txt");
    append_path(batch.get(), "m.txt");

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

    expect_paths(
        catalog.get(),
        {"a.txt", "m.txt", "z.txt"}
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        0U
    );
}

TEST(CatalogMergeTest, MergeAndSortMultipleBatches) {
    auto catalog = make_catalog();
    auto first = make_batch();
    auto second = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    append_path(first.get(), "gamma.txt");
    append_path(first.get(), "alpha.txt");

    append_path(second.get(), "delta.txt");
    append_path(second.get(), "beta.txt");

    mangen_catalog_batch_t *batches[] = {
        first.get(),
        second.get()
    };

    ASSERT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            batches,
            2
        ),
        MANGEN_CATALOG_OK
    );

    expect_paths(
        catalog.get(),
        {
            "alpha.txt",
            "beta.txt",
            "delta.txt",
            "gamma.txt"
        }
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(first.get()),
        0U
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(second.get()),
        0U
    );
}

TEST(CatalogMergeTest, RemoveIdenticalPaths) {
    auto catalog = make_catalog();
    auto first = make_batch();
    auto second = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    append_path(first.get(), "a.txt");
    append_path(first.get(), "b.txt");
    append_path(first.get(), "b.txt");

    append_path(second.get(), "a.txt");
    append_path(second.get(), "c.txt");

    mangen_catalog_batch_t *batches[] = {
        first.get(),
        second.get()
    };

    ASSERT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            batches,
            2
        ),
        MANGEN_CATALOG_OK
    );

    expect_paths(
        catalog.get(),
        {"a.txt", "b.txt", "c.txt"}
    );
}

TEST(CatalogMergeTest, EntriesStartPending) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "file.txt");

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

    auto *entry = mangen_catalog_entry_at(
        catalog.get(),
        0
    );

    ASSERT_NE(entry, nullptr);

    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        result.state,
        MANGEN_CATALOG_PENDING
    );

    EXPECT_EQ(result.digest_size, 0U);
    EXPECT_EQ(result.error, 0);
}

TEST(CatalogMergeTest, RejectRepeatedFinalize) {
    auto catalog = make_catalog();

    ASSERT_NE(catalog, nullptr);

    ASSERT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            nullptr,
            0
        ),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            nullptr,
            0
        ),
        MANGEN_CATALOG_INVALID_STATE
    );
}

TEST(CatalogMergeTest, RejectDuplicateBatchHandle) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "file.txt");

    mangen_catalog_batch_t *invalid_batches[] = {
        batch.get(),
        batch.get()
    };

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            invalid_batches,
            2
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        1U
    );

    mangen_catalog_batch_t *valid_batches[] = {
        batch.get()
    };

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            valid_batches,
            1
        ),
        MANGEN_CATALOG_OK
    );
}

TEST(CatalogMergeTest, RejectNullBatchWithoutConsumingPaths) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "file.txt");

    mangen_catalog_batch_t *batches[] = {
        batch.get(),
        nullptr
    };

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            batches,
            2
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        1U
    );
}

TEST(CatalogMergeTest, CancellationPreservesBatches) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "file.txt");

    mangen_catalog_cancel(catalog.get());

    mangen_catalog_batch_t *batches[] = {
        batch.get()
    };

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            batches,
            1
        ),
        MANGEN_CATALOG_CANCELLED
    );

    EXPECT_EQ(
        mangen_catalog_batch_size(batch.get()),
        1U
    );
}

TEST(CatalogMergeTest, EntryAddressesRemainStable) {
    auto catalog = make_catalog();
    auto batch = make_batch();

    ASSERT_NE(catalog, nullptr);
    ASSERT_NE(batch, nullptr);

    append_path(batch.get(), "first.txt");
    append_path(batch.get(), "second.txt");

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

    auto *first = mangen_catalog_entry_at(
        catalog.get(),
        0
    );

    auto *second = mangen_catalog_entry_at(
        catalog.get(),
        1
    );

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(
        first,
        mangen_catalog_entry_at(catalog.get(), 0)
    );

    EXPECT_EQ(
        second,
        mangen_catalog_entry_at(catalog.get(), 1)
    );

    EXPECT_EQ(
        mangen_catalog_entry_at(catalog.get(), 2),
        nullptr
    );
}

TEST(CatalogMergeTest, RejectInvalidArguments) {
    auto catalog = make_catalog();

    ASSERT_NE(catalog, nullptr);

    EXPECT_EQ(
        mangen_catalog_finalize(
            nullptr,
            nullptr,
            0
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_catalog_finalize(
            catalog.get(),
            nullptr,
            1
        ),
        MANGEN_CATALOG_INVALID_ARGUMENT
    );
}

} // namespace
