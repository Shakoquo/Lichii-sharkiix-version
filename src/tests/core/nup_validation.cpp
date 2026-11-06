// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <openssl/evp.h>

#include "core/file_sys/nup_container.h"

namespace {

constexpr std::size_t TocOffset = 0x80;
constexpr std::size_t PackageOffset = TocOffset + 0x30;
constexpr std::size_t DirOffset = PackageOffset + 0x40;

void Put(std::vector<u8>& bytes, std::size_t offset, u64 value, std::size_t width = 8) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes[offset + i] = static_cast<u8>(value >> (8 * i));
    }
}

struct NupFixture {
    explicit NupFixture(const std::vector<std::vector<u32>>& dirs = {{1}}) {
        file_offset = DirOffset + dirs.size() * 0x18;
        const std::size_t children_offset = file_offset + 0x50;
        std::size_t child_count{};
        for (const auto& children : dirs) {
            child_count += children.size();
        }
        toc_size = children_offset + child_count * sizeof(u32) - TocOffset;
        const std::size_t blob_offset = (TocOffset + toc_size + 15) & ~std::size_t{15};
        bytes.resize(blob_offset + 16);
        std::copy(FileSys::NupContainer::HeaderMagic.begin(),
                  FileSys::NupContainer::HeaderMagic.end(), bytes.begin());
        Put(bytes, 0x04, 0x80, 2);
        Put(bytes, 0x08, TocOffset);
        Put(bytes, 0x10, toc_size);
        Put(bytes, 0x38, blob_offset);
        Put(bytes, 0x40, bytes.size());
        Put(bytes, 0x48, 1, 4);
        Put(bytes, TocOffset, 0x01434F54, 4);
        Put(bytes, TocOffset + 0x04, 1, 4);
        Put(bytes, TocOffset + 0x08, dirs.size());
        Put(bytes, TocOffset + 0x10, 1);
        Put(bytes, TocOffset + 0x18, toc_size);
        std::size_t slot{};
        for (std::size_t i = 0; i < dirs.size(); ++i) {
            Put(bytes, DirOffset + i * 0x18 + 0x08, slot, 4);
            Put(bytes, DirOffset + i * 0x18 + 0x0C, dirs[i].size(), 4);
            for (u32 child : dirs[i]) {
                Put(bytes, children_offset + slot++ * sizeof(u32), child, 4);
            }
        }
        Put(bytes, file_offset + 0x08, 1);
        Put(bytes, file_offset + 0x10, blob_offset);
        Put(bytes, file_offset + 0x18, 1);
        bytes[blob_offset] = 0x42;
        UpdateHash();
    }

    void UpdateHash() {
        std::array<u8, 32> hash{};
        unsigned int length{};
        REQUIRE(EVP_Digest(bytes.data() + TocOffset, toc_size, hash.data(), &length, EVP_sha256(),
                           nullptr) == 1);
        REQUIRE(length == hash.size());
        std::copy(hash.begin(), hash.end(), bytes.begin() + 0x18);
    }

    FileSys::NupContainer Parse() const {
        return FileSys::NupContainer(std::make_shared<FileSys::VectorVfsFile>(bytes));
    }

    std::vector<u8> bytes;
    std::size_t file_offset{};
    std::size_t toc_size{};
};

} // namespace

TEST_CASE("NUP rejects wrapping table counts", "[nup][nup-validation]") {
    NupFixture fixture;
    SECTION("directory records") {
        Put(fixture.bytes, TocOffset + 0x08, (u64{1} << 61) + 1);
    }
    SECTION("file records") {
        Put(fixture.bytes, TocOffset + 0x10, (u64{1} << 60) + 1);
    }
    fixture.UpdateHash();
    REQUIRE(fixture.Parse().GetStatus() == Loader::ResultStatus::ErrorNUPBadFormat);
}

