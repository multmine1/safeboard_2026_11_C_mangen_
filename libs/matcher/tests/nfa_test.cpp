#include "nfa.h"

#include <gtest/gtest.h>

#include <string>

namespace {

std::string edge_literal(
    const matcher_nfa_t& nfa,
    const matcher_nfa_edge_t& edge
)
{
    return std::string(
        reinterpret_cast<const char*>(
            nfa.literals + edge.literal_offset
        ),
        edge.literal_length
    );
}

}  // namespace

TEST(MatcherNfa, EmptyPattern)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("", &nfa),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(nfa.state_count, 1u);
    EXPECT_EQ(nfa.edge_count, 0u);

    EXPECT_EQ(nfa.start_state, 0u);
    EXPECT_EQ(nfa.accept_state, 0u);

    ASSERT_NE(nfa.edge_offsets, nullptr);

    EXPECT_EQ(nfa.edge_offsets[0], 0u);
    EXPECT_EQ(nfa.edge_offsets[1], 0u);

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, SingleLiteral)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 2u);
    ASSERT_EQ(nfa.edge_count, 1u);

    EXPECT_EQ(nfa.edges[0].kind, MATCHER_NFA_LITERAL);
    EXPECT_EQ(nfa.edges[0].to, 1u);

    EXPECT_EQ(edge_literal(nfa, nfa.edges[0]), "a");

    EXPECT_EQ(nfa.edge_offsets[0], 0u);
    EXPECT_EQ(nfa.edge_offsets[1], 1u);
    EXPECT_EQ(nfa.edge_offsets[2], 1u);

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, StarCreatesSelfLoop)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("*", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 1u);
    ASSERT_EQ(nfa.edge_count, 1u);

    EXPECT_EQ(nfa.start_state, 0u);
    EXPECT_EQ(nfa.accept_state, 0u);

    EXPECT_EQ(nfa.edges[0].kind, MATCHER_NFA_ANY_MANY);
    EXPECT_EQ(nfa.edges[0].to, 0u);

    EXPECT_EQ(nfa.edge_offsets[0], 0u);
    EXPECT_EQ(nfa.edge_offsets[1], 1u);

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, AStarB)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a*b", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 3u);
    ASSERT_EQ(nfa.edge_count, 3u);

    EXPECT_EQ(nfa.start_state, 0u);
    EXPECT_EQ(nfa.accept_state, 2u);

    EXPECT_EQ(nfa.edge_offsets[0], 0u);
    EXPECT_EQ(nfa.edge_offsets[1], 1u);
    EXPECT_EQ(nfa.edge_offsets[2], 3u);
    EXPECT_EQ(nfa.edge_offsets[3], 3u);

    EXPECT_EQ(nfa.edges[0].kind, MATCHER_NFA_LITERAL);
    EXPECT_EQ(nfa.edges[0].to, 1u);
    EXPECT_EQ(edge_literal(nfa, nfa.edges[0]), "a");

    EXPECT_EQ(nfa.edges[1].kind, MATCHER_NFA_ANY_MANY);
    EXPECT_EQ(nfa.edges[1].to, 1u);

    EXPECT_EQ(nfa.edges[2].kind, MATCHER_NFA_LITERAL);
    EXPECT_EQ(nfa.edges[2].to, 2u);
    EXPECT_EQ(edge_literal(nfa, nfa.edges[2]), "b");

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, RepeatedStarsAreCollapsed)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a*****b", &nfa),
        MANGEN_MATCHER_OK
    );

    EXPECT_EQ(nfa.state_count, 3u);
    EXPECT_EQ(nfa.edge_count, 3u);

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, DotAdvancesOneState)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a.b", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 4u);
    ASSERT_EQ(nfa.edge_count, 3u);

    EXPECT_EQ(nfa.edges[1].kind, MATCHER_NFA_ANY_ONE);
    EXPECT_EQ(nfa.edges[1].to, 2u);

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, EscapedStarIsLiteral)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a\\*b", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 4u);
    ASSERT_EQ(nfa.edge_count, 3u);

    EXPECT_EQ(nfa.edges[1].kind, MATCHER_NFA_LITERAL);
    EXPECT_EQ(edge_literal(nfa, nfa.edges[1]), "*");

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, EmojiIsOneTransition)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("👩‍💻", &nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(nfa.state_count, 2u);
    ASSERT_EQ(nfa.edge_count, 1u);

    EXPECT_EQ(nfa.edges[0].kind, MATCHER_NFA_LITERAL);
    EXPECT_EQ(edge_literal(nfa, nfa.edges[0]), "👩‍💻");

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, OwnsLiteralStorage)
{
    matcher_pattern_t pattern{};
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_pattern_parse("abc", &pattern),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_nfa_build(&pattern, &nfa),
        MANGEN_MATCHER_OK
    );

    matcher_pattern_free(&pattern);

    ASSERT_EQ(nfa.edge_count, 3u);

    EXPECT_EQ(edge_literal(nfa, nfa.edges[0]), "a");
    EXPECT_EQ(edge_literal(nfa, nfa.edges[1]), "b");
    EXPECT_EQ(edge_literal(nfa, nfa.edges[2]), "c");

    matcher_nfa_free(&nfa);
}

TEST(MatcherNfa, FreeResetsStructure)
{
    matcher_nfa_t nfa{};

    ASSERT_EQ(
        matcher_nfa_from_pattern("a*b", &nfa),
        MANGEN_MATCHER_OK
    );

    matcher_nfa_free(&nfa);

    EXPECT_EQ(nfa.edges, nullptr);
    EXPECT_EQ(nfa.edge_offsets, nullptr);
    EXPECT_EQ(nfa.literals, nullptr);

    EXPECT_EQ(nfa.state_count, 0u);
    EXPECT_EQ(nfa.edge_count, 0u);
    EXPECT_EQ(nfa.literals_length, 0u);
}

TEST(MatcherNfa, RejectsInvalidPattern)
{
    matcher_nfa_t nfa{};

    EXPECT_EQ(
        matcher_nfa_from_pattern("\\", &nfa),
        MANGEN_MATCHER_INVALID_PATTERN
    );

    EXPECT_EQ(nfa.edges, nullptr);
    EXPECT_EQ(nfa.edge_offsets, nullptr);
}
