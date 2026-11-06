#include <gtest/gtest.h>

#include "mangen/hash.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

class TempFile {
public:
    TempFile() {
        char pattern[] = "/tmp/mangen-hash-XXXXXX";

        fd_ = mkstemp(pattern);

        if (fd_ >= 0) {
            path_ = pattern;
        }
    }

    ~TempFile() {
        if (fd_ >= 0) {
            close(fd_);
        }

        if (!path_.empty()) {
            unlink(path_.c_str());
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    bool valid() const {
        return fd_ >= 0;
    }

    const std::string& path() const {
        return path_;
    }

    bool write_all(const std::vector<uint8_t>& data) {
        if (fd_ < 0) {
            return false;
        }

        size_t offset = 0;

        while (offset < data.size()) {
            const ssize_t count = write(
                fd_,
                data.data() + offset,
                data.size() - offset
            );

            if (count < 0 && errno == EINTR) {
                continue;
            }

            if (count <= 0) {
                return false;
            }

            offset += static_cast<size_t>(count);
        }

        return true;
    }

    bool close_for_reading() {
        if (fd_ < 0) {
            return false;
        }

        const int fd = fd_;
        fd_ = -1;

        return close(fd) == 0;
    }

private:
    int fd_ = -1;
    std::string path_;
};

std::string hash_bytes(
    const std::vector<uint8_t>& data,
    mangen_hash_algorithm_t algorithm
) {
    TempFile file;

    if (!file.valid()) {
        ADD_FAILURE() << "mkstemp failed";
        return {};
    }

    if (!file.write_all(data)) {
        ADD_FAILURE() << "write failed";
        return {};
    }

    if (!file.close_for_reading()) {
        ADD_FAILURE() << "close failed";
        return {};
    }

    mangen_hash_digest_t digest{};
    int detail = 0;

    const mangen_hash_status_t status = mangen_hash_file(
        file.path().c_str(),
        algorithm,
        &digest,
        &detail
    );

    if (status != MANGEN_HASH_OK) {
        ADD_FAILURE()
            << "hash failed: status=" << status
            << ", detail=" << detail;
        return {};
    }

    if (digest.size != mangen_hash_digest_size(algorithm)) {
        ADD_FAILURE() << "incorrect binary digest size";
        return {};
    }

    char hex[MANGEN_HASH_MAX_HEX_SIZE]{};

    if (mangen_hash_to_hex(
            &digest,
            hex,
            sizeof(hex)
        ) != MANGEN_HASH_OK) {
        ADD_FAILURE() << "hex conversion failed";
        return {};
    }

    return hex;
}

} // namespace

TEST(HashFileTest, EmptyFileSHA256) {
    EXPECT_EQ(
        hash_bytes({}, MANGEN_HASH_SHA256),
        "E3B0C44298FC1C149AFBF4C8996FB924"
        "27AE41E4649B934CA495991B7852B855"
    );
}

TEST(HashFileTest, EmptyFileSHA384) {
    EXPECT_EQ(
        hash_bytes({}, MANGEN_HASH_SHA384),
        "38B060A751AC96384CD9327EB1B1E36"
        "A21FDB71114BE07434C0CC7BF63F6E1DA"
        "274EDEBFE76F65FBD51AD2F14898B95B"
    );
}

TEST(HashFileTest, EmptyFileSHA512) {
    EXPECT_EQ(
        hash_bytes({}, MANGEN_HASH_SHA512),
        "CF83E1357EEFB8BDF1542850D66D8007"
        "D620E4050B5715DC83F4A921D36CE9CE"
        "47D0D13C5D85F2B0FF8318D2877EEC2F"
        "63B931BD47417A81A538327AF927DA3E"
    );
}

TEST(HashFileTest, KnownVectorAbcSHA256) {
    const std::vector<uint8_t> data{'a', 'b', 'c'};

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA256),
        "BA7816BF8F01CFEA414140DE5DAE2223"
        "B00361A396177A9CB410FF61F20015AD"
    );
}

