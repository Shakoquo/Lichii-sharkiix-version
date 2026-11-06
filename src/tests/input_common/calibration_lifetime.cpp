// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <memory>
#include <utility>

#include <boost/asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include "input_common/drivers/udp_client.h"

TEST_CASE("Calibration cancellation releases callbacks before owner destruction returns",
          "[input][calibration]") {
    boost::asio::io_context context;
    boost::asio::ip::udp::socket server(
        context, {boost::asio::ip::address_v4::loopback(), 0});
    using Job = InputCommon::CemuhookUDP::CalibrationConfigurationJob;

    for (int i = 0; i < 32; ++i) {
        auto capture = std::make_shared<int>(i);
        std::weak_ptr<int> lifetime = capture;
        {
            Job job("127.0.0.1", server.local_endpoint().port(), [capture](Job::Status) {},
                    [capture](u16, u16, u16, u16) {});
            capture.reset();
            job.Stop();
            job.Stop();
        }
        REQUIRE(lifetime.expired());
    }
}
