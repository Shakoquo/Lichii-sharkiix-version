// SPDX-License-Identifier: GPL-3.0-or-later

#version 450
#extension GL_GOOGLE_include_directive : require

#include "x8d24_conversion.glsl"

layout(binding = 0) uniform sampler2D color_texture;

void main() {
    vec4 color = texelFetch(color_texture, ivec2(gl_FragCoord.xy), 0);
    gl_FragDepth = DecodeX8D24(color.r, color.g, color.b);
}
