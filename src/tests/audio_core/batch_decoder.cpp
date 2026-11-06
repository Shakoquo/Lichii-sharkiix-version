// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <opus.h>
#include <opus_multistream.h>

#include "audio_core/adsp/apps/opus/opus_decode_object.h"
#include "audio_core/adsp/apps/opus/opus_multistream_decode_object.h"
#include "audio_core/opus/batch_decoder.h"
#include "audio_core/opus/opus_error.h"
#include "common/alignment.h"

namespace {

using AudioCore::OpusDecoder::CodecConfig;
using AudioCore::OpusDecoder::CodecKind;
using AudioCore::OpusDecoder::HostDecoder;
using namespace Service::Audio;

CodecConfig MakeConfig(u32 channels, bool multistream = false) {
    CodecConfig config{};
    config.kind = multistream ? CodecKind::Multistream : CodecKind::Opus;
    config.rate = 48000;
    config.channels = channels;
    config.frame_samples = 960;
    if (multistream) {
        config.streams = 2;
        config.coupled = 1;
        config.mapping[0] = 0;
        config.mapping[1] = 1;
        config.mapping[2] = 2;
    }
    return config;
}

std::vector<u8> Frame(std::span<const u8> payload) {
    const auto size = static_cast<u32>(payload.size());
    std::vector<u8> packet{static_cast<u8>(size >> 24), static_cast<u8>(size >> 16),
                           static_cast<u8>(size >> 8), static_cast<u8>(size),
                           0x12, 0x34, 0x56, 0x78};
    packet.insert(packet.end(), payload.begin(), payload.end());
    return packet;
}

class CodecFixture {
public:
    explicit CodecFixture(CodecConfig config_) : config{config_} {
        int error{};
        if (config.kind == CodecKind::Opus) {
            encoder.reset(opus_encoder_create(static_cast<opus_int32>(config.rate),
                                              static_cast<int>(config.channels),
                                              OPUS_APPLICATION_AUDIO, &error));
            REQUIRE(error == OPUS_OK);
            REQUIRE(encoder);
            decoder.reset(opus_decoder_create(static_cast<opus_int32>(config.rate),
                                              static_cast<int>(config.channels), &error));
            REQUIRE(error == OPUS_OK);
            REQUIRE(decoder);
        } else {
            ms_encoder.reset(opus_multistream_encoder_create(
                static_cast<opus_int32>(config.rate), static_cast<int>(config.channels),
                static_cast<int>(config.streams), static_cast<int>(config.coupled),
                config.mapping.data(), OPUS_APPLICATION_AUDIO, &error));
            REQUIRE(error == OPUS_OK);
            REQUIRE(ms_encoder);
            ms_decoder.reset(opus_multistream_decoder_create(
                static_cast<opus_int32>(config.rate), static_cast<int>(config.channels),
                static_cast<int>(config.streams), static_cast<int>(config.coupled),
                config.mapping.data(), &error));
            REQUIRE(error == OPUS_OK);
            REQUIRE(ms_decoder);
        }
    }

    std::vector<u8> Encode(u32 phase) {
        std::vector<s16> input(config.frame_samples * config.channels);
        for (u32 sample = 0; sample < config.frame_samples; ++sample) {
            for (u32 channel = 0; channel < config.channels; ++channel) {
                const auto value = (sample * (channel + 3) * 97 + phase * 7919) % 24000;
                input[sample * config.channels + channel] =
                    static_cast<s16>(static_cast<s32>(value) - 12000);
            }
        }

        std::vector<u8> payload(4096);
        const auto size = encoder
                              ? opus_encode(encoder.get(), input.data(),
                                            static_cast<int>(config.frame_samples), payload.data(),
                                            static_cast<opus_int32>(payload.size()))
                              : opus_multistream_encode(ms_encoder.get(), input.data(),
                                                        static_cast<int>(config.frame_samples),
                                                        payload.data(),
                                                        static_cast<opus_int32>(payload.size()));
        REQUIRE(size > 0);
        payload.resize(static_cast<size_t>(size));
        return Frame(payload);
    }

