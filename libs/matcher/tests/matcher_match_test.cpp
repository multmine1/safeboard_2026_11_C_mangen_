#include <gtest/gtest.h>

#include <mangen/matcher.h>

#include <memory>
#include <string>

namespace {

using MatcherPtr = std::unique_ptr<
    mangen_matcher_t,
    decltype(&mangen_matcher_free)
>;

MatcherPtr compile_pattern(const char *pattern) {
    mangen_matcher_t *raw = nullptr;

    const auto status =
        mangen_matcher_compile(pattern, &raw);

    EXPECT_EQ(status, MANGEN_MATCHER_OK);

    return MatcherPtr(raw, &mangen_matcher_free);
}

TEST(MatcherMatchTest, EmptyPattern) {
    auto matcher = compile_pattern("");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), ""));
    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "a"));
}

TEST(MatcherMatchTest, LiteralPattern) {
    auto matcher = compile_pattern("file.txt");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), "file.txt")
    );

    EXPECT_FALSE(
        mangen_matcher_matches(matcher.get(), "file.txt.bak")
    );

    EXPECT_FALSE(
        mangen_matcher_matches(matcher.get(), "FILE.TXT")
    );
}

TEST(MatcherMatchTest, SingleGraphemeWildcard) {
    auto matcher = compile_pattern("a.c");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "abc"));
    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "aéc"));

    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "ac"));
    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "abbc"));
}

TEST(MatcherMatchTest, MultipleGraphemeWildcard) {
    auto matcher = compile_pattern("a*c");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "ac"));
    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "abc"));
    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "abbbc"));

    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "ab"));
    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "cab"));
}

TEST(MatcherMatchTest, ConsecutiveStars) {
    auto matcher = compile_pattern("a***b");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "ab"));
    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), "axxxb"));

    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), "axxxc"));
}

TEST(MatcherMatchTest, EscapedMetacharacters) {
    auto matcher = compile_pattern("\\*\\.\\\\");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), "*.\\")
    );

    EXPECT_FALSE(
        mangen_matcher_matches(matcher.get(), "abc")
    );
}

TEST(MatcherMatchTest, UnicodeNfcNormalization) {
    auto matcher = compile_pattern(u8"café");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), u8"café")
    );

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), u8"cafe\u0301")
    );
}

TEST(MatcherMatchTest, CombiningSequenceIsOneGrapheme) {
    auto matcher = compile_pattern(".");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), u8"e\u0301")
    );

    EXPECT_FALSE(
        mangen_matcher_matches(matcher.get(), u8"ae\u0301")
    );
}

TEST(MatcherMatchTest, EmojiSequenceIsOneGrapheme) {
    auto matcher = compile_pattern(".");

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(
        mangen_matcher_matches(matcher.get(), u8"👩‍💻")
    );

    EXPECT_FALSE(
        mangen_matcher_matches(matcher.get(), u8"👩‍💻x")
    );
}

TEST(MatcherMatchTest, InvalidUtf8BytesArePreserved) {
    const char pattern[] = {
        'a',
        static_cast<char>(0xFF),
        'b',
        '\0'
    };

    const char same[] = {
        'a',
        static_cast<char>(0xFF),
        'b',
        '\0'
    };

    const char different[] = {
        'a',
        static_cast<char>(0xFE),
        'b',
        '\0'
    };

    auto matcher = compile_pattern(pattern);

    ASSERT_NE(matcher, nullptr);

    EXPECT_TRUE(mangen_matcher_matches(matcher.get(), same));
    EXPECT_FALSE(mangen_matcher_matches(matcher.get(), different));
}

TEST(MatcherMatchTest, MinimizedDfaStateCount) {
    auto star = compile_pattern("*");
    auto literal = compile_pattern("a");
    auto contains = compile_pattern("*a*");

    ASSERT_NE(star, nullptr);
    ASSERT_NE(literal, nullptr);
    ASSERT_NE(contains, nullptr);

    EXPECT_EQ(mangen_matcher_state_count(star.get()), 1u);
    EXPECT_EQ(mangen_matcher_state_count(literal.get()), 3u);
    EXPECT_EQ(mangen_matcher_state_count(contains.get()), 2u);
}

TEST(MatcherMatchTest, CheckedMatchingInterface) {
    auto matcher = compile_pattern("a*b");

    ASSERT_NE(matcher, nullptr);

    bool matched = false;

    EXPECT_EQ(
        mangen_matcher_match(matcher.get(), "axxxb", &matched),
        MANGEN_MATCHER_OK
    );

    EXPECT_TRUE(matched);

    EXPECT_EQ(
        mangen_matcher_match(matcher.get(), "axxxc", &matched),
        MANGEN_MATCHER_OK
    );

    EXPECT_FALSE(matched);
}

TEST(MatcherMatchTest, InvalidArguments) {
    mangen_matcher_t *raw = nullptr;

    EXPECT_EQ(
        mangen_matcher_compile(nullptr, &raw),
        MANGEN_MATCHER_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_matcher_compile("abc", nullptr),
        MANGEN_MATCHER_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_matcher_compile("\\", &raw),
        MANGEN_MATCHER_INVALID_PATTERN
    );

    bool matched = true;

    EXPECT_EQ(
        mangen_matcher_match(nullptr, "abc", &matched),
        MANGEN_MATCHER_INVALID_ARGUMENT
    );

    EXPECT_FALSE(matched);

    EXPECT_EQ(
        mangen_matcher_match(nullptr, "abc", nullptr),
        MANGEN_MATCHER_INVALID_ARGUMENT
    );

    EXPECT_EQ(mangen_matcher_state_count(nullptr), 0u);

    mangen_matcher_free(nullptr);
}

TEST(MatcherMatchTest, PatternLengthLimit) {
    const std::string pattern(1025, 'a');

    mangen_matcher_t *raw = nullptr;

    EXPECT_EQ(
        mangen_matcher_compile(pattern.c_str(), &raw),
        MANGEN_MATCHER_LIMIT_EXCEEDED
    );

    EXPECT_EQ(raw, nullptr);
}

} // namespace
