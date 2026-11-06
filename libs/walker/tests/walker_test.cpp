#include <gtest/gtest.h>

#include "mangen/walker.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

namespace {

using CatalogPtr = std::unique_ptr<
    mangen_catalog_t,
    decltype(&mangen_catalog_destroy)
>;

using MatcherPtr = std::unique_ptr<
    mangen_matcher_t,
    decltype(&mangen_matcher_free)
>;

class TempTree {
public:
    TempTree() {
        char pattern[] = "/tmp/mangen-walker-XXXXXX";

        char *created = mkdtemp(pattern);

        if (created == nullptr) {
            return;
        }

        char *canonical = realpath(created, nullptr);

        if (canonical != nullptr) {
            root_ = canonical;
            std::free(canonical);
        } else {
            root_ = created;
        }
    }

    ~TempTree() {
        for (auto it = files_.rbegin();
             it != files_.rend();
             ++it) {
            unlink(it->c_str());
        }

        for (auto it = directories_.rbegin();
             it != directories_.rend();
             ++it) {
            rmdir(it->c_str());
        }

        if (!root_.empty()) {
            rmdir(root_.c_str());
        }
    }

    TempTree(const TempTree&) = delete;
    TempTree& operator=(const TempTree&) = delete;

    bool valid() const {
        return !root_.empty();
    }

    const std::string& root() const {
        return root_;
    }

    std::string path(const std::string& relative) const {
        return root_ + "/" + relative;
    }

    bool make_dir(const std::string& relative) {
        const std::string absolute = path(relative);

        if (mkdir(absolute.c_str(), 0700) != 0) {
            return false;
        }

        directories_.push_back(absolute);
        return true;
    }

    bool make_file(
        const std::string& relative,
        const std::string& content = "test"
    ) {
        const std::string absolute = path(relative);

        FILE *file = std::fopen(absolute.c_str(), "wb");

        if (file == nullptr) {
            return false;
        }

        files_.push_back(absolute);

        const size_t written = std::fwrite(
            content.data(),
            1u,
            content.size(),
            file
        );

        const int close_status = std::fclose(file);

        return written == content.size() &&
               close_status == 0;
    }

    bool make_link(
        const std::string& target,
        const std::string& relative
    ) {
        const std::string absolute = path(relative);

        if (symlink(target.c_str(), absolute.c_str()) != 0) {
            return false;
        }

        files_.push_back(absolute);
        return true;
    }

private:
    std::string root_;
    std::vector<std::string> files_;
    std::vector<std::string> directories_;
};

CatalogPtr make_catalog() {
    mangen_catalog_t *raw = nullptr;

    if (mangen_catalog_create(&raw) != MANGEN_CATALOG_OK) {
        ADD_FAILURE() << "catalog creation failed";
    }

    return CatalogPtr(raw, &mangen_catalog_destroy);
}

mangen_walker_status_t scan(
    const TempTree& tree,
    mangen_catalog_t *catalog,
    mangen_walker_report_t& report,
    size_t workers = 4u,
    size_t capacity = 256u,
    const mangen_matcher_t *const *exclusions = nullptr,
    size_t exclusion_count = 0u
) {
    mangen_walker_options_t options{};

    options.root_path = tree.root().c_str();
    options.exclusions = exclusions;
    options.exclusion_count = exclusion_count;
    options.worker_count = workers;
    options.queue_capacity = capacity;

    return mangen_walker_scan(
        &options,
        catalog,
        &report
    );
}

} // namespace

TEST(WalkerTest, EmptyDirectory) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(tree, catalog.get(), report),
        MANGEN_WALKER_OK
    );

    EXPECT_EQ(mangen_catalog_size(catalog.get()), 0u);
    EXPECT_EQ(report.directories_visited, 1u);
    EXPECT_EQ(report.files_discovered, 0u);
    EXPECT_EQ(report.io_errors, 0u);
}

TEST(WalkerTest, RecursiveTraversalAndSortedCatalog) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    ASSERT_TRUE(tree.make_dir("sub"));
    ASSERT_TRUE(tree.make_dir("sub/deep"));

    ASSERT_TRUE(tree.make_file("z.txt"));
    ASSERT_TRUE(tree.make_file("a.txt"));
    ASSERT_TRUE(tree.make_file("sub/c.txt"));
    ASSERT_TRUE(tree.make_file("sub/deep/b.txt"));

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(tree, catalog.get(), report),
        MANGEN_WALKER_OK
    );

    ASSERT_EQ(mangen_catalog_size(catalog.get()), 4u);
    EXPECT_EQ(report.directories_visited, 3u);
    EXPECT_EQ(report.files_discovered, 4u);

    const std::vector<std::string> expected{
        tree.path("a.txt"),
        tree.path("sub/c.txt"),
        tree.path("sub/deep/b.txt"),
        tree.path("z.txt")
    };

    for (size_t i = 0; i < expected.size(); ++i) {
        auto *entry = mangen_catalog_entry_at(
            catalog.get(),
            i
        );

        ASSERT_NE(entry, nullptr);

        EXPECT_STREQ(
            mangen_catalog_entry_path(entry),
            expected[i].c_str()
        );
    }
}