    std::vector<s16> Reference(std::span<const u8> packet, bool reset = false) {
        if (reset) {
            const auto result = decoder ? opus_decoder_ctl(decoder.get(), OPUS_RESET_STATE)
                                        : opus_multistream_decoder_ctl(ms_decoder.get(),
                                                                       OPUS_RESET_STATE);
            REQUIRE(result == OPUS_OK);
        }
        std::vector<s16> output(config.frame_samples * config.channels);
        const auto payload = packet.subspan(8);
        const auto samples =
            decoder ? opus_decode(decoder.get(), payload.data(),
                                  static_cast<opus_int32>(payload.size()), output.data(),
                                  static_cast<int>(config.frame_samples), 0)
                    : opus_multistream_decode(ms_decoder.get(), payload.data(),
                                              static_cast<opus_int32>(payload.size()), output.data(),
                                              static_cast<int>(config.frame_samples), 0);
        REQUIRE(samples > 0);
        output.resize(static_cast<size_t>(samples) * config.channels);
        REQUIRE(std::ranges::any_of(output, [](s16 sample) { return sample != 0; }));
        return output;
    }

private:
    CodecConfig config;
    std::unique_ptr<::OpusEncoder, decltype(&opus_encoder_destroy)> encoder{nullptr,
                                                                          opus_encoder_destroy};
    std::unique_ptr<::OpusDecoder, decltype(&opus_decoder_destroy)> decoder{nullptr,
                                                                          opus_decoder_destroy};
    std::unique_ptr<::OpusMSEncoder, decltype(&opus_multistream_encoder_destroy)> ms_encoder{
        nullptr, opus_multistream_encoder_destroy};
    std::unique_ptr<::OpusMSDecoder, decltype(&opus_multistream_decoder_destroy)> ms_decoder{
        nullptr, opus_multistream_decoder_destroy};
};

void Initialize(HostDecoder& decoder, const CodecConfig& config) {
    u32 bytes{};
    REQUIRE(HostDecoder::RequiredBytes(config, bytes).IsSuccess());
    REQUIRE(decoder.Initialize(config, bytes).IsSuccess());
}

void CheckDecode(HostDecoder& decoder, std::span<const u8> packet,
                 const std::vector<s16>& expected, bool reset = false) {
    constexpr s16 Canary = 0x5A5A;
    const auto& config = decoder.GetConfig();
    std::vector<s16> output(config.frame_samples * config.channels + 2, Canary);
    auto pcm = std::span{output}.subspan(1, output.size() - 2);
    u32 samples{std::numeric_limits<u32>::max()};
    REQUIRE(decoder.Decode(packet, pcm, reset, samples).IsSuccess());
    REQUIRE(samples * config.channels == expected.size());
    CHECK(std::ranges::equal(pcm.first(expected.size()), expected));
    CHECK(std::ranges::all_of(pcm.subspan(expected.size()),
                              [](s16 sample) { return sample == Canary; }));
    CHECK(output.front() == Canary);
    CHECK(output.back() == Canary);
}

} // namespace

TEST_CASE("Batch Opus storage is derived from its real config and codec state",
          "[audio_core][hwacod][batch_decoder]") {
    STATIC_REQUIRE(sizeof(CodecConfig) == 0x120);
    STATIC_REQUIRE(offsetof(CodecConfig, mapping) == 24);
    STATIC_REQUIRE(offsetof(CodecConfig, padding) == 279);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<HostDecoder>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<HostDecoder>);

    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        u32 bytes{};
        REQUIRE(HostDecoder::RequiredBytes(config, bytes).IsSuccess());
        const size_t state_bytes =
            config.kind == CodecKind::Opus
                ? sizeof(AudioCore::ADSP::OpusDecoder::OpusDecodeObject) +
                      static_cast<size_t>(opus_decoder_get_size(static_cast<int>(config.channels)))
                : sizeof(AudioCore::ADSP::OpusDecoder::OpusMultiStreamDecodeObject) +
                      static_cast<size_t>(opus_multistream_decoder_get_size(
                          static_cast<int>(config.streams), static_cast<int>(config.coupled)));
        CHECK(bytes == Common::AlignUp(sizeof(CodecConfig), 64) + Common::AlignUp(state_bytes, 64));
        HostDecoder decoder;
        CHECK(decoder.Initialize(config, bytes - 1) == ResultBufferTooSmall);
        REQUIRE(decoder.Initialize(config, bytes).IsSuccess());
        CHECK(decoder.GetConfig().channels == config.channels);
        CHECK(decoder.GetConfig().mapping == config.mapping);
        CHECK(reinterpret_cast<uintptr_t>(&decoder.GetConfig()) % 64 == 0);
        CHECK(decoder.Initialize(config, bytes) == ResultLibOpusInvalidState);
    }
}

