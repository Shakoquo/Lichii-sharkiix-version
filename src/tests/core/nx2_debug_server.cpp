// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <thread>

#include <boost/asio.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/debugger/nx2_debug_server.h"
#include "core/debugger/nx2_debug_tools.h"
#include "video_core/nx2_debug_control.h"

namespace {

using namespace std::chrono_literals;
using Core::Nx2Debug::Handlers;
using nlohmann::json;

Handlers EchoHandlers() {
    return {
        {"echo", [](const json& params) { return params; }},
        {"fail", [](const json&) -> json { throw std::runtime_error{"broken"}; }},
    };
}

} // Anonymous namespace

TEST_CASE("Debug server endpoints are loopback only", "[core][nx2_debug_server]") {
    std::string error;
    const auto port_only = Core::Nx2Debug::ParseEndpoint("7777", error);
    REQUIRE(port_only);
    REQUIRE(port_only->host == "127.0.0.1");
    REQUIRE(port_only->port == 7777);
    const auto localhost = Core::Nx2Debug::ParseEndpoint("localhost:1234", error);
    REQUIRE(localhost);
    REQUIRE(localhost->host == "127.0.0.1");
    const auto ipv6 = Core::Nx2Debug::ParseEndpoint("[::1]:80", error);
    REQUIRE(ipv6);
    REQUIRE(ipv6->host == "::1");
    REQUIRE_FALSE(Core::Nx2Debug::ParseEndpoint("0.0.0.0:7777", error));
    REQUIRE(error.find("loopback") != std::string::npos);
    REQUIRE_FALSE(Core::Nx2Debug::ParseEndpoint("192.168.1.2:7777", error));
    REQUIRE_FALSE(Core::Nx2Debug::ParseEndpoint("127.0.0.1:", error));
    REQUIRE_FALSE(Core::Nx2Debug::ParseEndpoint("127.0.0.1:70000", error));
    REQUIRE_FALSE(Core::Nx2Debug::ParseEndpoint("12ab", error));
}

TEST_CASE("Debug server requests map to handlers", "[core][nx2_debug_server]") {
    const Handlers handlers = EchoHandlers();
    REQUIRE(json::parse(Core::Nx2Debug::HandleRequest(
                handlers, R"({"id":7,"method":"echo","params":{"a":1}})")) ==
            json{{"id", 7}, {"result", {{"a", 1}}}});
    REQUIRE(json::parse(Core::Nx2Debug::HandleRequest(handlers, R"({"id":"x","method":"echo"})")) ==
            json{{"id", "x"}, {"result", json::object()}});
    REQUIRE(json::parse(Core::Nx2Debug::HandleRequest(handlers, R"({"id":1,"method":"fail"})")) ==
            json{{"id", 1}, {"error", "broken"}});
    const json unknown = json::parse(Core::Nx2Debug::HandleRequest(handlers, R"({"id":2,"method":"nope"})"));
    REQUIRE(unknown["id"] == 2);
    REQUIRE(unknown["error"].get<std::string>().find("unknown method") != std::string::npos);
    const json garbage = json::parse(Core::Nx2Debug::HandleRequest(handlers, "{not json"));
    REQUIRE(garbage["id"].is_null());
    REQUIRE(garbage.contains("error"));
    const json bad_params =
        json::parse(Core::Nx2Debug::HandleRequest(handlers, R"({"id":3,"method":"echo","params":[1]})"));
    REQUIRE(bad_params.contains("error"));
}

TEST_CASE("Debug server shader hashes need all 16 hex digits", "[core][nx2_debug_server]") {
    REQUIRE(Core::Nx2Debug::ParseShaderHash("00000000621c4755") == 0x621c4755);
    REQUIRE(Core::Nx2Debug::ParseShaderHash("3DDFB1D7621C4755") == 0x3ddfb1d7621c4755);
    REQUIRE(Core::Nx2Debug::ParseShaderHash("0x3ddfb1d7621c4755") == 0x3ddfb1d7621c4755);
    for (const char* bad : {"621c4755", "3ddfb1d7", "0x621c4755", "", "0x", "3ddfb1d7621c47551",
                            "3ddfb1d7621c475g", "-ddfb1d7621c4755", " 3ddfb1d7621c475"}) {
        try {
            (void)Core::Nx2Debug::ParseShaderHash(bad);
            FAIL("accepted " << bad);
        } catch (const std::runtime_error& error) {
            REQUIRE(std::string_view{error.what()}.find("16 hex digits") != std::string_view::npos);
        }
    }
}

TEST_CASE("Debug server base64 pads like RFC 4648", "[core][nx2_debug_server]") {
    const auto encode = [](std::string_view text) {
        return Core::Nx2Debug::Base64(
            {reinterpret_cast<const u8*>(text.data()), text.size()});
    };
    REQUIRE(encode("") == "");
    REQUIRE(encode("f") == "Zg==");
    REQUIRE(encode("fo") == "Zm8=");
    REQUIRE(encode("foo") == "Zm9v");
    REQUIRE(encode("foobar") == "Zm9vYmFy");
}

