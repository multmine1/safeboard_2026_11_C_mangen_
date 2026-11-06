#include <gtest/gtest.h>

#include "mangen/hash.h"

#include <array>
#include <cstdint>
#include <cstring>

TEST(HashFormatTest, AlgorithmDigestSizes) {
    EXPECT_EQ(mangen_hash_digest_size(MANGEN_HASH_SHA256), 32u);
    EXPECT_EQ(mangen_hash_digest_size(MANGEN_HASH_SHA384), 48u);
    EXPECT_EQ(mangen_hash_digest_size(MANGEN_HASH_SHA512), 64u);
}

TEST(HashFormatTest, InvalidAlgorithmHasZeroSize) {
    EXPECT_EQ(
        mangen_hash_digest_size(
            static_cast<mangen_hash_algorithm_t>(99)
        ),
        0u
    );
}

TEST(HashFormatTest, UppercaseHexPreservesLeadingZeroes) {
    mangen_hash_digest_t digest{};

    digest.size = 32u;
    digest.bytes[0] = 0x00;
    digest.bytes[1] = 0x01;
    digest.bytes[2] = 0x0A;
    digest.bytes[3] = 0xFF;

    std::array<char, MANGEN_HASH_MAX_HEX_SIZE> output{};

    ASSERT_EQ(
        mangen_hash_to_hex(
            &digest,
            output.data(),
            output.size()
        ),
        MANGEN_HASH_OK
    );

    EXPECT_EQ(std::strlen(output.data()), 64u);
    EXPECT_EQ(std::strncmp(output.data(), "00010AFF", 8), 0);
}

TEST(HashFormatTest, SupportsAllThreeHexLengths) {
    for (const size_t length : {32u, 48u, 64u}) {
        mangen_hash_digest_t digest{};
        digest.size = length;

        std::array<char, MANGEN_HASH_MAX_HEX_SIZE> output{};

        ASSERT_EQ(
            mangen_hash_to_hex(
                &digest,
                output.data(),
                output.size()
            ),
            MANGEN_HASH_OK
        );

        EXPECT_EQ(std::strlen(output.data()), length * 2u);
    }
}

TEST(HashFormatTest, RejectsInsufficientOutputCapacity) {
    mangen_hash_digest_t digest{};
    digest.size = 64u;

    std::array<char, 128u> output{};

    EXPECT_EQ(
        mangen_hash_to_hex(
            &digest,
            output.data(),
            output.size()
        ),
        MANGEN_HASH_BUFFER_TOO_SMALL
    );
}

TEST(HashFormatTest, RejectsInvalidDigestLength) {
    mangen_hash_digest_t digest{};
    digest.size = 65u;

    std::array<char, MANGEN_HASH_MAX_HEX_SIZE> output{};

    EXPECT_EQ(
        mangen_hash_to_hex(
            &digest,
            output.data(),
            output.size()
        ),
        MANGEN_HASH_INVALID_ARGUMENT
    );
}

TEST(HashFormatTest, RejectsNullArguments) {
    mangen_hash_digest_t digest{};
    digest.size = 32u;

    char output[MANGEN_HASH_MAX_HEX_SIZE]{};

    EXPECT_EQ(
        mangen_hash_to_hex(nullptr, output, sizeof(output)),
        MANGEN_HASH_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_hash_to_hex(&digest, nullptr, sizeof(output)),
        MANGEN_HASH_INVALID_ARGUMENT
    );
}
