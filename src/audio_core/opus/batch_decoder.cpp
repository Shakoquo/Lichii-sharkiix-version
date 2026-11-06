// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

#include "audio_core/adsp/apps/opus/opus_decode_object.h"
#include "audio_core/adsp/apps/opus/opus_multistream_decode_object.h"
#include "audio_core/opus/batch_decoder.h"
#include "audio_core/opus/opus_error.h"
#include "common/alignment.h"
#include "common/swap.h"

namespace AudioCore::OpusDecoder {
namespace {

using ADSP::OpusDecoder::OpusDecodeObject;
using ADSP::OpusDecoder::OpusMultiStreamDecodeObject;
using namespace Service::Audio;

constexpr size_t StorageAlignment = 64;
constexpr size_t DecoderOffset = Common::AlignUp(sizeof(CodecConfig), StorageAlignment);
static_assert(DecoderOffset == 0x140);
static_assert(StorageAlignment >= alignof(OpusDecodeObject));
static_assert(StorageAlignment >= alignof(OpusMultiStreamDecodeObject));

} // namespace

void HostDecoder::StorageDeleter::operator()(u8* pointer) const {
    ::operator delete(pointer, std::align_val_t{StorageAlignment});
}

HostDecoder::~HostDecoder() {
    if (!storage) {
        return;
    }

    const auto address = reinterpret_cast<u64>(storage.get() + DecoderOffset);
    if (GetConfig().kind == CodecKind::Opus) {
        OpusDecodeObject::Initialize(address, address).Shutdown();
    } else {
        OpusMultiStreamDecodeObject::Initialize(address, address).Shutdown();
    }
}

Result HostDecoder::RequiredBytes(const CodecConfig& config, u32& bytes) {
    bytes = 0;
    R_UNLESS(static_cast<u32>(config.kind) != 0, ResultNotSupported);
    R_UNLESS(config.kind == CodecKind::Opus || config.kind == CodecKind::Multistream,
             ResultLibOpusBadArg);

    constexpr std::array<u32, 5> SampleRates{8000, 12000, 16000, 24000, 48000};
    R_UNLESS(std::ranges::find(SampleRates, config.rate) != SampleRates.end(),
             ResultInvalidOpusSampleRate);

    const std::array<u32, 4> FrameSamples{config.rate / 400, config.rate / 200, config.rate / 100,
                                        config.rate / 50};
    R_UNLESS(std::ranges::any_of(FrameSamples,
                                [&](u32 size) {
                                    return config.frame_samples == size ||
                                           config.frame_samples == size + 1;
                                }),
             ResultLibOpusBadArg);

    u64 decoder_bytes{};
    if (config.kind == CodecKind::Opus) {
        R_UNLESS(config.channels == 1 || config.channels == 2, ResultInvalidOpusChannelCount);
        decoder_bytes = OpusDecodeObject::GetWorkBufferSize(config.channels);
    } else {
        R_UNLESS(config.channels >= 1 && config.channels <= 255, ResultInvalidOpusChannelCount);
        R_UNLESS(config.streams >= 1 && config.streams <= 255 && config.coupled <= config.streams &&
                     config.streams + config.coupled <= config.channels,
                 ResultLibOpusBadArg);
        const auto mapping = std::span{config.mapping}.first(config.channels);
        R_UNLESS(std::ranges::all_of(mapping,
                                    [&](u8 channel) {
                                        return channel == 255 ||
                                               channel < config.streams + config.coupled;
                                    }),
                 ResultLibOpusBadArg);
        decoder_bytes =
            OpusMultiStreamDecodeObject::GetWorkBufferSize(config.streams, config.coupled);
    }

    R_UNLESS(decoder_bytes != 0, ResultLibOpusBadArg);
    const u64 required = DecoderOffset + Common::AlignUp(decoder_bytes, StorageAlignment);
    R_UNLESS(required <= std::numeric_limits<u32>::max(), ResultBufferTooSmall);
    bytes = static_cast<u32>(required);
    R_SUCCEED();
}

Result HostDecoder::Initialize(const CodecConfig& config, u32 reserved_bytes) {
    R_UNLESS(!storage, ResultLibOpusInvalidState);

    u32 required{};
    R_TRY(RequiredBytes(config, required));
    R_UNLESS(reserved_bytes >= required, ResultBufferTooSmall);

    std::unique_ptr<u8, StorageDeleter> allocation{
        static_cast<u8*>(::operator new(required, std::align_val_t{StorageAlignment}, std::nothrow))};
    R_UNLESS(allocation, ResultLibOpusAllocFail);
    std::memset(allocation.get(), 0, required);
    std::memcpy(allocation.get(), &config, sizeof(config));

    const auto address = reinterpret_cast<u64>(allocation.get() + DecoderOffset);
    s32 error{};
    if (config.kind == CodecKind::Opus) {
        error = OpusDecodeObject::Initialize(address, address)
                    .InitializeDecoder(config.rate, config.channels);
    } else {
        auto& stored_config = *reinterpret_cast<CodecConfig*>(allocation.get());
        error = OpusMultiStreamDecodeObject::Initialize(address, address)
                    .InitializeDecoder(config.rate, config.streams, config.channels, config.coupled,
                                       stored_config.mapping.data());
    }
    R_TRY(ResultCodeFromLibOpusErrorCode(error));
    storage = std::move(allocation);
    R_SUCCEED();
}

Result HostDecoder::Decode(std::span<const u8> framed_packet, std::span<s16> output, bool reset,
                            u32& samples_per_channel) {
    samples_per_channel = 0;
    R_UNLESS(storage, ResultLibOpusInvalidState);

    const auto& config = GetConfig();
    const auto address = reinterpret_cast<u64>(storage.get() + DecoderOffset);
    if (reset) {
        const s32 error = config.kind == CodecKind::Opus
                              ? OpusDecodeObject::Initialize(address, address).ResetDecoder()
                              : OpusMultiStreamDecodeObject::Initialize(address, address)
                                    .ResetDecoder();
        R_TRY(ResultCodeFromLibOpusErrorCode(error));
    }

    constexpr size_t HeaderBytes = 8;
    R_UNLESS(framed_packet.size() >= HeaderBytes, ResultInputDataTooSmall);
    u32 payload_size{};
    std::memcpy(&payload_size, framed_packet.data(), sizeof(payload_size));
    payload_size = Common::swap32(payload_size);
    R_UNLESS(payload_size > 0, ResultLibOpusInvalidPacket);
    R_UNLESS(payload_size <= framed_packet.size() - HeaderBytes &&
                 payload_size <= static_cast<u32>(std::numeric_limits<s32>::max()),
             ResultBufferTooSmall);

    const auto frame_capacity =
        std::min<size_t>(config.frame_samples, output.size() / config.channels);
    R_UNLESS(frame_capacity > 0, ResultBufferTooSmall);

    const auto input_address = reinterpret_cast<u64>(framed_packet.data() + HeaderBytes);
    const auto output_address = reinterpret_cast<u64>(output.data());
    u32 samples{};
    const s32 error =
        config.kind == CodecKind::Opus
            ? OpusDecodeObject::Initialize(address, address)
                  .Decode(samples, output_address, frame_capacity, input_address, payload_size)
            : OpusMultiStreamDecodeObject::Initialize(address, address)
                  .Decode(samples, output_address, frame_capacity, input_address, payload_size);
    R_TRY(ResultCodeFromLibOpusErrorCode(error));
    samples_per_channel = samples;
    R_SUCCEED();
}

const CodecConfig& HostDecoder::GetConfig() const {
    ASSERT(storage);
    return *reinterpret_cast<const CodecConfig*>(storage.get());
}

} // namespace AudioCore::OpusDecoder
