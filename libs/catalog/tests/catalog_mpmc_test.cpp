#include <gtest/gtest.h>

#include "mangen/catalog.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
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

class CatalogMpmcTest : public ::testing::Test {
protected:
    CatalogPtr catalog;
    BatchPtr batch;

    void initialize(size_t count) {
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

        for (size_t i = 0; i < count; ++i) {
            const std::string name =
                "file_" + std::to_string(i);

            char *path = static_cast<char *>(
                std::malloc(name.size() + 1)
            );

            ASSERT_NE(path, nullptr);

            std::memcpy(
                path,
                name.c_str(),
                name.size() + 1
            );

            const auto status =
                mangen_catalog_batch_append(
                    batch.get(),
                    path
                );

            if (status != MANGEN_CATALOG_OK) {
                std::free(path);
            }

            ASSERT_EQ(status, MANGEN_CATALOG_OK);
        }

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

        ASSERT_EQ(
            mangen_catalog_size(catalog.get()),
            count
        );
    }
};

TEST_F(CatalogMpmcTest, MultipleWaitersReceiveResult) {
    initialize(1);

    ASSERT_NE(catalog, nullptr);
    ASSERT_EQ(mangen_catalog_size(catalog.get()), 1U);

    auto *entry = mangen_catalog_entry_at(
        catalog.get(),
        0
    );

    ASSERT_NE(entry, nullptr);

    constexpr size_t waiter_count = 8;

    std::atomic<size_t> started{0};
    std::atomic<size_t> successful{0};
    std::atomic<size_t> errors{0};

    std::vector<std::thread> waiters;

    for (size_t i = 0; i < waiter_count; ++i) {
        waiters.emplace_back([&] {
            started.fetch_add(1);

            mangen_catalog_result_t result{};

            const auto status =
                mangen_catalog_wait_result(
                    entry,
                    &result
                );

            if (
                status == MANGEN_CATALOG_OK &&
                result.state == MANGEN_CATALOG_SUCCESS &&
                result.digest_size == 32
            ) {
                successful.fetch_add(1);
            } else {
                errors.fetch_add(1);
            }
        });
    }

    while (started.load() < waiter_count) {
        std::this_thread::yield();
    }

    std::array<uint8_t, 32> digest{};

    digest[0] = 42;

    const auto mark_status =
        mangen_catalog_mark_processing(entry);

    mangen_catalog_status_t publish_status =
        MANGEN_CATALOG_INVALID_STATE;

    if (mark_status == MANGEN_CATALOG_OK) {
        publish_status = mangen_catalog_publish_success(
            entry,
            digest.data(),
            digest.size()
        );
    }

    if (
        mark_status != MANGEN_CATALOG_OK ||
        publish_status != MANGEN_CATALOG_OK
    ) {
        mangen_catalog_cancel(catalog.get());
    }

    for (auto &thread : waiters) {
        thread.join();
    }

    EXPECT_EQ(mark_status, MANGEN_CATALOG_OK);
    EXPECT_EQ(publish_status, MANGEN_CATALOG_OK);

    EXPECT_EQ(errors.load(), 0U);
    EXPECT_EQ(successful.load(), waiter_count);
}

TEST_F(CatalogMpmcTest, CancellationWakesAllWaiters) {
    initialize(1);

    ASSERT_NE(catalog, nullptr);

    auto *entry = mangen_catalog_entry_at(
        catalog.get(),
        0
    );

    ASSERT_NE(entry, nullptr);

    constexpr size_t waiter_count = 8;

    std::atomic<size_t> started{0};
    std::atomic<size_t> cancelled{0};

    std::vector<std::thread> waiters;

    for (size_t i = 0; i < waiter_count; ++i) {
        waiters.emplace_back([&] {
            started.fetch_add(1);

            mangen_catalog_result_t result{};

            if (
                mangen_catalog_wait_result(
                    entry,
                    &result
                ) == MANGEN_CATALOG_CANCELLED
            ) {
                cancelled.fetch_add(1);
            }
        });
    }

    while (started.load() < waiter_count) {
        std::this_thread::yield();
    }

    mangen_catalog_cancel(catalog.get());

    for (auto &thread : waiters) {
        thread.join();
    }

    EXPECT_EQ(cancelled.load(), waiter_count);
}

