#include <gtest/gtest.h>

#include "mangen/queue.h"

#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

namespace {

struct QueueDeleter {
    void operator()(mangen_queue_t *queue) const noexcept {
        mangen_queue_destroy(queue);
    }
};

using QueuePtr = std::unique_ptr<mangen_queue_t, QueueDeleter>;

QueuePtr make_queue(
    size_t capacity,
    mangen_queue_dispose_fn dispose = nullptr,
    void *context = nullptr
) {
    mangen_queue_t *raw = nullptr;

    const auto status = mangen_queue_create(
        capacity,
        dispose,
        context,
        &raw
    );

    EXPECT_EQ(status, MANGEN_QUEUE_OK);

    return QueuePtr(raw);
}

void count_disposal(void *item, void *context) {
    (void)item;

    auto *count = static_cast<size_t *>(context);
    ++(*count);
}

TEST(QueueTest, CapacityIsRoundedToRingRequirements) {
    auto queue = make_queue(1);

    ASSERT_NE(queue, nullptr);

    EXPECT_EQ(mangen_queue_capacity(queue.get()), 3U);
}

TEST(QueueTest, RequestedCapacityIsNeverReduced) {
    const std::vector<size_t> capacities = {
        1, 2, 3, 4, 7, 8, 15, 16, 100, 1000
    };

    for (size_t requested : capacities) {
        auto queue = make_queue(requested);

        ASSERT_NE(queue, nullptr);

        EXPECT_GE(
            mangen_queue_capacity(queue.get()),
            requested
        );
    }
}

TEST(QueueTest, EmptyQueueReturnsEmpty) {
    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    void *item = reinterpret_cast<void *>(1);

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_EMPTY
    );

    EXPECT_EQ(item, nullptr);
}

TEST(QueueTest, PreservesFifoOrder) {
    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    int first = 10;
    int second = 20;
    int third = 30;

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &first),
        MANGEN_QUEUE_OK
    );

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &second),
        MANGEN_QUEUE_OK
    );

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &third),
        MANGEN_QUEUE_OK
    );

    void *item = nullptr;

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );
    EXPECT_EQ(item, &first);

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );
    EXPECT_EQ(item, &second);

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );
    EXPECT_EQ(item, &third);

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_EMPTY
    );
}

TEST(QueueTest, FullQueueRejectsNewItem) {
    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    const size_t capacity = mangen_queue_capacity(
        queue.get()
    );

    std::vector<int> items(capacity + 1);

    for (size_t i = 0; i < capacity; ++i) {
        items[i] = static_cast<int>(i);

        ASSERT_EQ(
            mangen_queue_try_push(queue.get(), &items[i]),
            MANGEN_QUEUE_OK
        );
    }

    EXPECT_EQ(
        mangen_queue_try_push(
            queue.get(),
            &items[capacity]
        ),
        MANGEN_QUEUE_FULL
    );

    for (size_t i = 0; i < capacity; ++i) {
        void *item = nullptr;

        ASSERT_EQ(
            mangen_queue_try_pop(queue.get(), &item),
            MANGEN_QUEUE_OK
        );

        EXPECT_EQ(item, &items[i]);
    }
}

TEST(QueueTest, ClosedQueueCanBeDrained) {
    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    int first = 10;
    int second = 20;

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &first),
        MANGEN_QUEUE_OK
    );

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &second),
        MANGEN_QUEUE_OK
    );

    EXPECT_EQ(
        mangen_queue_close(queue.get()),
        MANGEN_QUEUE_OK
    );

    EXPECT_EQ(
        mangen_queue_close(queue.get()),
        MANGEN_QUEUE_OK
    );

    EXPECT_EQ(
        mangen_queue_try_push(queue.get(), &first),
        MANGEN_QUEUE_CLOSED
    );

    void *item = nullptr;

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );
    EXPECT_EQ(item, &first);

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );
    EXPECT_EQ(item, &second);

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_CLOSED
    );

    EXPECT_EQ(item, nullptr);
}

TEST(QueueTest, AbortRejectsFurtherOperations) {
    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    int value = 42;

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &value),
        MANGEN_QUEUE_OK
    );

    mangen_queue_abort(queue.get());

    EXPECT_EQ(
        mangen_queue_try_push(queue.get(), &value),
        MANGEN_QUEUE_ABORTED
    );

    void *item = &value;

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_ABORTED
    );

    EXPECT_EQ(item, nullptr);

    EXPECT_EQ(
        mangen_queue_close(queue.get()),
        MANGEN_QUEUE_ABORTED
    );
}

TEST(QueueTest, DestroyDisposesOnlyRemainingItems) {
    size_t disposal_count = 0;

    auto queue = make_queue(
        3,
        count_disposal,
        &disposal_count
    );

    ASSERT_NE(queue, nullptr);

    int first = 1;
    int second = 2;
    int third = 3;

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &first),
        MANGEN_QUEUE_OK
    );

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &second),
        MANGEN_QUEUE_OK
    );

    ASSERT_EQ(
        mangen_queue_try_push(queue.get(), &third),
        MANGEN_QUEUE_OK
    );

    void *item = nullptr;

    ASSERT_EQ(
        mangen_queue_try_pop(queue.get(), &item),
        MANGEN_QUEUE_OK
    );

    EXPECT_EQ(item, &first);
    EXPECT_EQ(disposal_count, 0U);

    queue.reset();

    EXPECT_EQ(disposal_count, 2U);
}

TEST(QueueTest, InvalidArgumentsAreRejected) {
    mangen_queue_t *raw = nullptr;

    EXPECT_EQ(
        mangen_queue_create(
            0,
            nullptr,
            nullptr,
            &raw
        ),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    EXPECT_EQ(raw, nullptr);

    EXPECT_EQ(
        mangen_queue_create(
            4,
            nullptr,
            nullptr,
            nullptr
        ),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    auto queue = make_queue(4);

    ASSERT_NE(queue, nullptr);

    EXPECT_EQ(
        mangen_queue_try_push(queue.get(), nullptr),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_queue_try_push(nullptr, &raw),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_queue_try_pop(queue.get(), nullptr),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    void *item = &raw;

    EXPECT_EQ(
        mangen_queue_try_pop(nullptr, &item),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    EXPECT_EQ(item, nullptr);

    EXPECT_EQ(
        mangen_queue_close(nullptr),
        MANGEN_QUEUE_INVALID_ARGUMENT
    );

    EXPECT_EQ(mangen_queue_capacity(nullptr), 0U);

    mangen_queue_abort(nullptr);
    mangen_queue_destroy(nullptr);
}

TEST(QueueTest, ExcessiveCapacityIsRejected) {
    mangen_queue_t *raw = nullptr;

    EXPECT_EQ(
        mangen_queue_create(
            std::numeric_limits<size_t>::max(),
            nullptr,
            nullptr,
            &raw
        ),
        MANGEN_QUEUE_LIMIT_EXCEEDED
    );

    EXPECT_EQ(raw, nullptr);
}

} // namespace
