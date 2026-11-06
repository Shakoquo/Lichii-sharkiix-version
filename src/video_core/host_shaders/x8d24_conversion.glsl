// SPDX-License-Identifier: GPL-3.0-or-later

float DecodeX8D24(float red, float green, float blue) {
    uint depth = uint(round(red * 255.0f)) | (uint(round(green * 255.0f)) << 8) |
                 (uint(round(blue * 255.0f)) << 16);
    uint scaled = floatBitsToUint(float(depth) * (1.0f / 16777216.0f));
    return uintBitsToFloat(scaled + uint(depth != 0u));
}

uint EncodeX8D24(float depth) {
    return uint(round(depth * 16777215.0f));
}
