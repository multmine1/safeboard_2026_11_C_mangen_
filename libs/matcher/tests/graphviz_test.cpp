#include <gtest/gtest.h>

extern "C" {
#include "dfa.h"
#include "graphviz.h"
#include "min_dfa.h"
#include "nfa.h"
}

#include <array>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef MANGEN_GRAPHVIZ_OUTPUT_DIR
#error MANGEN_GRAPHVIZ_OUTPUT_DIR is not defined
#endif

namespace {

namespace fs = std::filesystem;

using FilePtr = std::unique_ptr<
    FILE,
    decltype(&std::fclose)
>;

struct Automata {
    matcher_nfa_t nfa{};
    matcher_dfa_t dfa{};
    matcher_dfa_t minimized{};

    Automata() = default;

    Automata(const Automata &) = delete;
    Automata &operator=(const Automata &) = delete;

    ~Automata() {
        matcher_dfa_free(&minimized);
        matcher_dfa_free(&dfa);
        matcher_nfa_free(&nfa);
    }
};

std::string read_dot(FILE *stream) {
    std::rewind(stream);

    std::string result;
    char buffer[4096];

    while (true) {
        const size_t count = std::fread(
            buffer,
            1,
            sizeof(buffer),
            stream
        );

        result.append(buffer, count);

        if (count < sizeof(buffer)) {
            break;
        }
    }

    return result;
}

template <typename Writer>
bool write_dot_file(
    const fs::path &path,
    Writer writer
) {
    FILE *stream = std::fopen(path.c_str(), "wb");

    if (stream == nullptr) {
        return false;
    }

    const bool written = writer(stream);
    const bool closed = std::fclose(stream) == 0;

    return written && closed;
}

bool render_svg(
    const fs::path &dot_path,
    const fs::path &svg_path
) {
    const pid_t child = fork();

    if (child < 0) {
        return false;
    }

    if (child == 0) {
        execlp(
            "dot",
            "dot",
            "-Tsvg",
            dot_path.c_str(),
            "-o",
            svg_path.c_str(),
            static_cast<char *>(nullptr)
        );

        _exit(127);
    }

    int status = 0;
    pid_t result;

    do {
        result = waitpid(child, &status, 0);
    } while (result < 0 && errno == EINTR);

    return result == child &&
           WIFEXITED(status) &&
           WEXITSTATUS(status) == 0;
}

TEST(GraphvizTest, NfaExportsLiteralAndWildcardEdges) {
    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern("a*b", &automata.nfa),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_nfa(
            &automata.nfa,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(dot.find("digraph NFA"), std::string::npos);
    EXPECT_NE(dot.find("start -> s0"), std::string::npos);
    EXPECT_NE(dot.find("doublecircle"), std::string::npos);
    EXPECT_NE(dot.find("L:a"), std::string::npos);
    EXPECT_NE(dot.find("L:b"), std::string::npos);
    EXPECT_NE(dot.find("label=\"*\""), std::string::npos);
}

TEST(GraphvizTest, DfaUsesCompressedAlphabet) {
    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern("a", &automata.nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(
            &automata.nfa,
            &automata.dfa
        ),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_dfa(
            &automata.dfa,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(dot.find("digraph DFA"), std::string::npos);
    EXPECT_NE(dot.find("OTHER"), std::string::npos);
    EXPECT_NE(dot.find("c1:a"), std::string::npos);
    EXPECT_NE(dot.find("doublecircle"), std::string::npos);
}

TEST(GraphvizTest, MinDfaGroupsTransitionsByDestination) {
    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern("a", &automata.nfa),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(
            &automata.nfa,
            &automata.dfa
        ),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(
            &automata.dfa,
            &automata.minimized
        ),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_min_dfa(
            &automata.minimized,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(
        dot.find("digraph Min_DFA"),
        std::string::npos
    );

    EXPECT_NE(
        dot.find("OTHER, c1:a"),
        std::string::npos
    );
}

TEST(GraphvizTest, UnicodeLiteralIsPreserved) {
    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern(u8"é", &automata.nfa),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_nfa(
            &automata.nfa,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(dot.find(u8"é"), std::string::npos);
}

TEST(GraphvizTest, InvalidUtf8IsEscaped) {
    const char pattern[] = {
        static_cast<char>(0xFF),
        '\0'
    };

    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern(pattern, &automata.nfa),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_nfa(
            &automata.nfa,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(
        dot.find("\\\\xFF"),
        std::string::npos
    );
}

TEST(GraphvizTest, QuotesAreEscaped) {
    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern("a\"b", &automata.nfa),
        MANGEN_MATCHER_OK
    );

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    ASSERT_TRUE(
        matcher_graphviz_write_nfa(
            &automata.nfa,
            stream.get()
        )
    );

    const std::string dot = read_dot(stream.get());

    EXPECT_NE(
        dot.find("\\\""),
        std::string::npos
    );
}

TEST(GraphvizTest, RejectsInvalidArguments) {
    matcher_nfa_t invalid_nfa{};
    matcher_dfa_t invalid_dfa{};

    FilePtr stream(std::tmpfile(), &std::fclose);
    ASSERT_NE(stream, nullptr);

    EXPECT_FALSE(
        matcher_graphviz_write_nfa(
            nullptr,
            stream.get()
        )
    );

    EXPECT_FALSE(
        matcher_graphviz_write_nfa(
            &invalid_nfa,
            stream.get()
        )
    );

    EXPECT_FALSE(
        matcher_graphviz_write_dfa(
            &invalid_dfa,
            stream.get()
        )
    );

    EXPECT_FALSE(
        matcher_graphviz_write_min_dfa(
            &invalid_dfa,
            stream.get()
        )
    );

    EXPECT_FALSE(
        matcher_graphviz_write_dfa(
            &invalid_dfa,
            nullptr
        )
    );
}

TEST(GraphvizArtifacts, GenerateDotAndSvg) {
    const fs::path output_dir =
        MANGEN_GRAPHVIZ_OUTPUT_DIR;

    std::error_code error;

    fs::create_directories(output_dir, error);

    ASSERT_FALSE(error) << error.message();

    const std::array<const char *, 6> old_files = {
        "nfa.dot",
        "nfa.svg",
        "dfa.dot",
        "dfa.svg",
        "min_dfa.dot",
        "min_dfa.svg"
    };

    for (const char *name : old_files) {
        error.clear();

        fs::remove(output_dir / name, error);

        ASSERT_FALSE(error)
            << name << ": " << error.message();
    }

    Automata automata;

    ASSERT_EQ(
        matcher_nfa_from_pattern(
            u8"a.*é",
            &automata.nfa
        ),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_dfa_build(
            &automata.nfa,
            &automata.dfa
        ),
        MANGEN_MATCHER_OK
    );

    ASSERT_EQ(
        matcher_min_dfa_build(
            &automata.dfa,
            &automata.minimized
        ),
        MANGEN_MATCHER_OK
    );

    const fs::path nfa_dot = output_dir / "nfa.dot";
    const fs::path dfa_dot = output_dir / "dfa.dot";
    const fs::path min_dot = output_dir / "min_dfa.dot";

    const fs::path nfa_svg = output_dir / "nfa.svg";
    const fs::path dfa_svg = output_dir / "dfa.svg";
    const fs::path min_svg = output_dir / "min_dfa.svg";

    ASSERT_TRUE(
        write_dot_file(
            nfa_dot,
            [&](FILE *stream) {
                return matcher_graphviz_write_nfa(
                    &automata.nfa,
                    stream
                );
            }
        )
    );

    ASSERT_TRUE(
        write_dot_file(
            dfa_dot,
            [&](FILE *stream) {
                return matcher_graphviz_write_dfa(
                    &automata.dfa,
                    stream
                );
            }
        )
    );

    ASSERT_TRUE(
        write_dot_file(
            min_dot,
            [&](FILE *stream) {
                return matcher_graphviz_write_min_dfa(
                    &automata.minimized,
                    stream
                );
            }
        )
    );

    ASSERT_TRUE(render_svg(nfa_dot, nfa_svg));
    ASSERT_TRUE(render_svg(dfa_dot, dfa_svg));
    ASSERT_TRUE(render_svg(min_dot, min_svg));

    for (const char *name : old_files) {
        const fs::path file = output_dir / name;

        error.clear();

        const bool exists = fs::is_regular_file(file, error);

        ASSERT_FALSE(error) << error.message();
        ASSERT_TRUE(exists) << file.string();

        error.clear();

        const auto size = fs::file_size(file, error);

        ASSERT_FALSE(error) << error.message();
        EXPECT_GT(size, 0u) << file.string();
    }
}

} // namespace
