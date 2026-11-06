// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <span>
#include <vector>

#include "video_core/surface.h"
#include "video_core/texture_cache/types.h"

namespace VideoCommon {

inline VideoCore::Surface::PixelFormat GetAliasConversionFormat(
    VideoCore::Surface::PixelFormat format, VideoCore::Surface::PixelFormat other_format) {
    using namespace VideoCore::Surface;
    if (other_format == PixelFormat::X8_D24_UNORM &&
        GetFormatType(format) == SurfaceType::ColorTexture && BytesPerBlock(format) == 4) {
        return PixelFormat::A8B8G8R8_UNORM;
    }
    return format;
}

inline std::vector<ImageCopy> MakeImageCopyLayers(std::span<const ImageCopy> copies) {
    std::vector<ImageCopy> layer_copies;
    layer_copies.reserve(copies.size());
    for (const ImageCopy& copy : copies) {
        const s32 num_layers = copy.dst_subresource.num_layers;
        if (num_layers <= 1 || copy.src_subresource.num_layers != num_layers) {
            layer_copies.push_back(copy);
            continue;
        }
        for (s32 layer = 0; layer < num_layers; ++layer) {
            ImageCopy single = copy;
            single.dst_subresource.base_layer += layer;
            single.src_subresource.base_layer += layer;
            single.dst_subresource.num_layers = 1;
            single.src_subresource.num_layers = 1;
            layer_copies.push_back(single);
        }
    }
    return layer_copies;
}

} // namespace VideoCommon
