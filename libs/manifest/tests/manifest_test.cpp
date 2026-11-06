#include <gtest/gtest.h>

#include "mangen/catalog.h"
#include "mangen/manifest.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <unistd.h>

namespace {

using File = std::unique_ptr<FILE, decltype(&std::fclose)>;

File temporary_file() {
    return File(std::tmpfile(), &std::fclose);
}

std::string read_file(FILE *file) {
    std::rewind(file);

    std::string result;
    std::array<char, 4096> buffer{};

    for (;;) {
        const size_t count = std::fread(
            buffer.data(),
            1u,
            buffer.size(),
            file
        );

        result.append(buffer.data(), count);

        if (count < buffer.size()) {
            break;
        }
    }

    return result;
}

std::string repeated_hex(
    const std::string &byte,
    size_t count
) {
    std::string result;
    result.reserve(byte.size() * count);

    for (size_t i = 0u; i < count; ++i) {
        result += byte;
    }

    return result;
}

class ManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        char directory[] = "/tmp/mangen-manifest-XXXXXX";

        char *created = ::mkdtemp(directory);
        ASSERT_NE(created, nullptr);

        char *canonical = ::realpath(created, nullptr);
        ASSERT_NE(canonical, nullptr);

        root_ = canonical;
        std::free(canonical);

        ASSERT_EQ(
            mangen_catalog_create(&catalog_),
            MANGEN_CATALOG_OK
        );

        ASSERT_EQ(
            mangen_catalog_batch_create(&batch_),
            MANGEN_CATALOG_OK
        );
    }

    void TearDown() override {
        if (batch_ != nullptr) {
            mangen_catalog_batch_destroy(batch_);
        }

        if (catalog_ != nullptr) {
            mangen_catalog_destroy(catalog_);
        }

        if (!root_.empty()) {
            ::rmdir(root_.c_str());
        }
    }

    void append(const std::string &relative_path) {
        const std::string full_path =
            root_ + "/" + relative_path;

        char *owned_path = ::strdup(full_path.c_str());

        ASSERT_NE(owned_path, nullptr);

        const mangen_catalog_status_t status =
            mangen_catalog_batch_append(
                batch_,
                owned_path
            );

        if (status != MANGEN_CATALOG_OK) {
            std::free(owned_path);
        }

        ASSERT_EQ(status, MANGEN_CATALOG_OK);
    }

    void append_absolute(const std::string &absolute_path) {
        char *owned_path = ::strdup(absolute_path.c_str());

        ASSERT_NE(owned_path, nullptr);

        const mangen_catalog_status_t status =
            mangen_catalog_batch_append(
                batch_,
                owned_path
            );

        if (status != MANGEN_CATALOG_OK) {
            std::free(owned_path);
        }

        ASSERT_EQ(status, MANGEN_CATALOG_OK);
    }

    void finalize() {
        mangen_catalog_batch_t *batches[] = {batch_};

        ASSERT_EQ(
            mangen_catalog_finalize(
                catalog_,
                batches,
                1u
            ),
            MANGEN_CATALOG_OK
        );
    }

    void publish_success(
        size_t index,
        size_t digest_size = 32u,
        uint8_t value = 0xABu
    ) {
        ASSERT_LE(digest_size, 64u);

        mangen_catalog_entry_t *entry =
            mangen_catalog_entry_at(catalog_, index);

        ASSERT_NE(entry, nullptr);

        std::array<uint8_t, 64> digest{};
        digest.fill(value);

        ASSERT_EQ(
            mangen_catalog_mark_processing(entry),
            MANGEN_CATALOG_OK
        );

        ASSERT_EQ(
            mangen_catalog_publish_success(
                entry,
                digest.data(),
                digest_size
            ),
            MANGEN_CATALOG_OK
        );
    }

    void publish_error(size_t index, int error_code) {
        mangen_catalog_entry_t *entry =
            mangen_catalog_entry_at(catalog_, index);

        ASSERT_NE(entry, nullptr);

        ASSERT_EQ(
            mangen_catalog_mark_processing(entry),
            MANGEN_CATALOG_OK
        );

        ASSERT_EQ(
            mangen_catalog_publish_error(
                entry,
                error_code
            ),
            MANGEN_CATALOG_OK
        );
    }

    mangen_catalog_t *catalog_ = nullptr;
    mangen_catalog_batch_t *batch_ = nullptr;
    std::string root_;
};

TEST_F(ManifestTest, EmptyCatalogProducesEmptyOutput) {
    finalize();

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    mangen_manifest_report_t report{};

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            &report
        ),
        MANGEN_MANIFEST_OK
    );

    EXPECT_TRUE(read_file(output.get()).empty());
    EXPECT_EQ(report.records_written, 0u);
    EXPECT_EQ(report.files_failed, 0u);
}

