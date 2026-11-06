#include "pattern.h"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string literal_at(
    const matcher_pattern_t& pattern,
    std::size_t index
)
{
    const matcher_token_t& token = pattern.tokens[index];

    return std::string(
        reinterpret_cast<const char*>(
            pattern.literals + token.literal_offset
        ),
        token.literal_length
    );
}

}  // namespace

TEST(MatcherPattern, EmptyPattern)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("", &pattern),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(pattern.count, 0u);
    EXPECT_EQ(pattern.literals_length, 0u);

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, ConsecutiveStarsAreCollapsed)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("a***b", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 3u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(pattern.tokens[1].kind, MATCHER_TOKEN_ANY_MANY);
    EXPECT_EQ(pattern.tokens[2].kind, MATCHER_TOKEN_LITERAL);

    EXPECT_EQ(literal_at(pattern, 0), "a");
    EXPECT_EQ(literal_at(pattern, 2), "b");

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, EscapedStarIsLiteral)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("a\\*b", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 3u);

    EXPECT_EQ(literal_at(pattern, 0), "a");
    EXPECT_EQ(literal_at(pattern, 1), "*");
    EXPECT_EQ(literal_at(pattern, 2), "b");

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, EscapedDotIsLiteral)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("a\\.b", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 3u);

    EXPECT_EQ(pattern.tokens[1].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(literal_at(pattern, 1), ".");

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, DotIsAnyOne)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("a.b", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 3u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(pattern.tokens[1].kind, MATCHER_TOKEN_ANY_ONE);
    EXPECT_EQ(pattern.tokens[2].kind, MATCHER_TOKEN_LITERAL);

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, DecomposedCharacterIsNormalized)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("e\xCC\x81*", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 2u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(literal_at(pattern, 0), "\xC3\xA9");

    EXPECT_EQ(pattern.tokens[1].kind, MATCHER_TOKEN_ANY_MANY);

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, EmojiIsSingleLiteral)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("👩‍💻", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 1u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(literal_at(pattern, 0), "👩‍💻");

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, EscapeConsumesWholeGrapheme)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("\\👩‍💻", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 1u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(literal_at(pattern, 0), "👩‍💻");

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, InvalidUtf8ByteIsLiteral)
{
    const char source[] = {
        static_cast<char>(0xFF),
        '\0'
    };

    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse(source, &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(pattern.count, 1u);

    EXPECT_EQ(pattern.tokens[0].kind, MATCHER_TOKEN_LITERAL);
    EXPECT_EQ(pattern.tokens[0].literal_length, 1u);
    EXPECT_EQ(pattern.literals[0], 0xFF);

    matcher_pattern_free(&pattern);
}

TEST(MatcherPattern, TrailingEscapeIsInvalid)
{
    matcher_pattern_t pattern{};

    EXPECT_EQ(
        matcher_pattern_parse("\\", &pattern),
        MANGEN_MATCHER_INVALID_PATTERN
    );

    EXPECT_EQ(pattern.tokens, nullptr);
    EXPECT_EQ(pattern.count, 0u);
    EXPECT_EQ(pattern.literals, nullptr);
}

TEST(MatcherPattern, PatternLengthIsLimited)
{
    const std::string source(1025u, 'a');

    matcher_pattern_t pattern{};

    EXPECT_EQ(
        matcher_pattern_parse(source.c_str(), &pattern),
        MANGEN_MATCHER_LIMIT_EXCEEDED
    );

    EXPECT_EQ(pattern.tokens, nullptr);
    EXPECT_EQ(pattern.literals, nullptr);
}

TEST(MatcherPattern, FreeResetsStructure)
{
    matcher_pattern_t pattern{};

    ASSERT_EQ(
        matcher_pattern_parse("abc", &pattern),
        MANGEN_MATCHER_OK
    );

    matcher_pattern_free(&pattern);

    EXPECT_EQ(pattern.tokens, nullptr);
    EXPECT_EQ(pattern.count, 0u);
    EXPECT_EQ(pattern.literals, nullptr);
    EXPECT_EQ(pattern.literals_length, 0u);
}
