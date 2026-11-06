// SPDX-License-Identifier: GPL-3.0-or-later

#version 450
#extension GL_GOOGLE_include_directive : require

#include "x8d24_conversion.glsl"

layout(binding = 0) uniform sampler2D depth_texture;
layout(location = 0) out vec4 color;

void main() {
    float depth = texelFetch(depth_texture, ivec2(gl_FragCoord.xy), 0).r;
    color = unpackUnorm4x8(EncodeX8D24(depth));
}