TEST_F(ManifestTest, WritesRecordsInLexicographicOrder) {
    append("zeta.txt");
    append("alpha.txt");
    append("dir/beta.txt");

    finalize();

    ASSERT_EQ(mangen_catalog_size(catalog_), 3u);

    for (size_t i = 0u; i < 3u; ++i) {
        publish_success(i);
    }

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    mangen_manifest_report_t report{};

    ASSERT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            &report
        ),
        MANGEN_MANIFEST_OK
    );

    const std::string hex = repeated_hex("AB", 32u);

    const std::string expected =
        "alpha.txt : " + hex + "\n" +
        "dir/beta.txt : " + hex + "\n" +
        "zeta.txt : " + hex + "\n";

    EXPECT_EQ(read_file(output.get()), expected);
    EXPECT_EQ(report.records_written, 3u);
    EXPECT_EQ(report.files_failed, 0u);
}

TEST_F(ManifestTest, EscapesSpecialCharactersInPath) {
    append("name\\part\nline\rend.txt");

    finalize();
    publish_success(0u, 32u, 0x01u);

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    ASSERT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_OK
    );

    const std::string expected =
        "name\\\\part\\nline\\rend.txt : " +
        repeated_hex("01", 32u) +
        "\n";

    EXPECT_EQ(read_file(output.get()), expected);
}

TEST_F(ManifestTest, KeepsColonInPath) {
    append("name:version.txt");

    finalize();
    publish_success(0u, 32u, 0xFFu);

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    ASSERT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_OK
    );

    EXPECT_EQ(
        read_file(output.get()),
        "name:version.txt : " +
            repeated_hex("FF", 32u) +
            "\n"
    );
}

TEST_F(ManifestTest, SupportsSha256Sha384AndSha512) {
    append("sha256.txt");
    append("sha384.txt");
    append("sha512.txt");

    finalize();

    publish_success(0u, 32u, 0x0Au);
    publish_success(1u, 48u, 0x1Bu);
    publish_success(2u, 64u, 0x2Cu);

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    ASSERT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_OK
    );

    const std::string expected =
        "sha256.txt : " + repeated_hex("0A", 32u) + "\n" +
        "sha384.txt : " + repeated_hex("1B", 48u) + "\n" +
        "sha512.txt : " + repeated_hex("2C", 64u) + "\n";

    EXPECT_EQ(read_file(output.get()), expected);
}

TEST_F(ManifestTest, SkipsFailedFilesAndReportsPartialError) {
    append("alpha.txt");
    append("broken.txt");
    append("zeta.txt");

    finalize();

    publish_success(0u, 32u, 0x11u);
    publish_error(1u, ENOENT);
    publish_success(2u, 32u, 0x22u);

    auto output = temporary_file();
    auto diagnostics = temporary_file();

    ASSERT_NE(output, nullptr);
    ASSERT_NE(diagnostics, nullptr);

    mangen_manifest_report_t report{};

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            diagnostics.get(),
            &report
        ),
        MANGEN_MANIFEST_PARTIAL_ERROR
    );

    const std::string expected =
        "alpha.txt : " + repeated_hex("11", 32u) + "\n" +
        "zeta.txt : " + repeated_hex("22", 32u) + "\n";

    EXPECT_EQ(read_file(output.get()), expected);

    const std::string errors =
        read_file(diagnostics.get());

    EXPECT_NE(errors.find("broken.txt"), std::string::npos);

    EXPECT_EQ(report.records_written, 2u);
    EXPECT_EQ(report.files_failed, 1u);
}

TEST_F(ManifestTest, RejectsFileOutsideRoot) {
    append_absolute(root_ + "-other/file.txt");

    finalize();
    publish_success(0u);

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_INVALID_PATH
    );

    EXPECT_TRUE(read_file(output.get()).empty());
}

TEST_F(ManifestTest, RejectsNonexistentRoot) {
    finalize();

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    const std::string missing_root =
        root_ + "/nonexistent-directory";

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            missing_root.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_INVALID_ROOT
    );
}

TEST_F(ManifestTest, RejectsInvalidArguments) {
    finalize();

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    EXPECT_EQ(
        mangen_manifest_write(
            nullptr,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            nullptr,
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            nullptr,
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_INVALID_ARGUMENT
    );
}

TEST_F(ManifestTest, CancellationStopsOutput) {
    append("pending.txt");
    finalize();

    mangen_catalog_cancel(catalog_);

    auto output = temporary_file();
    ASSERT_NE(output, nullptr);

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_CANCELLED
    );

    EXPECT_TRUE(read_file(output.get()).empty());
}

TEST_F(ManifestTest, ReportsOutputFailure) {
    append("file.txt");
    finalize();

    publish_success(0u);

    File output(std::fopen("/dev/null", "r"), &std::fclose);
    ASSERT_NE(output, nullptr);

    EXPECT_EQ(
        mangen_manifest_write(
            catalog_,
            root_.c_str(),
            output.get(),
            nullptr,
            nullptr
        ),
        MANGEN_MANIFEST_OUTPUT_ERROR
    );
}

} // namespace
