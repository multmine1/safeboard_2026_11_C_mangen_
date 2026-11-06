#include <gtest/gtest.h>

#include "mangen/catalog.h"
#include "mangen/hash.h"
#include "mangen/hash_pool.h"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace {

using PoolHandle = std::unique_ptr<
    mangen_hash_pool_t,
    decltype(&mangen_hash_pool_destroy)
>;

class TempCatalog {
public:
    TempCatalog() {
        char pattern[] = "/tmp/mangen-pool-XXXXXX";

        char *directory = mkdtemp(pattern);

        if (directory == nullptr) {
            return;
        }

        directory_ = directory;

        if (mangen_catalog_create(&catalog_) != MANGEN_CATALOG_OK) {
            return;
        }

        mangen_catalog_batch_create(&batch_);
    }

    ~TempCatalog() {
        mangen_catalog_batch_destroy(batch_);
        mangen_catalog_destroy(catalog_);

        for (const auto& path : paths_) {
            unlink(path.c_str());
        }

        if (!directory_.empty()) {
            rmdir(directory_.c_str());
        }
    }

    TempCatalog(const TempCatalog&) = delete;
    TempCatalog& operator=(const TempCatalog&) = delete;

    bool valid() const {
        return !directory_.empty() &&
               catalog_ != nullptr &&
               batch_ != nullptr;
    }

    mangen_catalog_t *catalog() const {
        return catalog_;
    }

    const std::string& path(size_t index) const {
        return paths_.at(index);
    }

    bool add_files(size_t count) {
        if (!valid()) {
            return false;
        }

        for (size_t i = 0; i < count; ++i) {
            char name[32];

            std::snprintf(
                name,
                sizeof(name),
                "file-%06zu.dat",
                i
            );

            const std::string path =
                directory_ + "/" + name;

            paths_.push_back(path);

            FILE *file = std::fopen(path.c_str(), "wb");

            if (file == nullptr) {
                return false;
            }

            const std::string content =
                "mangen hash pool test " + std::to_string(i);

            const size_t written = std::fwrite(
                content.data(),
                1u,
                content.size(),
                file
            );

            const int close_result = std::fclose(file);

            if (written != content.size() || close_result != 0) {
                return false;
            }

            char *owned_path = static_cast<char *>(
                std::malloc(path.size() + 1u)
            );

            if (owned_path == nullptr) {
                return false;
            }

            std::memcpy(
                owned_path,
                path.c_str(),
                path.size() + 1u
            );

            const mangen_catalog_status_t status =
                mangen_catalog_batch_append(
                    batch_,
                    owned_path
                );

            if (status != MANGEN_CATALOG_OK) {
                std::free(owned_path);
                return false;
            }
        }

        return mangen_catalog_finalize(
            catalog_,
            &batch_,
            1u
        ) == MANGEN_CATALOG_OK;
    }

private:
    std::string directory_;
    std::vector<std::string> paths_;

    mangen_catalog_t *catalog_ = nullptr;
    mangen_catalog_batch_t *batch_ = nullptr;
};

PoolHandle create_pool(
    mangen_catalog_t *catalog,
    mangen_hash_algorithm_t algorithm,
    size_t workers,
    size_t capacity
) {
    mangen_hash_pool_t *raw = nullptr;

    const mangen_hash_pool_status_t status =
        mangen_hash_pool_create(
            catalog,
            algorithm,
            workers,
            capacity,
            &raw
        );

    if (status != MANGEN_HASH_POOL_OK) {
        ADD_FAILURE() << "pool creation failed: " << status;
    }

    return PoolHandle(raw, &mangen_hash_pool_destroy);
}

} // namespace

TEST(HashPoolTest, ProcessesAllFiles) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(64u));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        4u,
        128u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    for (size_t i = 0; i < 64u; ++i) {
        ASSERT_EQ(
            mangen_hash_pool_submit(pool.get(), i),
            MANGEN_HASH_POOL_OK
        );
    }

    ASSERT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(mangen_hash_pool_completed(pool.get()), 64u);
    EXPECT_EQ(mangen_hash_pool_failed(pool.get()), 0u);

    for (size_t i = 0; i < 64u; ++i) {
        auto *entry = mangen_catalog_entry_at(
            files.catalog(),
            i
        );

        ASSERT_NE(entry, nullptr);

        mangen_catalog_result_t result{};

        ASSERT_EQ(
            mangen_catalog_get_result(entry, &result),
            MANGEN_CATALOG_OK
        );

        EXPECT_EQ(result.state, MANGEN_CATALOG_SUCCESS);
        EXPECT_EQ(result.digest_size, 32u);

        mangen_hash_digest_t reference{};
        int detail = 0;

        ASSERT_EQ(
            mangen_hash_file(
                mangen_catalog_entry_path(entry),
                MANGEN_HASH_SHA256,
                &reference,
                &detail
            ),
            MANGEN_HASH_OK
        );

        EXPECT_EQ(
            std::memcmp(
                result.digest,
                reference.bytes,
                reference.size
            ),
            0
        );
    }
}

