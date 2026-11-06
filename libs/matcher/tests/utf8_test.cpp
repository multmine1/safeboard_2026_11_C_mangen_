#include "utf8.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::vector<std::string> split_graphemes(const std::string& input)
{
    matcher_utf8_cursor_t cursor{};

    matcher_utf8_cursor_init(
        &cursor,
        reinterpret_cast<const unsigned char*>(input.data()),
        input.size()
    );

    std::vector<std::string> result;
    matcher_utf8_grapheme_t grapheme{};

    while (matcher_utf8_next(&cursor, &grapheme)) {
        result.emplace_back(
            reinterpret_cast<const char*>(grapheme.data),
            grapheme.length
        );
    }

    return result;
}

}  // namespace

TEST(MatcherUtf8, EmptyInput)
{
    matcher_utf8_text_t text{};

    ASSERT_EQ(
        matcher_utf8_normalize_nfc(nullptr, 0u, &text),
        MANGEN_MATCHER_OK
    );

    EXPECT_NE(text.data, nullptr);
    EXPECT_EQ(text.length, 0u);

    matcher_utf8_text_free(&text);

    EXPECT_EQ(text.data, nullptr);
    EXPECT_EQ(text.length, 0u);
}

TEST(MatcherUtf8, NfcNormalization)
{
    const unsigned char decomposed[] = {
        'e', 0xCC, 0x81
    };

    matcher_utf8_text_t text{};

    ASSERT_EQ(
        matcher_utf8_normalize_nfc(
            decomposed,
            sizeof(decomposed),
            &text
        ),
        MANGEN_MATCHER_OK
    );

    const std::string actual(
        reinterpret_cast<const char*>(text.data),
        text.length
    );

    EXPECT_EQ(actual, "\xC3\xA9");

    matcher_utf8_text_free(&text);
}

TEST(MatcherUtf8, CombiningCharacterIsOneGrapheme)
{
    const std::string input = "e\xCC\x81";

    const auto graphemes = split_graphemes(input);

    ASSERT_EQ(graphemes.size(), 1u);
    EXPECT_EQ(graphemes[0], input);
}

TEST(MatcherUtf8, CyrillicCharacterIsOneGrapheme)
{
    const std::string input = "\xD1\x8F";

    const auto graphemes = split_graphemes(input);

    ASSERT_EQ(graphemes.size(), 1u);
    EXPECT_EQ(graphemes[0], input);
}

TEST(MatcherUtf8, EmojiWithSkinToneIsOneGrapheme)
{
    const std::string input = "👍🏽";

    const auto graphemes = split_graphemes(input);

    ASSERT_EQ(graphemes.size(), 1u);
    EXPECT_EQ(graphemes[0], input);
}

TEST(MatcherUtf8, ZWJEmojiIsOneGrapheme)
{
    const std::string input = "a👩‍💻b";

    const auto graphemes = split_graphemes(input);

    ASSERT_EQ(graphemes.size(), 3u);

    EXPECT_EQ(graphemes[0], "a");
    EXPECT_EQ(graphemes[1], "👩‍💻");
    EXPECT_EQ(graphemes[2], "b");
}

TEST(MatcherUtf8, TwoFlagsAreTwoGraphemes)
{
    const std::string input = "🇷🇺🇺🇸";

    const auto graphemes = split_graphemes(input);

    ASSERT_EQ(graphemes.size(), 2u);

    EXPECT_EQ(graphemes[0], "🇷🇺");
    EXPECT_EQ(graphemes[1], "🇺🇸");
}

TEST(MatcherUtf8, InvalidBytesRemainSeparate)
{
    const unsigned char input[] = {
        0xFF, 'e', 0xCC, 0x81, 0x80
    };

    matcher_utf8_text_t text{};

    ASSERT_EQ(
        matcher_utf8_normalize_nfc(
            input,
            sizeof(input),
            &text
        ),
        MANGEN_MATCHER_OK
    );

    matcher_utf8_cursor_t cursor{};

    matcher_utf8_cursor_init(
        &cursor,
        text.data,
        text.length
    );

    matcher_utf8_grapheme_t grapheme{};

    ASSERT_TRUE(matcher_utf8_next(&cursor, &grapheme));
    EXPECT_FALSE(grapheme.valid_utf8);
    EXPECT_EQ(grapheme.length, 1u);
    EXPECT_EQ(grapheme.data[0], 0xFF);

    ASSERT_TRUE(matcher_utf8_next(&cursor, &grapheme));
    EXPECT_TRUE(grapheme.valid_utf8);
    EXPECT_EQ(grapheme.length, 2u);

    ASSERT_TRUE(matcher_utf8_next(&cursor, &grapheme));
    EXPECT_FALSE(grapheme.valid_utf8);
    EXPECT_EQ(grapheme.length, 1u);
    EXPECT_EQ(grapheme.data[0], 0x80);

    EXPECT_FALSE(matcher_utf8_next(&cursor, &grapheme));

    matcher_utf8_text_free(&text);
}

TEST(MatcherUtf8, RejectsNonEmptyOutput)
{
    matcher_utf8_text_t text{};

    const unsigned char input[] = {'a'};

    ASSERT_EQ(
        matcher_utf8_normalize_nfc(
            input,
            sizeof(input),
            &text
        ),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(
        matcher_utf8_normalize_nfc(
            input,
            sizeof(input),
            &text
        ),
        MANGEN_MATCHER_INVALID_ARGUMENT
    );

    matcher_utf8_text_free(&text);
}
