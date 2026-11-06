#include "min_dfa.h"
#include "utf8.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

bool accepts(
    const matcher_dfa_t& dfa,
    const std::string& input
)
{
    matcher_utf8_text_t normalized{};

    if (
        matcher_utf8_normalize_nfc(
            reinterpret_cast<const unsigned char*>(input.data()),
            input.size(),
            &normalized
        ) != MANGEN_MATCHER_OK
    ) {
        return false;
    }

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

    const bool result = matcher_dfa_is_accepting(
        &dfa,
        state
    );

    matcher_utf8_text_free(&normalized);

    return result;
}

void check_equivalence(
    const char *pattern,
    const std::vector<std::string>& inputs
)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_dfa_t minimal{};

    ASSERT_EQ(
        matcher_nfa_from_pattern(pattern, &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(&dfa, &minimal),
        MANGEN_MATCHER_OK
    );

    EXPECT_LE(minimal.state_count, dfa.state_count);
    EXPECT_EQ(minimal.class_count, dfa.class_count);
    EXPECT_EQ(minimal.start_state, 0u);

    for (const std::string& input : inputs) {
        EXPECT_EQ(
            accepts(dfa, input),
            accepts(minimal, input)
        ) << "Pattern: " << pattern
          << ", input: " << input;
    }

    matcher_dfa_free(&minimal);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}

}  // namespace

TEST(MatcherMinDfa, StarHasOneState)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_dfa_t minimal{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("*", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(&dfa, &minimal),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(minimal.state_count, 1u);
    EXPECT_TRUE(matcher_dfa_is_accepting(&minimal, 0u));

    matcher_dfa_free(&minimal);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}

TEST(MatcherMinDfa, SingleLiteralHasThreeStates)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_dfa_t minimal{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(&dfa, &minimal),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(minimal.state_count, 3u);

    matcher_dfa_free(&minimal);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}

TEST(MatcherMinDfa, ContainsLiteralHasTwoStates)
{
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_dfa_t minimal{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("*a*", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(&nfa, &dfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(&dfa, &minimal),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(minimal.state_count, 2u);

    matcher_dfa_free(&minimal);
    matcher_dfa_free(&dfa);
    matcher_nfa_free(&nfa);
}

TEST(MatcherMinDfa, AStarB)
{
    check_equivalence(
        "a*b",
        {
            "",
            "a",
            "b",
            "ab",
            "axb",
            "axyzb",
            "aba",
            "a👩‍💻b",
            "a🇷🇺b"
        }
    );
}

TEST(MatcherMinDfa, Dot)
{
    check_equivalence(
        ".",
        {
            "",
            "a",
            "ab",
            "я",
            "e\xCC\x81",
            "👍🏽",
            "👩‍💻",
            "🇷🇺"
        }
    );
}

TEST(MatcherMinDfa, CanonicalEquivalence)
{
    check_equivalence(
        "\xC3\xA9*",
        {
            "",
            "\xC3\xA9",
            "e\xCC\x81",
            "\xC3\xA9xyz",
            "xyz"
        }
    );
}

TEST(MatcherMinDfa, EmptyPattern)
{
    check_equivalence(
        "",
        {
            "",
            "a",
            "abc",
            "👩‍💻"
        }
    );
}

TEST(MatcherMinDfa, ExhaustiveSmallAlphabet)
{
    const std::vector<std::string> patterns = {
        "",
        "*",
        ".",
        "a",
        "ab",
        "a*b",
        "*a*",
        "a.b",
        "a***b",
        "a\\*b"
    };

    std::vector<std::string> inputs = {""};

    std::vector<std::string> level = {""};

    for (int length = 1; length <= 5; ++length) {
        std::vector<std::string> next;

        for (const std::string& prefix : level) {
            for (char symbol : {'a', 'b', 'x'}) {
                next.push_back(prefix + symbol);
            }
        }

        inputs.insert(
            inputs.end(),
            next.begin(),
            next.end()
        );

        level = std::move(next);
    }

    for (const std::string& pattern : patterns) {
        check_equivalence(
            pattern.c_str(),
            inputs
        );
    }
}
