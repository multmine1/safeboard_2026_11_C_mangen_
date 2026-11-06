#include <gtest/gtest.h>

#include "mangen/queue.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace {

struct QueueDeleter {
    void operator()(mangen_queue_t *queue) const noexcept {
        mangen_queue_destroy(queue);
    }
};

using QueuePtr = std::unique_ptr<mangen_queue_t, QueueDeleter>;

struct TestItem {
    size_t id;
};

TEST(QueueMpmcTest, EveryItemIsConsumedExactlyOnce) {
    constexpr size_t producer_count = 4;
    constexpr size_t consumer_count = 4;
    constexpr size_t items_per_producer = 2000;

    constexpr size_t total_items =
        producer_count * items_per_producer;

    mangen_queue_t *raw = nullptr;

    ASSERT_EQ(
        mangen_queue_create(
            31,
            nullptr,
            nullptr,
            &raw
        ),
        MANGEN_QUEUE_OK
    );

    QueuePtr queue(raw);

    ASSERT_NE(queue, nullptr);

    std::vector<TestItem> items(total_items);

    std::vector<std::atomic<unsigned int>> seen(
        total_items
    );

    for (size_t i = 0; i < total_items; ++i) {
        items[i].id = i;

        seen[i].store(
            0,
            std::memory_order_relaxed
        );
    }

    std::atomic<size_t> consumed{0};
    std::atomic<size_t> errors{0};

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(20);

    std::vector<std::thread> producers;
    std::vector<std::thread> consumers;

    producers.reserve(producer_count);
    consumers.reserve(consumer_count);

    for (size_t p = 0; p < producer_count; ++p) {
        producers.emplace_back([&, p] {
            const size_t begin = p * items_per_producer;
            const size_t end = begin + items_per_producer;

            for (size_t i = begin; i < end; ++i) {
                bool inserted = false;

                while (
                    std::chrono::steady_clock::now() <
                    deadline
                ) {
                    const auto status =
                        mangen_queue_try_push(
                            queue.get(),
                            &items[i]
                        );

                    if (status == MANGEN_QUEUE_OK) {
                        inserted = true;
                        break;
                    }

                    if (status != MANGEN_QUEUE_FULL) {
                        errors.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                        return;
                    }

                    // Только в стресс-тесте: повторяем
                    // попытку для проверки оборотов кольца.
                    std::this_thread::yield();
                }

                if (!inserted) {
                    errors.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                    return;
                }
            }
        });
    }

    for (size_t c = 0; c < consumer_count; ++c) {
        consumers.emplace_back([&] {
            while (
                consumed.load(
                    std::memory_order_acquire
                ) < total_items
            ) {
                if (
                    std::chrono::steady_clock::now() >=
                    deadline
                ) {
                    errors.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                    return;
                }

                void *raw_item = nullptr;

                const auto status =
                    mangen_queue_try_pop(
                        queue.get(),
                        &raw_item
                    );

                if (status == MANGEN_QUEUE_OK) {
                    if (raw_item == nullptr) {
                        errors.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                        continue;
                    }

                    const auto *item =
                        static_cast<const TestItem *>(
                            raw_item
                        );

                    if (item->id >= total_items) {
                        errors.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                        continue;
                    }

                    if (
                        seen[item->id].fetch_add(
                            1,
                            std::memory_order_relaxed
                        ) != 0
                    ) {
                        errors.fetch_add(
                            1,
                            std::memory_order_relaxed
                        );
                    }

                    consumed.fetch_add(
                        1,
                        std::memory_order_release
                    );

                    continue;
                }

                if (
                    status == MANGEN_QUEUE_EMPTY ||
                    status == MANGEN_QUEUE_CLOSED
                ) {
                    std::this_thread::yield();
                    continue;
                }

                errors.fetch_add(
                    1,
                    std::memory_order_relaxed
                );

                return;
            }
        });
    }

    for (auto &thread : producers) {
        thread.join();
    }

    EXPECT_EQ(
        mangen_queue_close(queue.get()),
        MANGEN_QUEUE_OK
    );

    for (auto &thread : consumers) {
        thread.join();
    }

    EXPECT_EQ(errors.load(), 0U);
    EXPECT_EQ(consumed.load(), total_items);

    for (size_t i = 0; i < total_items; ++i) {
        EXPECT_EQ(seen[i].load(), 1U)
            << "Item ID: " << i;
    }

    void *item = nullptr;

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_CLOSED
    );
}

TEST(QueueMpmcTest, ConcurrentAbortStopsConsumers) {
    constexpr size_t consumer_count = 4;

    mangen_queue_t *raw = nullptr;

    ASSERT_EQ(
        mangen_queue_create(
            16,
            nullptr,
            nullptr,
            &raw
        ),
        MANGEN_QUEUE_OK
    );

    QueuePtr queue(raw);

    ASSERT_NE(queue, nullptr);

    std::atomic<size_t> started{0};
    std::atomic<size_t> aborted{0};
    std::atomic<size_t> errors{0};

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::seconds(10);

    std::vector<std::thread> consumers;

    consumers.reserve(consumer_count);

    for (size_t i = 0; i < consumer_count; ++i) {
        consumers.emplace_back([&] {
            started.fetch_add(
                1,
                std::memory_order_release
            );

            while (
                std::chrono::steady_clock::now() <
                deadline
            ) {
                void *item = nullptr;

                const auto status =
                    mangen_queue_try_pop(
                        queue.get(),
                        &item
                    );

                if (status == MANGEN_QUEUE_ABORTED) {
                    aborted.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                    return;
                }

                if (status != MANGEN_QUEUE_EMPTY) {
                    errors.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                    return;
                }

                std::this_thread::yield();
            }

            errors.fetch_add(
                1,
                std::memory_order_relaxed
            );
        });
    }

    while (
        started.load(std::memory_order_acquire) <
        consumer_count
    ) {
        std::this_thread::yield();
    }

    mangen_queue_abort(queue.get());

    for (auto &thread : consumers) {
        thread.join();
    }

    EXPECT_EQ(errors.load(), 0U);
    EXPECT_EQ(aborted.load(), consumer_count);
}

} // namespace