TEST_CASE("NUP rejects wrapping ranges", "[nup][nup-validation]") {
    NupFixture fixture;
    SECTION("TOC size") {
        Put(fixture.bytes, 0x10, std::numeric_limits<u64>::max());
    }
    SECTION("TOC offset") {
        Put(fixture.bytes, 0x08, std::numeric_limits<u64>::max() - 15);
    }
    SECTION("string pool") {
        Put(fixture.bytes, TocOffset + 0x20,
            std::numeric_limits<u64>::max() - fixture.toc_size + 1);
    }
    SECTION("directory children") {
        Put(fixture.bytes, DirOffset + 0x08, std::numeric_limits<u32>::max(), 4);
        Put(fixture.bytes, DirOffset + 0x0C, 1, 4);
    }
    SECTION("directory name") {
        Put(fixture.bytes, DirOffset, std::numeric_limits<u32>::max(), 4);
        Put(fixture.bytes, DirOffset + 0x04, 1, 4);
    }
    SECTION("file name") {
        Put(fixture.bytes, fixture.file_offset, std::numeric_limits<u32>::max(), 4);
        Put(fixture.bytes, fixture.file_offset + 0x04, 1, 4);
    }
    SECTION("file extent") {
        Put(fixture.bytes, fixture.file_offset + 0x10, std::numeric_limits<u64>::max() - 15);
        Put(fixture.bytes, fixture.file_offset + 0x18, 32);
    }
    fixture.UpdateHash();
    REQUIRE(fixture.Parse().GetStatus() == Loader::ResultStatus::ErrorNUPBadFormat);
}

TEST_CASE("NUP ignores optional metadata with wrapping bounds", "[nup][nup-validation]") {
    NupFixture fixture;
    Put(fixture.bytes, PackageOffset + 0x20, std::numeric_limits<u64>::max() - 15);
    Put(fixture.bytes, PackageOffset + 0x28, 32, 4);
    fixture.UpdateHash();
    auto container = fixture.Parse();
    REQUIRE(container.GetStatus() == Loader::ResultStatus::Success);
    REQUIRE(container.GetPackages().size() == 1);
    CHECK(container.GetPackages()[0].meta_offset == 0);
    CHECK(container.GetPackages()[0].meta_size == 0);
}

TEST_CASE("NUP rejects directory cycles", "[nup][nup-validation]") {
    SECTION("self reference") {
        REQUIRE(NupFixture({{0}}).Parse().GetStatus() == Loader::ResultStatus::ErrorNUPBadFormat);
    }
    SECTION("ancestor reference") {
        REQUIRE(NupFixture({{1}, {0}}).Parse().GetStatus() ==
                Loader::ResultStatus::ErrorNUPBadFormat);
    }
    SECTION("unreachable cycle") {
        REQUIRE(NupFixture({{}, {2}, {1}}).Parse().GetStatus() ==
                Loader::ResultStatus::ErrorNUPBadFormat);
    }
}

TEST_CASE("NUP bounds directory depth while allowing shared subtrees", "[nup][nup-validation]") {
    SECTION("depth limit") {
        std::vector<std::vector<u32>> dirs(128);
        for (u32 i = 0; i < dirs.size(); ++i) {
            dirs[i].push_back(i + 1);
        }
        auto container = NupFixture(dirs).Parse();
        REQUIRE(container.GetStatus() == Loader::ResultStatus::Success);
        auto dir = container.GetExeFSDir(0);
        for (u32 i = 1; i < dirs.size(); ++i) {
            REQUIRE(dir->GetSubdirectories().size() == 1);
            dir = dir->GetSubdirectories()[0];
        }
        REQUIRE(dir->GetFiles().size() == 1);
        CHECK(dir->GetFiles()[0]->ReadByte(0) == 0x42);
        dirs.push_back({129});
        REQUIRE(NupFixture(dirs).Parse().GetStatus() == Loader::ResultStatus::ErrorNUPBadFormat);
    }
    SECTION("shared subtree") {
        auto container = NupFixture({{1, 2}, {3}, {3}, {4}}).Parse();
        REQUIRE(container.GetStatus() == Loader::ResultStatus::Success);
        REQUIRE(container.GetExeFSDir(0)->GetSubdirectories().size() == 2);
    }
    SECTION("previously visited subtree exceeds depth limit") {
        std::vector<std::vector<u32>> dirs(129);
        for (u32 i = 1; i < dirs.size(); ++i) {
            dirs[i].push_back(i - 1);
        }
        REQUIRE(NupFixture(dirs).Parse().GetStatus() == Loader::ResultStatus::ErrorNUPBadFormat);
    }
}