TEST_F(CatalogMpmcTest, ConcurrentPublicationSucceedsOnce) {
    initialize(1);

    ASSERT_NE(catalog, nullptr);

    auto *entry = mangen_catalog_entry_at(
        catalog.get(),
        0
    );

    ASSERT_NE(entry, nullptr);

    constexpr size_t worker_count = 8;

    std::atomic<size_t> claimed{0};
    std::atomic<size_t> published{0};
    std::atomic<size_t> errors{0};

    std::vector<std::thread> workers;

    for (size_t i = 0; i < worker_count; ++i) {
        workers.emplace_back([&, i] {
            const auto status =
                mangen_catalog_mark_processing(entry);

            if (status == MANGEN_CATALOG_OK) {
                claimed.fetch_add(1);

                std::array<uint8_t, 32> digest{};

                digest[0] = static_cast<uint8_t>(i);

                if (
                    mangen_catalog_publish_success(
                        entry,
                        digest.data(),
                        digest.size()
                    ) == MANGEN_CATALOG_OK
                ) {
                    published.fetch_add(1);
                } else {
                    errors.fetch_add(1);
                }
            } else if (
                status != MANGEN_CATALOG_INVALID_STATE
            ) {
                errors.fetch_add(1);
            }
        });
    }

    for (auto &thread : workers) {
        thread.join();
    }

    EXPECT_EQ(claimed.load(), 1U);
    EXPECT_EQ(published.load(), 1U);
    EXPECT_EQ(errors.load(), 0U);

    mangen_catalog_result_t result{};

    ASSERT_EQ(
        mangen_catalog_get_result(entry, &result),
        MANGEN_CATALOG_OK
    );

    EXPECT_EQ(result.state, MANGEN_CATALOG_SUCCESS);
}

TEST_F(CatalogMpmcTest, ParallelHashingPreservesCatalogOrder) {
    constexpr size_t item_count = 512;
    constexpr size_t worker_count = 4;

    initialize(item_count);

    ASSERT_NE(catalog, nullptr);

    std::vector<mangen_catalog_entry_t *> entries;

    entries.reserve(item_count);

    for (size_t i = 0; i < item_count; ++i) {
        auto *entry = mangen_catalog_entry_at(
            catalog.get(),
            i
        );

        ASSERT_NE(entry, nullptr);

        entries.push_back(entry);
    }

    std::atomic<size_t> next{0};
    std::atomic<size_t> consumed{0};
    std::atomic<size_t> errors{0};

    std::atomic<bool> finished{false};
    std::atomic<bool> timed_out{false};

    std::thread watchdog([&] {
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(15);

        while (
            !finished.load() &&
            std::chrono::steady_clock::now() < deadline
        ) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5)
            );
        }

        if (!finished.load()) {
            timed_out.store(true);
            mangen_catalog_cancel(catalog.get());
        }
    });

    std::thread reader([&] {
        for (size_t i = 0; i < item_count; ++i) {
            mangen_catalog_result_t result{};

            const auto status =
                mangen_catalog_wait_result(
                    entries[i],
                    &result
                );

            if (status != MANGEN_CATALOG_OK) {
                errors.fetch_add(1);
                return;
            }

            const uint8_t expected_low =
                static_cast<uint8_t>(i & 0xFF);

            const uint8_t expected_high =
                static_cast<uint8_t>((i >> 8) & 0xFF);

            if (
                result.state != MANGEN_CATALOG_SUCCESS ||
                result.digest_size != 32 ||
                result.digest[0] != expected_low ||
                result.digest[1] != expected_high
            ) {
                errors.fetch_add(1);
            }

            consumed.fetch_add(1);
        }
    });

    std::vector<std::thread> workers;

    for (size_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back([&] {
            for (;;) {
                const size_t ticket = next.fetch_add(1);

                if (ticket >= item_count) {
                    return;
                }

                const size_t index =
                    item_count - 1 - ticket;

                auto *entry = entries[index];

                if (
                    mangen_catalog_mark_processing(entry) !=
                    MANGEN_CATALOG_OK
                ) {
                    errors.fetch_add(1);
                    return;
                }

                std::array<uint8_t, 32> digest{};

                digest[0] = static_cast<uint8_t>(
                    index & 0xFF
                );

                digest[1] = static_cast<uint8_t>(
                    (index >> 8) & 0xFF
                );

                if (
                    mangen_catalog_publish_success(
                        entry,
                        digest.data(),
                        digest.size()
                    ) != MANGEN_CATALOG_OK
                ) {
                    errors.fetch_add(1);
                    return;
                }
            }
        });
    }

    for (auto &thread : workers) {
        thread.join();
    }

    reader.join();

    finished.store(true);
    watchdog.join();

    EXPECT_FALSE(timed_out.load());
    EXPECT_EQ(errors.load(), 0U);
    EXPECT_EQ(consumed.load(), item_count);
}

} // namespace
