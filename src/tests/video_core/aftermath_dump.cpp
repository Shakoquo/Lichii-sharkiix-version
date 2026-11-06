// SPDX-License-Identifier: GPL-3.0-or-later

#include <fstream>
#include <iterator>
#include <stdexcept>
#include <catch2/catch_test_macros.hpp>
#include "common/scope_exit.h"
#include "common/uuid.h"
#include "video_core/vulkan_common/aftermath_dump.h"

namespace {
std::string Read(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
} // namespace

TEST_CASE("Aftermath initialization preserves existing captures", "[aftermath]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("aftermath-test-" + Common::UUID::MakeRandom().FormattedString());
    SCOPE_EXIT {
        std::filesystem::remove_all(root);
    };
    REQUIRE(std::filesystem::create_directory(root));
    std::ofstream{root / "crash.nv-gpudmp"} << "legacy capture";
    const auto first = Vulkan::CreateAftermathDumpDirectory(root);
    REQUIRE(first);
    REQUIRE(Read(root / "crash.nv-gpudmp") == "legacy capture");
    REQUIRE(*first != root);
    std::ofstream{*first / "crash.nv-gpudmp"} << "first capture";
    const auto second = Vulkan::CreateAftermathDumpDirectory(root);
    REQUIRE(second);
    REQUIRE(*first != *second);
    REQUIRE(Read(*first / "crash.nv-gpudmp") == "first capture");
    const auto occupied = root / "occupied";
    std::ofstream{occupied} << "not a directory";
    REQUIRE_FALSE(Vulkan::CreateAftermathDumpDirectory(occupied));
    REQUIRE(Read(occupied) == "not a directory");
}

TEST_CASE("Aftermath raw capture precedes optional JSON decoding", "[aftermath]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("aftermath-test-" + Common::UUID::MakeRandom().FormattedString());
    REQUIRE(std::filesystem::create_directory(root));
    SCOPE_EXIT {
        std::filesystem::remove_all(root);
    };
    const auto path = root / "crash.nv-gpudmp";
    auto json_path = path;
    json_path += ".json";
    const std::string raw{"\0\xff\x7f\x01", 4};
    SECTION("decode failure keeps the raw bytes") {
        bool called{};
        Vulkan::SaveAftermathCrashDump(path, raw, [&]() -> std::optional<std::string> {
            called = true;
            CHECK(Read(path) == raw);
            return std::nullopt;
        });
        REQUIRE(called);
        REQUIRE(Read(path) == raw);
        REQUIRE_FALSE(std::filesystem::exists(json_path));
    }
    SECTION("decode exceptions keep the raw bytes") {
        REQUIRE_THROWS_AS(
            Vulkan::SaveAftermathCrashDump(
                path, raw,
                []() -> std::optional<std::string> { throw std::runtime_error("decode failure"); }),
            std::runtime_error);
        REQUIRE(Read(path) == raw);
    }
    SECTION("successful decoding writes both files") {
        Vulkan::SaveAftermathCrashDump(path, raw, [] { return std::optional<std::string>{"{}"}; });
        REQUIRE(Read(path) == raw);
        REQUIRE(Read(json_path) == "{}");
    }
    SECTION("JSON write failure leaves the raw file intact") {
        REQUIRE(std::filesystem::create_directory(json_path));
        Vulkan::SaveAftermathCrashDump(path, raw, [] { return std::optional<std::string>{"{}"}; });
        REQUIRE(Read(path) == raw);
        REQUIRE(std::filesystem::is_directory(json_path));
    }
    SECTION("raw write failure does not attempt decoding") {
        REQUIRE(std::filesystem::create_directory(path));
        bool called{};
        Vulkan::SaveAftermathCrashDump(path, raw, [&]() {
            called = true;
            return std::optional<std::string>{"{}"};
        });
        REQUIRE_FALSE(called);
        REQUIRE(std::filesystem::is_directory(path));
    }
}
