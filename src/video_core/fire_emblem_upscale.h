// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include "common/common_types.h"
#include "video_core/textures/texture.h"

namespace VideoCommon::FireEmblemUpscale {

constexpr u64 ProgramId = 0x04007EF01F8AE000ULL;
// NativeProgramKey of the captured DLSS final output pass, not its lowered shader hash.
constexpr u64 OutputProgramKey = 0x14b4222bbe23abeeULL;
constexpr u64 PrepassProgramKey = 0x4cbf6eff32525aecULL;
constexpr u64 StarFoxProgramId = 0x0400E88025B1A000ULL;
constexpr u64 StarFoxPrepassProgramKey = 0xd686ca0b9fad9e6dULL;
constexpr u64 StarFoxOutputProgramKey = 0x7d3764ff101bec27ULL;
constexpr u32 PrepassColorOffset = 0x278;
constexpr u32 JitterOffset = 0x1b0;
constexpr u32 OutputColorInputOffset = 0x328;
constexpr u32 OutputColorOffset = 0x348;

struct Passes {
    u64 prepass;
    u64 output;
    bool spatial_only;
};

constexpr std::optional<Passes> CapturedPasses(u64 program_id) {
    if (program_id == ProgramId) {
        return Passes{PrepassProgramKey, OutputProgramKey, false};
    }
    if (program_id == StarFoxProgramId) {
        return Passes{StarFoxPrepassProgramKey, StarFoxOutputProgramKey, true};
    }
    return std::nullopt;
}

struct JitterSample {
    GPUVAddr color{};
    u32 width{}, height{};
    std::array<f32, 2> offset{};
};

class FrameJitter {
public:
    void Capture(GPUVAddr color, u32 width, u32 height, std::array<f32, 2> offset) {
        sample.reset();
        if (color == 0 || width == 0 || height == 0 ||
            !std::isfinite(offset[0]) || !std::isfinite(offset[1]) ||
            std::abs(offset[0]) > 0.5f || std::abs(offset[1]) > 0.5f) {
            return;
        }
        sample = JitterSample{color, width, height, offset};
    }

    // Consume on every output attempt, even if its resources cannot be used.
    std::optional<JitterSample> Take() {
        return std::exchange(sample, std::nullopt);
    }

private:
    std::optional<JitterSample> sample;
};

inline bool UseReplacement(u64 program_id, u64 native_key, const char* option) {
    const auto passes = CapturedPasses(program_id);
    return passes && native_key == passes->output &&
           (option == nullptr || (std::string_view{option} == "fsr" || std::string_view{option} == "dlss"));
}

inline bool PlainImage(const Tegra::Texture::TICEntry& image) {
    using namespace Tegra::Texture;
    return (image.texture_type == TextureType::Texture2D ||
            image.texture_type == TextureType::Texture2DNoMipmap) && image.IsBlockLinear() &&
           image.Depth() == 1 && image.BaseLayer() == 0 &&
           image.max_mip_level == 0 && image.Address() != 0;
}

inline bool StarFoxColor(const Tegra::Texture::TICEntry& image,
                         Tegra::Texture::TextureFormat format,
                         Tegra::Texture::ComponentType component, bool srgb) {
    using namespace Tegra::Texture;
    return PlainImage(image) && image.format == format && (image.srgb_conversion != 0) == srgb &&
           image.r_type == component && image.g_type == component &&
           image.b_type == component && image.a_type == component &&
           image.x_source == SwizzleSource::R && image.y_source == SwizzleSource::G &&
           image.z_source == SwizzleSource::B && image.w_source == SwizzleSource::A &&
           image.res_min_mip_level == 0 && image.res_max_mip_level == 0 &&
           image.msaa_mode == MsaaMode::Msaa1x1 && image.block_width == 0 &&
           image.block_depth == 0 && image.is_sparse == 0;
}

inline bool ValidInput(const Tegra::Texture::TICEntry& input, bool spatial_only = false) {
    using namespace Tegra::Texture;
    if (spatial_only) {
        return StarFoxColor(input, TextureFormat::A8B8G8R8, ComponentType::UNORM, true);
    }
    return input.format == TextureFormat::B10G11R11 && input.IsBlockLinear();
}

inline bool ValidPair(const Tegra::Texture::TICEntry& input,
                      const Tegra::Texture::TICEntry& output, bool spatial_only = false) {
    using namespace Tegra::Texture;
    if (spatial_only) {
        return ValidInput(input, true) &&
               StarFoxColor(output, TextureFormat::R16G16B16A16, ComponentType::FLOAT, false) &&
               input.Address() != output.Address() && input.Width() == output.Width() &&
               input.Height() == output.Height();
    }
    return PlainImage(input) && PlainImage(output) &&
           input.format == TextureFormat::B10G11R11 && output.format == TextureFormat::B10G11R11 &&
           input.Address() != output.Address() && output.Width() > input.Width() &&
           output.Height() > input.Height();
}

// A successful substitute dispatch, not a shader rejection. Never cache it as a failure.
struct DispatchReplaced {};

} // namespace VideoCommon::FireEmblemUpscale