TEST(WalkerTest, ExcludesFilesAndEntireDirectories) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    ASSERT_TRUE(tree.make_dir("skip_data"));
    ASSERT_TRUE(tree.make_dir("keep"));

    ASSERT_TRUE(tree.make_file("skip_data/hidden.txt"));
    ASSERT_TRUE(tree.make_file("keep/visible.txt"));
    ASSERT_TRUE(tree.make_file("temporary.tmp"));
    ASSERT_TRUE(tree.make_file("root.txt"));

    mangen_matcher_t *raw_tmp = nullptr;
    mangen_matcher_t *raw_skip = nullptr;

    ASSERT_EQ(
        mangen_matcher_compile("*\\.tmp", &raw_tmp),
        MANGEN_MATCHER_OK
    );

    MatcherPtr tmp(raw_tmp, &mangen_matcher_free);

    ASSERT_EQ(
        mangen_matcher_compile("skip*", &raw_skip),
        MANGEN_MATCHER_OK
    );

    MatcherPtr skip(raw_skip, &mangen_matcher_free);

    const mangen_matcher_t *exclusions[]{
        tmp.get(),
        skip.get()
    };

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(
            tree,
            catalog.get(),
            report,
            4u,
            128u,
            exclusions,
            2u
        ),
        MANGEN_WALKER_OK
    );

    EXPECT_EQ(mangen_catalog_size(catalog.get()), 2u);
    EXPECT_EQ(report.directories_excluded, 1u);
    EXPECT_EQ(report.files_excluded, 1u);
    EXPECT_EQ(report.files_discovered, 2u);
}

TEST(WalkerTest, FileSymlinksAreIncludedDirectorySymlinksSkipped) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    ASSERT_TRUE(tree.make_dir("sub"));
    ASSERT_TRUE(tree.make_file("target.txt"));
    ASSERT_TRUE(tree.make_file("sub/nested.txt"));

    ASSERT_TRUE(tree.make_link("target.txt", "linked.txt"));
    ASSERT_TRUE(tree.make_link("sub", "linked_dir"));

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(tree, catalog.get(), report),
        MANGEN_WALKER_OK
    );

    EXPECT_EQ(mangen_catalog_size(catalog.get()), 3u);
    EXPECT_EQ(report.directories_visited, 2u);
    EXPECT_EQ(report.files_discovered, 3u);
}

TEST(WalkerTest, BrokenSymlinkReportsPartialIOError) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    ASSERT_TRUE(tree.make_file("good.txt"));
    ASSERT_TRUE(tree.make_link("missing.txt", "broken.txt"));

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    EXPECT_EQ(
        scan(tree, catalog.get(), report),
        MANGEN_WALKER_PARTIAL_IO_ERROR
    );

    EXPECT_EQ(mangen_catalog_size(catalog.get()), 1u);
    EXPECT_EQ(report.files_discovered, 1u);
    EXPECT_EQ(report.io_errors, 1u);
}

TEST(WalkerTest, DeepDirectoryTree) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    std::string relative;

    for (size_t i = 0; i < 25u; ++i) {
        if (!relative.empty()) {
            relative += "/";
        }

        relative += "d";

        ASSERT_TRUE(tree.make_dir(relative));
    }

    ASSERT_TRUE(tree.make_file(relative + "/leaf.txt"));

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(tree, catalog.get(), report, 4u, 64u),
        MANGEN_WALKER_OK
    );

    EXPECT_EQ(report.directories_visited, 26u);
    EXPECT_EQ(report.files_discovered, 1u);
    EXPECT_EQ(mangen_catalog_size(catalog.get()), 1u);
}

TEST(WalkerTest, ParallelTraversalPreservesUniquePaths) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    constexpr size_t directory_count = 16u;
    constexpr size_t files_per_directory = 8u;

    for (size_t d = 0; d < directory_count; ++d) {
        const std::string directory =
            "dir-" + std::to_string(d);

        ASSERT_TRUE(tree.make_dir(directory));

        for (size_t f = 0; f < files_per_directory; ++f) {
            ASSERT_TRUE(
                tree.make_file(
                    directory + "/file-" + std::to_string(f)
                )
            );
        }
    }

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    ASSERT_EQ(
        scan(tree, catalog.get(), report, 8u, 256u),
        MANGEN_WALKER_OK
    );

    ASSERT_EQ(mangen_catalog_size(catalog.get()), 128u);
    EXPECT_EQ(report.directories_visited, 17u);
    EXPECT_EQ(report.files_discovered, 128u);

    for (size_t i = 1; i < 128u; ++i) {
        const char *previous = mangen_catalog_entry_path(
            mangen_catalog_entry_at(catalog.get(), i - 1u)
        );

        const char *current = mangen_catalog_entry_path(
            mangen_catalog_entry_at(catalog.get(), i)
        );

        ASSERT_NE(previous, nullptr);
        ASSERT_NE(current, nullptr);

        EXPECT_LT(std::strcmp(previous, current), 0);
    }
}

TEST(WalkerTest, QueueOverflowAbortsTraversal) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    for (size_t i = 0; i < 80u; ++i) {
        ASSERT_TRUE(
            tree.make_dir("directory-" + std::to_string(i))
        );
    }

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    mangen_walker_report_t report{};

    EXPECT_EQ(
        scan(tree, catalog.get(), report, 1u, 2u),
        MANGEN_WALKER_QUEUE_FULL
    );
}

TEST(WalkerTest, InvalidRootDirectory) {
    TempTree tree;
    ASSERT_TRUE(tree.valid());

    auto catalog = make_catalog();
    ASSERT_TRUE(catalog);

    const std::string nonexistent =
        tree.path("nonexistent");

    mangen_walker_options_t options{};

    options.root_path = nonexistent.c_str();
    options.worker_count = 2u;
    options.queue_capacity = 16u;

    mangen_walker_report_t report{};

    EXPECT_EQ(
        mangen_walker_scan(
            &options,
            catalog.get(),
            &report
        ),
        MANGEN_WALKER_INVALID_ROOT
    );
}
