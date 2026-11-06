// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/logging.h"
#include "common/uuid.h"

namespace Vulkan {

inline std::optional<std::filesystem::path> CreateAftermathDumpDirectory(
    const std::filesystem::path& root) {
    if (!Common::FS::CreateDirs(root)) {
        return std::nullopt;
    }
    const auto directory = root / Common::UUID::MakeRandom().FormattedString();
    std::error_code error;
    if (!std::filesystem::create_directory(directory, error)) {
        return std::nullopt;
    }
    return directory;
}

template <typename Decode>
void SaveAftermathCrashDump(const std::filesystem::path& path, std::string_view raw,
                            Decode decode) {
    if (Common::FS::WriteStringToFile(path, Common::FS::FileType::BinaryFile, raw) != raw.size()) {
        LOG_ERROR(Render_Vulkan, "Failed to write dump file");
        return;
    }
    const auto json = decode();
    if (!json) {
        return;
    }
    auto json_path = path;
    json_path += ".json";
    if (Common::FS::WriteStringToFile(json_path, Common::FS::FileType::TextFile, *json) !=
        json->size()) {
        LOG_ERROR(Render_Vulkan, "Failed to write JSON");
    }
}

} // namespace Vulkan