TEST(HashFileTest, KnownVectorAbcSHA384) {
    const std::vector<uint8_t> data{'a', 'b', 'c'};

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA384),
        "CB00753F45A35E8BB5A03D699AC650072"
        "72C32AB0EDED1631A8B605A43FF5BED8"
        "086072BA1E7CC2358BAECA134C825A7"
    );
}

TEST(HashFileTest, KnownVectorAbcSHA512) {
    const std::vector<uint8_t> data{'a', 'b', 'c'};

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA512),
        "DDAF35A193617ABACC417349AE204131"
        "12E6FA4E89A97EA20A9EEEE64B55D39A"
        "2192992A274FC1A836BA3C23A3FEEBBD"
        "454D4423643CE80E2A9AC94FA54CA49F"
    );
}

TEST(HashFileTest, BinaryFileWithNullBytes) {
    const std::vector<uint8_t> data{
        0x00, 0x01, 0x00, 0xFF, 0x80
    };

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA256),
        "26D5448031BBFB719DE70372970DAFBB"
        "840F27DE5958112DDDDC513EC9B12EE3"
    );
}

TEST(HashFileTest, LargeFileCrossesReadBufferBoundaries) {
    std::vector<uint8_t> data;
    data.reserve(1048579u);

    for (size_t i = 0; i < 4096u; ++i) {
        for (unsigned value = 0; value < 256u; ++value) {
            data.push_back(static_cast<uint8_t>(value));
        }
    }

    data.push_back(0x00);
    data.push_back(0xFF);
    data.push_back(0x00);

    ASSERT_EQ(data.size(), 1048579u);

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA256),
        "E9EC71BE9FA9584FB3C4A4CE82F6347A"
        "B4A49ED663066709CE97AA4E153C2C27"
    );

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA384),
        "C2AB3D2EAE9B5C6EA116C59A3B8B867"
        "3E42C6661BBCE6D1ADB8015D46A138F0"
        "4FBA7A7B295960CE424F5BFFC8A07CBD0"
    );

    EXPECT_EQ(
        hash_bytes(data, MANGEN_HASH_SHA512),
        "2DE95C23A2D27866F3AB9D21FD9393C"
        "9A35FAF2D823708FB7FB82318FDCF95E"
        "94582DC52D982C6A228482F542F61E94"
        "37C5C10FA238029ADE9FB57722E59DD99"
    );
}

TEST(HashFileTest, MissingFileReturnsOpenError) {
    TempFile file;

    ASSERT_TRUE(file.valid());
    ASSERT_TRUE(file.close_for_reading());
    ASSERT_EQ(unlink(file.path().c_str()), 0);

    mangen_hash_digest_t digest{};
    int detail = 0;

    EXPECT_EQ(
        mangen_hash_file(
            file.path().c_str(),
            MANGEN_HASH_SHA256,
            &digest,
            &detail
        ),
        MANGEN_HASH_OPEN_ERROR
    );

    EXPECT_EQ(detail, ENOENT);
    EXPECT_EQ(digest.size, 0u);
}

TEST(HashFileTest, InvalidArguments) {
    mangen_hash_digest_t digest{};
    int detail = 0;

    EXPECT_EQ(
        mangen_hash_file(
            nullptr,
            MANGEN_HASH_SHA256,
            &digest,
            &detail
        ),
        MANGEN_HASH_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_hash_file(
            "",
            MANGEN_HASH_SHA256,
            &digest,
            &detail
        ),
        MANGEN_HASH_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_hash_file(
            "/tmp/irrelevant",
            static_cast<mangen_hash_algorithm_t>(99),
            &digest,
            &detail
        ),
        MANGEN_HASH_INVALID_ARGUMENT
    );

    EXPECT_EQ(
        mangen_hash_file(
            "/tmp/irrelevant",
            MANGEN_HASH_SHA256,
            nullptr,
            &detail
        ),
        MANGEN_HASH_INVALID_ARGUMENT
    );
}
