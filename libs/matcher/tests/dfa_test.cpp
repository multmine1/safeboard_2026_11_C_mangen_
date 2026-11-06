#include "dfa.h"
#include "utf8.h"

#include <gtest/gtest.h>

#include <string>

namespace {

void check_match(
    const char *pattern,
    const std::string& input,
    bool expected
)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_utf8_text_t normalized{};

    ASSERT_EQ(
        matcher_nfa_from_pattern(pattern, &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_utf8_normalize_nfc(
            reinterpret_cast<const unsigned char*>(input.data()),
            input.size(),
            &normalized
        ),
        MANGEN_MATCHER_OK
    );

    matcher_utf8_cursor_t cursor{};

    matcher_utf8_cursor_init(
        &cursor,
        normalized.data,
        normalized.length
    );

    matcher_utf8_grapheme_t grapheme{};

    uint32_t state = dfa.start_state;

    while (matcher_utf8_next(&cursor, &grapheme)) {
        const uint32_t class_id = matcher_dfa_class_of(
            &dfa,
            grapheme.data,
            grapheme.length
        );

        state = dfa.transitions[
            static_cast<std::size_t>(state) *
                dfa.class_count +
            class_id
        ];
    }

    EXPECT_EQ(
        matcher_dfa_is_accepting(&dfa, state),
        expected
    );

    matcher_utf8_text_free(&normalized);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}

}  // namespace

TEST(MatcherDfa, EmptyPattern)
{
    check_match("", "", true);
    check_match("", "a", false);
}

TEST(MatcherDfa, Literal)
{
    check_match("abc", "abc", true);
    check_match("abc", "ab", false);
    check_match("abc", "abcd", false);
    check_match("abc", "xbc", false);
}

TEST(MatcherDfa, Star)
{
    check_match("*", "", true);
    check_match("*", "abc", true);
    check_match("*", "👩‍💻", true);
}

TEST(MatcherDfa, AStarB)
{
    check_match("a*b", "ab", true);
    check_match("a*b", "axb", true);
    check_match("a*b", "axxxb", true);
    check_match("a*b", "a👩‍💻b", true);

    check_match("a*b", "b", false);
    check_match("a*b", "aba", false);
}

TEST(MatcherDfa, DotMatchesOneGrapheme)
{
    check_match(".", "a", true);
    check_match(".", "я", true);
    check_match(".", "e\xCC\x81", true);
    check_match(".", "👍🏽", true);
    check_match(".", "🇷🇺", true);
    check_match(".", "👩‍💻", true);

    check_match(".", "", false);
    check_match(".", "ab", false);
}

TEST(MatcherDfa, EscapedDot)
{
    check_match("\\.", ".", true);
    check_match("\\.", "a", false);
}

TEST(MatcherDfa, CanonicalEquivalence)
{
    check_match("\xC3\xA9", "e\xCC\x81", true);
    check_match("e\xCC\x81", "\xC3\xA9", true);
}

TEST(MatcherDfa, RepeatedStars)
{
    check_match("a*****b", "ab", true);
    check_match("a*****b", "axyzb", true);
    check_match("a*****b", "ac", false);
}

TEST(MatcherDfa, InvalidUtf8AsOneElement)
{
    const std::string invalid(1, static_cast<char>(0xFF));

    check_match(".", invalid, true);
    check_match("*", invalid, true);
    check_match("a", invalid, false);
}

TEST(MatcherDfa, MoreThan64NfaStates)
{
    const std::string pattern(70u, 'a');

    check_match(pattern.c_str(), pattern, true);
    check_match(pattern.c_str(), std::string(69u, 'a'), false);
    check_match(pattern.c_str(), std::string(71u, 'a'), false);
}

TEST(MatcherDfa, AlphabetCompression)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("aaba", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(dfa.class_count, 3u);

    const unsigned char a[] = {'a'};
    const unsigned char b[] = {'b'};
    const unsigned char z[] = {'z'};

    const uint32_t class_a =
        matcher_dfa_class_of(&dfa, a, sizeof(a));

    const uint32_t class_b =
        matcher_dfa_class_of(&dfa, b, sizeof(b));

    EXPECT_NE(class_a, 0u);
    EXPECT_NE(class_b, 0u);
    EXPECT_NE(class_a, class_b);

    EXPECT_EQ(
        matcher_dfa_class_of(&dfa, z, sizeof(z)),
        0u
    );

    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}