TEST_CASE("Debug server answers over loopback TCP", "[core][nx2_debug_server]") {
    Core::Nx2Debug::Server server{{"127.0.0.1", 0}, EchoHandlers()};
    REQUIRE(server.Port() != 0);
    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket{io};
    socket.connect({boost::asio::ip::make_address("127.0.0.1"), server.Port()});
    const std::string requests =
        "{\"id\":1,\"method\":\"echo\",\"params\":{\"n\":1}}\n\r\n{\"id\":2,\"method\":\"fail\"}\r\n";
    boost::asio::write(socket, boost::asio::buffer(requests));
    std::string input;
    for (int line = 0; line < 2; ++line) {
        const size_t length = boost::asio::read_until(socket, boost::asio::dynamic_buffer(input), '\n');
        const json response = json::parse(input.substr(0, length - 1));
        input.erase(0, length);
        if (line == 0) {
            REQUIRE(response == json{{"id", 1}, {"result", {{"n", 1}}}});
        } else {
            REQUIRE(response == json{{"id", 2}, {"error", "broken"}});
        }
    }
}

TEST_CASE("Debug control hands captures to the rasterizer once", "[core][nx2_debug_server]") {
    VideoCore::Nx2Debug::BeginSession();
    REQUIRE_FALSE(VideoCore::Nx2Debug::HasRequests());
    VideoCore::Nx2Debug::RequestCapture({.id = 5, .directory = "dir", .sampled = true});
    REQUIRE(VideoCore::Nx2Debug::HasRequests());
    const auto request = VideoCore::Nx2Debug::TakeCapture();
    REQUIRE(request);
    REQUIRE(request->id == 5);
    REQUIRE(request->sampled);
    REQUIRE_FALSE(VideoCore::Nx2Debug::HasRequests());
    REQUIRE_FALSE(VideoCore::Nx2Debug::TakeCapture());
    REQUIRE_FALSE(VideoCore::Nx2Debug::WaitCapture(5, 1ms));
    std::thread writer{[] { VideoCore::Nx2Debug::FinishCapture(5, {.error = "disk full"}); }};
    const auto result = VideoCore::Nx2Debug::WaitCapture(5, 10s);
    writer.join();
    REQUIRE(result);
    REQUIRE(result->error == "disk full");

    VideoCore::Nx2Debug::RequestCapture({.id = 6, .directory = "dir", .sampled = false});
    VideoCore::Nx2Debug::CancelCapture(6);
    REQUIRE_FALSE(VideoCore::Nx2Debug::HasRequests());

    // Ending the session wakes a waiter instead of leaving it to its timeout.
    std::thread stopper{[] {
        std::this_thread::sleep_for(20ms);
        VideoCore::Nx2Debug::EndSession();
    }};
    const auto begin = std::chrono::steady_clock::now();
    REQUIRE_FALSE(VideoCore::Nx2Debug::WaitCapture(7, 30s));
    stopper.join();
    REQUIRE(std::chrono::steady_clock::now() - begin < 10s);
}

TEST_CASE("Debug control counts reject generations", "[core][nx2_debug_server]") {
    VideoCore::Nx2Debug::BeginSession();
    REQUIRE_FALSE(VideoCore::Nx2Debug::AnyRejected());
    REQUIRE_FALSE(VideoCore::Nx2Debug::IsRejected(0x1234));
    const std::array<u64, 2> hashes{0x1234, 0x5678};
    const u64 generation = VideoCore::Nx2Debug::SetRejected(hashes, true);
    REQUIRE(VideoCore::Nx2Debug::IsRejected(0x1234));
    REQUIRE(VideoCore::Nx2Debug::SetRejected(hashes, true) == generation);
    REQUIRE_FALSE(VideoCore::Nx2Debug::WaitRejectApplied(generation, 1ms));
    const auto change = VideoCore::Nx2Debug::TakeRejectChange();
    REQUIRE(change);
    REQUIRE_FALSE(VideoCore::Nx2Debug::TakeRejectChange());
    VideoCore::Nx2Debug::NoteRejectApplied(change->generation);
    REQUIRE(VideoCore::Nx2Debug::WaitRejectApplied(generation, 1ms));
    const std::array<u64, 2> both{0x1234, 0x5678};
    VideoCore::Nx2Debug::SetRejected(both, false);
    REQUIRE_FALSE(VideoCore::Nx2Debug::AnyRejected());
    REQUIRE(VideoCore::Nx2Debug::TakeRejectChange()->unrejected);
    VideoCore::Nx2Debug::EndSession();
}