TEST(HashPoolTest, RejectsDuplicateSubmission) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(1u));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        2u,
        8u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_submit(pool.get(), 0u),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_submit(pool.get(), 0u),
        MANGEN_HASH_POOL_INVALID_STATE
    );

    EXPECT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(mangen_hash_pool_completed(pool.get()), 1u);
}

TEST(HashPoolTest, ConcurrentProducersSubmitUniqueIndices) {
    constexpr size_t file_count = 64u;
    constexpr size_t producer_count = 4u;

    TempCatalog files;

    ASSERT_TRUE(files.add_files(file_count));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA512,
        4u,
        128u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    std::atomic<bool> all_submitted{true};
    std::vector<std::thread> producers;

    for (size_t producer = 0; producer < producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            for (size_t i = producer;
                 i < file_count;
                 i += producer_count) {
                if (mangen_hash_pool_submit(
                        pool.get(),
                        i
                    ) != MANGEN_HASH_POOL_OK) {
                    all_submitted.store(false);
                }
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    EXPECT_TRUE(all_submitted.load());

    ASSERT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_completed(pool.get()),
        file_count
    );

    EXPECT_EQ(mangen_hash_pool_failed(pool.get()), 0u);

    for (size_t i = 0; i < file_count; ++i) {
        auto *entry = mangen_catalog_entry_at(
            files.catalog(),
            i
        );

        mangen_catalog_result_t result{};

        ASSERT_EQ(
            mangen_catalog_get_result(entry, &result),
            MANGEN_CATALOG_OK
        );

        EXPECT_EQ(result.state, MANGEN_CATALOG_SUCCESS);
        EXPECT_EQ(result.digest_size, 64u);
    }
}

TEST(HashPoolTest, MissingFileDoesNotStopOtherWorkers) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(4u));

    ASSERT_EQ(unlink(files.path(1u).c_str()), 0);

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        2u,
        8u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    for (size_t i = 0; i < 4u; ++i) {
        ASSERT_EQ(
            mangen_hash_pool_submit(pool.get(), i),
            MANGEN_HASH_POOL_OK
        );
    }

    ASSERT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(mangen_hash_pool_completed(pool.get()), 4u);
    EXPECT_EQ(mangen_hash_pool_failed(pool.get()), 1u);

    for (size_t i = 0; i < 4u; ++i) {
        auto *entry = mangen_catalog_entry_at(
            files.catalog(),
            i
        );

        mangen_catalog_result_t result{};

        ASSERT_EQ(
            mangen_catalog_get_result(entry, &result),
            MANGEN_CATALOG_OK
        );

        if (i == 1u) {
            EXPECT_EQ(result.state, MANGEN_CATALOG_ERROR);
            EXPECT_EQ(result.error, ENOENT);
        } else {
            EXPECT_EQ(result.state, MANGEN_CATALOG_SUCCESS);
        }
    }
}

TEST(HashPoolTest, CancelWakesIdleWorkers) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(1u));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        4u,
        8u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    mangen_hash_pool_cancel(pool.get());

    EXPECT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_CANCELLED
    );
}

TEST(HashPoolTest, CloseRejectsNewSubmissions) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(1u));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        2u,
        8u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_close(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_close(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_submit(pool.get(), 0u),
        MANGEN_HASH_POOL_INVALID_STATE
    );

    EXPECT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );
}

TEST(HashPoolTest, RejectsInvalidCatalogIndex) {
    TempCatalog files;

    ASSERT_TRUE(files.add_files(1u));

    auto pool = create_pool(
        files.catalog(),
        MANGEN_HASH_SHA256,
        1u,
        8u
    );

    ASSERT_NE(pool, nullptr);

    ASSERT_EQ(
        mangen_hash_pool_start(pool.get()),
        MANGEN_HASH_POOL_OK
    );

    EXPECT_EQ(
        mangen_hash_pool_submit(pool.get(), 100u),
        MANGEN_HASH_POOL_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_hash_pool_join(pool.get()),
        MANGEN_HASH_POOL_OK
    );
}