TEST_CASE("Batch Opus validates codec parameters and duration-derived frame limits",
          "[audio_core][hwacod][batch_decoder]") {
    for (const u32 rate : {8000u, 12000u, 16000u, 24000u, 48000u}) {
        for (const u32 divisor : {400u, 200u, 100u, 50u}) {
            auto config = MakeConfig(1);
            config.rate = rate;
            for (const u32 increment : {0u, 1u}) {
                config.frame_samples = rate / divisor + increment;
                u32 bytes{};
                CHECK(HostDecoder::RequiredBytes(config, bytes).IsSuccess());
                CHECK(bytes > 0);
            }
        }
    }

    const auto invalid = [](const CodecConfig& config) {
        u32 bytes = 123;
        CHECK(HostDecoder::RequiredBytes(config, bytes).IsError());
        CHECK(bytes == 0);
        HostDecoder decoder;
        CHECK(decoder.Initialize(config, std::numeric_limits<u32>::max()).IsError());
    };
    auto config = MakeConfig(1);
    config.kind = static_cast<CodecKind>(0);
    invalid(config);
    config.kind = static_cast<CodecKind>(3);
    invalid(config);
    config = MakeConfig(1);
    config.rate = 44100;
    invalid(config);
    for (const u32 channels : {0u, 3u, std::numeric_limits<u32>::max()}) {
        config = MakeConfig(channels);
        invalid(config);
    }
    for (const u32 samples : {0u, 119u, 122u, 962u, 1920u,
                              std::numeric_limits<u32>::max()}) {
        config = MakeConfig(1);
        config.frame_samples = samples;
        invalid(config);
    }
    for (const u32 streams : {0u, 256u, std::numeric_limits<u32>::max()}) {
        config = MakeConfig(3, true);
        config.streams = streams;
        invalid(config);
    }
    config = MakeConfig(3, true);
    config.coupled = 3;
    invalid(config);
    config.coupled = 2;
    invalid(config);
    config = MakeConfig(3, true);
    config.mapping[2] = 3;
    invalid(config);
    config.mapping[2] = 255;
    u32 bytes{};
    CHECK(HostDecoder::RequiredBytes(config, bytes).IsSuccess());
    config.channels = 256;
    invalid(config);
    config.channels = 0;
    invalid(config);

    config = MakeConfig(255, true);
    config.streams = 255;
    config.coupled = 0;
    for (u32 channel = 0; channel < config.channels; ++channel) {
        config.mapping[channel] = static_cast<u8>(channel);
    }
    CHECK(HostDecoder::RequiredBytes(config, bytes).IsSuccess());
}

TEST_CASE("Batch Opus matches real mono stereo and multistream decoder history",
          "[audio_core][hwacod][batch_decoder]") {
    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        CodecFixture fixture{config};
        HostDecoder decoder;
        Initialize(decoder, config);
        const auto first = fixture.Encode(1);
        const auto second = fixture.Encode(2);
        CheckDecode(decoder, first, fixture.Reference(first));
        CheckDecode(decoder, second, fixture.Reference(second));
        CodecFixture fresh_reference{config};
        const auto fresh_output = fresh_reference.Reference(first);
        CHECK(fixture.Reference(first, true) == fresh_output);
        CheckDecode(decoder, first, fresh_output, true);
    }
}

TEST_CASE("Interleaved batch Opus instances keep independent codec state",
          "[audio_core][hwacod][batch_decoder]") {
    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        CodecFixture fixture_a{config};
        CodecFixture fixture_b{config};
        HostDecoder decoder_a;
        HostDecoder decoder_b;
        Initialize(decoder_a, config);
        Initialize(decoder_b, config);
        const auto a_first = fixture_a.Encode(1);
        const auto b_first = fixture_b.Encode(9);
        const auto a_second = fixture_a.Encode(2);
        const auto b_second = fixture_b.Encode(10);
        CheckDecode(decoder_a, a_first, fixture_a.Reference(a_first));
        CheckDecode(decoder_b, b_first, fixture_b.Reference(b_first));
        CheckDecode(decoder_a, a_second, fixture_a.Reference(a_second));
        CheckDecode(decoder_b, b_second, fixture_b.Reference(b_second));
    }
}

