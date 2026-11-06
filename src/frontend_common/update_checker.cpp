// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <charconv>
#include "common/logging.h"
#include "common/scm_rev.h"
#include "frontend_common/update_checker.h"

namespace UpdateChecker {

namespace {

std::optional<u64> ParseBuildNumber(std::string_view tag) {
    constexpr std::string_view prefix = "build-";
    if (!tag.starts_with(prefix)) {
        return std::nullopt;
    }
    tag.remove_prefix(prefix.size());
    u64 number{};
    const auto [end, error] = std::from_chars(tag.data(), tag.data() + tag.size(), number);
    if (error != std::errc{} || end != tag.data() + tag.size() || number == 0) {
        return std::nullopt;
    }
    return number;
}

} // namespace

std::optional<Common::Net::Release> SelectUpdate(std::span<const Common::Net::Release> releases,
                                                 std::string_view build_tag,
                                                 std::string_view platform) {
    auto newest = ParseBuildNumber(build_tag);
    if (!newest) {
        return std::nullopt;
    }

    const Common::Net::Release* selected = nullptr;
    for (const auto& release : releases) {
        const auto number = ParseBuildNumber(release.tag);
        if (number && *number > *newest && !release.GetPlatformAssets(platform).empty()) {
            newest = number;
            selected = &release;
        }
    }
    return selected ? std::make_optional(*selected) : std::nullopt;
}

std::optional<Common::Net::Release> GetUpdate() {
    if (!ParseBuildNumber(Common::g_build_release_tag) ||
        std::string_view{Common::g_build_release_platform}.empty()) {
        return std::nullopt;
    }

    auto update = SelectUpdate(Common::Net::GetReleases(), Common::g_build_release_tag,
                               Common::g_build_release_platform);
    if (update) {
        LOG_INFO(Frontend, "Received update {}", update->title);
    }
    return update;
}

} // namespace UpdateChecker
