// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "hid_core/hid_types.h"

namespace Service::HID {

// Keep defaults outside the trivial IPC structure. Zero ranges invalidate guest sensor fusion.
constexpr Core::HID::SixAxisSensorIcInformation DefaultSixAxisSensorIcInformation() {
    return {
        .angular_rate = 2000.0f,
        .unknown_gyro_data1 = {-10.0f, -10.0f, -10.0f, 10.0f, 10.0f, 10.0f},
        .unknown_gyro_data2 = {0.95f, -0.003f, -0.003f, -0.003f, 0.95f, -0.003f,
                              -0.003f, -0.003f, 0.95f},
        .unknown_gyro_data3 = {1.05f, 0.003f, 0.003f, 0.003f, 1.05f, 0.003f,
                              0.003f, 0.003f, 1.05f},
        .acceleration_range = 8.0f,
        .unknown_accel_data1 = {-0.0612f, -0.0612f, -0.0612f, 0.0612f, 0.0612f, 0.0612f},
        .unknown_accel_data2 = {0.95f, -0.003f, -0.003f, -0.003f, 0.95f, -0.003f,
                               -0.003f, -0.003f, 0.95f},
        .unknown_accel_data3 = {1.05f, 0.003f, 0.003f, 0.003f, 1.05f, 0.003f,
                               0.003f, 0.003f, 1.05f},
    };
}

} // namespace Service::HID
