// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "shader_recompiler/shader_info.h"

namespace Shader {

constexpr bool IsByteAddressed32BitColorFormat(TexturePixelFormat format) {
    switch (format) {
    case TexturePixelFormat::R32_UINT:
    case TexturePixelFormat::R32_SINT:
    case TexturePixelFormat::R32_FLOAT:
    case TexturePixelFormat::R16G16_UNORM:
    case TexturePixelFormat::R16G16_FLOAT:
    case TexturePixelFormat::R16G16_UINT:
    case TexturePixelFormat::R16G16_SINT:
    case TexturePixelFormat::R16G16_SNORM:
    case TexturePixelFormat::A8B8G8R8_UNORM:
    case TexturePixelFormat::A8B8G8R8_SNORM:
    case TexturePixelFormat::A8B8G8R8_SINT:
    case TexturePixelFormat::A8B8G8R8_UINT:
        return true;
    default:
        return false;
    }
}

} // namespace Shader