TEST_CASE("Batch Opus applies reset before rejecting malformed packet framing",
          "[audio_core][hwacod][batch_decoder]") {
    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        CodecFixture fixture{config};
        HostDecoder decoder;
        Initialize(decoder, config);
        const auto packet = fixture.Encode(3);
        CheckDecode(decoder, packet, fixture.Reference(packet));

        std::vector<s16> output(config.frame_samples * config.channels, 1234);
        u32 samples = 99;
        CHECK(decoder.Decode(std::span{packet}.first(7), output, true, samples) ==
              ResultInputDataTooSmall);
        CHECK(samples == 0);
        CHECK(std::ranges::all_of(output, [](s16 sample) { return sample == 1234; }));
        CheckDecode(decoder, packet, fixture.Reference(packet, true));
    }
}

TEST_CASE("Batch Opus rejects framing and codec errors without crossing output bounds",
          "[audio_core][hwacod][batch_decoder]") {
    constexpr s16 Canary = 0x5A5A;
    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        CodecFixture fixture{config};
        HostDecoder decoder;
        Initialize(decoder, config);
        const auto packet = fixture.Encode(4);
        auto truncated = packet;
        truncated.pop_back();
        auto oversized = packet;
        std::fill_n(oversized.begin(), 4, 0xFF);
        const std::array<u8, 2> invalid_opus{0x03, 0x00};
        const std::array<std::vector<u8>, 5> bad_packets{
            std::vector<u8>(7), Frame({}), truncated, oversized, Frame(invalid_opus)};

        for (const auto& bad_packet : bad_packets) {
            std::vector<s16> output(config.frame_samples * config.channels + 2, Canary);
            u32 samples = 99;
            CHECK(decoder.Decode(bad_packet, std::span{output}.subspan(1, output.size() - 2),
                                 false, samples)
                      .IsError());
            CHECK(samples == 0);
            CHECK(output.front() == Canary);
            CHECK(output.back() == Canary);
        }

        CheckDecode(decoder, packet, fixture.Reference(packet));

        std::array<s16, 3> short_output{Canary, Canary, Canary};
        u32 samples = 99;
        CHECK(decoder.Decode(packet, std::span{short_output}.subspan(1, 1), true, samples) ==
              ResultBufferTooSmall);
        CHECK(samples == 0);
        CHECK(short_output.front() == Canary);
        CHECK(short_output.back() == Canary);
        CHECK(decoder.Decode(packet, {}, false, samples) == ResultBufferTooSmall);
        CHECK(samples == 0);
        CheckDecode(decoder, packet, fixture.Reference(packet, true), true);
    }

    HostDecoder uninitialized;
    u32 samples = 99;
    CHECK(uninitialized.Decode({}, {}, true, samples) == ResultLibOpusInvalidState);
    CHECK(samples == 0);
}

TEST_CASE("Batch Opus enforces configured frame limits even with oversized output",
          "[audio_core][hwacod][batch_decoder]") {
    for (const auto& config : {MakeConfig(1), MakeConfig(2), MakeConfig(3, true)}) {
        CAPTURE(config.channels);
        CodecFixture fixture{config};
        const auto packet = fixture.Encode(5);
        auto short_config = config;
        short_config.frame_samples = config.rate / 400;
        HostDecoder decoder;
        Initialize(decoder, short_config);
        std::vector<s16> output(config.frame_samples * config.channels + 2, 1234);
        u32 samples = 99;
        CHECK(decoder.Decode(packet, std::span{output}.subspan(1, output.size() - 2), false,
                             samples) == ResultBufferTooSmall);
        CHECK(samples == 0);
        CHECK(output.front() == 1234);
        CHECK(output.back() == 1234);

        auto fractional_config = config;
        ++fractional_config.frame_samples;
        HostDecoder fractional_decoder;
        Initialize(fractional_decoder, fractional_config);
        CheckDecode(fractional_decoder, packet, fixture.Reference(packet));
    }
}

TEST_CASE("Shared libopus result mapping preserves the legacy error codes",
          "[audio_core][hwacod][batch_decoder]") {
    using AudioCore::OpusDecoder::ResultCodeFromLibOpusErrorCode;
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_OK) == ResultSuccess);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_ALLOC_FAIL) == ResultLibOpusAllocFail);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_INVALID_STATE) == ResultLibOpusInvalidState);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_UNIMPLEMENTED) == ResultLibOpusUnimplemented);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_INVALID_PACKET) == ResultLibOpusInvalidPacket);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_INTERNAL_ERROR) == ResultLibOpusInternalError);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_BUFFER_TOO_SMALL) == ResultBufferTooSmall);
    CHECK(ResultCodeFromLibOpusErrorCode(OPUS_BAD_ARG) == ResultLibOpusBadArg);
}
