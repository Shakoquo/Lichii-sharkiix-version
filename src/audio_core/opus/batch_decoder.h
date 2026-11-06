// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <memory>
#include <span>

#include "common/common_types.h"
#include "core/hle/result.h"

namespace AudioCore::OpusDecoder {

enum class CodecKind : u32 {
    Opus = 1,
    Multistream = 2,
};

struct CodecConfig {
    CodecKind kind{};
    u32 rate{};
    u32 channels{};
    u32 streams{};
    u32 coupled{};
    u32 frame_samples{};
    std::array<u8, 255> mapping{};
    std::array<u8, 9> padding{};
};
static_assert(sizeof(CodecConfig) == 0x120);

class HostDecoder final {
public:
    HostDecoder() = default;
    ~HostDecoder();

    HostDecoder(const HostDecoder&) = delete;
    HostDecoder& operator=(const HostDecoder&) = delete;
    HostDecoder(HostDecoder&&) = delete;
    HostDecoder& operator=(HostDecoder&&) = delete;

    static Result RequiredBytes(const CodecConfig& config, u32& bytes);
    Result Initialize(const CodecConfig& config, u32 reserved_bytes);
    Result Decode(std::span<const u8> framed_packet, std::span<s16> output, bool reset,
                  u32& samples_per_channel);
    const CodecConfig& GetConfig() const;

private:
    struct StorageDeleter {
        void operator()(u8* pointer) const;
    };

    std::unique_ptr<u8, StorageDeleter> storage;
};

} // namespace AudioCore::OpusDecoder
