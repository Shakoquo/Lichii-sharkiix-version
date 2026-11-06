// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "common/common_types.h"

namespace Core {
class System;
}

namespace Core::Nx2Debug {

class Server;
struct Endpoint;
struct ToolState;

/// The debug server of one boot (docs/nx2/debug-mcp.md): started when a game has loaded, stopped
/// first when it shuts down, so the tools never see a half-built or half-destroyed system.
class Service {
public:
    explicit Service(System& system, std::string title, u64 program_id, const Endpoint& endpoint,
                     std::string dump_directory);
    ~Service();

    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;

private:
    std::shared_ptr<ToolState> state;
    std::unique_ptr<Server> server;
};

/// A reject_shaders hash: exactly 16 hex digits, optionally after 0x. Throws std::runtime_error.
[[nodiscard]] u64 ParseShaderHash(std::string_view text);

/// The service when EDEN_NX2_DEBUG_SERVER is set and it can listen, otherwise null.
[[nodiscard]] std::unique_ptr<Service> MaybeStartService(System& system, std::string title,
                                                         u64 program_id);

} // namespace Core::Nx2Debug
