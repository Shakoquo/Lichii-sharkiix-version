// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <tuple>
#include <catch2/catch_test_macros.hpp>

#include "core/core.h"
#include "core/hle/kernel/k_readable_event.h"
#include "core/hle/service/audio/errors.h"
#include "core/hle/service/audio/hardware_audio_codec.h"
#include "core/hle/service/audio/hardware_opus_decoder_manager.h"
#include "core/hle/service/cmif_serialization.h"
#include "core/memory.h"
#include "tests/core/system_test_context.h"

namespace {
using namespace Service;
using namespace Service::Audio;
using namespace Service::Audio::Hwacod;

template <typename R, typename C, typename... A>
auto ArgumentsOf(R (C::*)(A...)) -> std::tuple<A...>;

std::array<u8, 0x120> Parameters(u64 kind = 1, s64 duration = 20000000) {
    std::array<u8, 0x120> bytes{};
    const s32 rate = 48000;
    const s32 channels = 2;
    std::memcpy(bytes.data(), &kind, sizeof(kind));
    std::memcpy(bytes.data() + 8, &rate, sizeof(rate));
    std::memcpy(bytes.data() + 12, &channels, sizeof(channels));
    std::memcpy(bytes.data() + 16, &duration, sizeof(duration));
    return bytes;
}

SharedMemoryWire Layout() {
    SharedMemoryWire l{};
    l.version = 1;
    l.manager_size = 128;
    l.instances_offset = 128;
    l.instance_stride = 1024;
    l.instance_count = 2;
    l.instances_size = 2048;
    l.transfer_queue_offset = 4096;
    l.transfer_queue_size = 4096;
    l.transfer_size = 8192;
    l.io_count = 2;
    l.input_stride = 0x600;
    l.output_stride = 3840;
    l.io_size = 2 * (l.input_stride + l.output_stride);
    l.completion_offset = 12288;
    l.completion_size = 4096;
    l.process_size = 16384;
    return l;
}

std::array<u32, IPC::COMMAND_BUFFER_LENGTH> Request(RequestShape shape, bool domain,
                                                  u32 buffer_bytes = 0) {
    std::array<u32, IPC::COMMAND_BUFFER_LENGTH> words{};
    IPC::CommandHeader header{};
    header.type.Assign(IPC::CommandType::RequestWithContext);
    u32 offset = 2;
    if (shape.copy_handles) {
        header.enable_handle_descriptor.Assign(1);
        IPC::HandleDescriptorHeader handles{};
        handles.num_handles_to_copy.Assign(shape.copy_handles);
        words[offset++] = handles.raw_high;
        offset += shape.copy_handles;
    }
    if (shape.buffer != InputBuffer::None) {
        header.num_buf_x_descriptors.Assign(1);
        IPC::BufferDescriptorX x{};
        x.size.Assign(buffer_bytes);
        std::memcpy(words.data() + offset, &x, sizeof(x));
        offset += 2;
    }
    const u32 raw_start = offset;
    offset = (offset + 3) & ~3u;
    if (domain) {
        IPC::DomainMessageHeader h{};
        h.command.Assign(IPC::DomainMessageHeader::CommandType::SendMessage);
        h.size.Assign(16 + shape.scalar_bytes);
        h.object_id = 1;
        std::memcpy(words.data() + offset, &h, sizeof(h));
        offset += 4;
    }
    words[offset] = 0x49434653;
    words[offset + 1] = 1;
    words[offset + 2] = shape.copy_handles ? 10 : 100;
    offset += 4 + (shape.scalar_bytes + 3) / 4;
    header.data_size.Assign(offset - raw_start);
    std::memcpy(words.data(), &header, sizeof(header));
    return words;
}
}

TEST_CASE("hwacod parameter parsing preserves exact wire lengths and duration rounding", "[hwacod]") {
    AudioCore::OpusDecoder::CodecConfig config{};
    auto bytes = Parameters();
    REQUIRE(ParseParameters(bytes, config).IsSuccess());
    REQUIRE(config.channels == 2);
    REQUIRE(config.frame_samples == 960);
    REQUIRE(ParseParameters(std::span{bytes}.first(0x11f), config).IsFailure());
    std::array<u8, 0x121> oversized{};
    std::copy(bytes.begin(), bytes.end(), oversized.begin());
    REQUIRE(ParseParameters(oversized, config).IsFailure());
    REQUIRE(ParseParameters(Parameters(0), config) == ResultNotSupported);
    REQUIRE(ParseParameters(Parameters(3), config).IsFailure());
    REQUIRE(ParseParameters(Parameters(1, 20000999), config).IsSuccess());
    REQUIRE(config.frame_samples == 961);
    REQUIRE(ParseParameters(Parameters(1, 20001000), config).IsFailure());
    REQUIRE(ParseParameters(Parameters(1, -20000000), config).IsFailure());
    bytes.back() = 1;
    REQUIRE(ParseParameters(bytes, config).IsSuccess());
}

TEST_CASE("hwacod layout accepts padded reservations and rejects overlap and overflow", "[hwacod]") {
    auto l = Layout();
    const auto original = l;
    REQUIRE(ValidateLayout(l, l.transfer_size, 0x100000, l.process_size, 7).IsSuccess());
    SECTION("opaque property bits remain accepted") {
        REQUIRE(ValidateLayout(l, l.transfer_size, 0x100000, l.process_size, 0x8000000000000001).IsSuccess());
    }
    SECTION("manager reservation is real") { l.manager_size = 95; }
    SECTION("instance reservation multiplication cannot wrap") { l.instance_stride = 0xffffffc0; }
    SECTION("IO reservation multiplication cannot wrap") { l.output_stride = 0xffffffc0; }
    SECTION("transfer regions cannot overlap") { l.instances_offset = 64; }
    SECTION("completion cannot overlap IO") { l.completion_offset = 4096; }
    SECTION("queue reservation includes the spare ring slot") { l.completion_size = 64; }
    SECTION("input stride must fit the SDK input reservation") { l.input_stride = 0x5c0; }
    SECTION("counts cannot escape u16 identifiers") { l.instance_count = 65537; }
    SECTION("zero IO is invalid") { l.io_count = 0; }
    SECTION("reserved bytes must be zero") { l.reserved.back() = 1; }
    if (std::memcmp(&l, &original, sizeof(l)) != 0) {
        REQUIRE(ValidateLayout(l, l.transfer_size, 0x100000, l.process_size, 7).IsFailure());
    }
    l = Layout();
    REQUIRE(ValidateLayout(l, l.transfer_size, std::numeric_limits<u64>::max() - 4095,
                           l.process_size, 7).IsFailure());
    REQUIRE(ValidateLayout(l, l.transfer_size, 0x100000, l.process_size, 8).IsFailure());
}

TEST_CASE("hwacod authoritative manager storage owns leases and ring wrap", "[hwacod]") {
    BatchState batch;
    REQUIRE(batch.Initialize(2, 9).IsSuccess());
    u32 required{};
    REQUIRE(GetManagerSize(2, required).IsSuccess());
    REQUIRE(required == 128);
    REQUIRE(batch.StorageBytes() == required);
    const std::array<u8, 2> live{1, 1};
    const std::array<Task, 2> tasks{{{9, 0, 0, 65535, 12, 0, 0, 0},
                                    {9, 1, 1, 0, 12, 1, 1, 0}}};
    REQUIRE(batch.ValidateTasks(tasks, live, 0x600).IsSuccess());
    REQUIRE(batch.State().outstanding == 0);
    for (const auto& task : tasks) {
        batch.Commit(task);
    }
    REQUIRE(batch.State().write_index == 2);
    REQUIRE(batch.HasOutstanding(0));
    REQUIRE(batch.HasOutstanding(1));
    REQUIRE(batch.ValidateTasks(tasks, live, 0x600) == ResultBufferCountReached);
    REQUIRE(batch.Reclaim(1, 2).IsSuccess());
    REQUIRE_FALSE(batch.HasOutstanding(0));
    REQUIRE(batch.HasOutstanding(1));
    REQUIRE(batch.ValidateTasks(std::span{tasks}.first(1), live, 0x600).IsSuccess());
    batch.Commit(tasks[0]);
    REQUIRE(batch.State().write_index == 0);
    REQUIRE(batch.State().outstanding == 2);
    REQUIRE(batch.Reclaim(0, 0).IsSuccess());
    REQUIRE(batch.State().outstanding == 0);
    REQUIRE_FALSE(batch.HasOutstanding(1));
}

TEST_CASE("hwacod admission rejects every bad descriptor without new leases", "[hwacod]") {
    BatchState batch;
    REQUIRE(batch.Initialize(2, 7).IsSuccess());
    const std::array<u8, 2> live{1, 0};
    std::array<Task, 2> tasks{{{7, 0, 0, 1, 0x600, 0, 0, 0},
                              {7, 0, 1, 2, 12, 1, 1, 0}}};
    REQUIRE(batch.ValidateTasks(tasks, live, 0x600).IsSuccess());
    SECTION("duplicate IO") { tasks[1].io = 0; }
    SECTION("wrong manager") { tasks[1].tag = 8; }
    SECTION("missing instance") { tasks[1].instance = 1; }
    SECTION("out of range instance") { tasks[1].instance = 2; }
    SECTION("out of range IO") { tasks[1].io = 2; }
    SECTION("oversize input") { tasks[1].input_bytes = 0x601; }
    SECTION("invalid reset") { tasks[1].reset = 2; }
    SECTION("invalid notify") { tasks[1].notify = 2; }
    SECTION("reserved bits") { tasks[1].reserved = 1; }
    REQUIRE(batch.ValidateTasks(tasks, live, 0x600).IsFailure());
    REQUIRE(batch.State().write_index == 0);
    REQUIRE(batch.State().outstanding == 0);
    REQUIRE(std::ranges::none_of(batch.Leases(), [](const IoLease& lease) { return lease.occupied; }));
    REQUIRE(batch.Reclaim(3, 0).IsFailure());
    REQUIRE(batch.Reclaim(1, 0).IsFailure());
    REQUIRE(batch.Reclaim(0, 1).IsFailure());
    REQUIRE(batch.State().reclaimed_read_index == 0);
}

TEST_CASE("hwacod accepted codec errors have real completions and reclaim normally", "[hwacod]") {
    AudioCore::OpusDecoder::CodecConfig config{};
    REQUIRE(ParseParameters(Parameters(), config).IsSuccess());
    u32 bytes{};
    REQUIRE(AudioCore::OpusDecoder::HostDecoder::RequiredBytes(config, bytes).IsSuccess());
    AudioCore::OpusDecoder::HostDecoder decoder;
    REQUIRE(decoder.Initialize(config, bytes).IsSuccess());
    std::vector<s16> pcm(config.frame_samples * config.channels, 0x1234);
    u32 samples = 99;
    const std::array<u8, 3> packet{};
    const Result result = decoder.Decode(packet, pcm, true, samples);
    REQUIRE(result.IsFailure());
    REQUIRE(samples == 0);
    const Task task{13, 0, 0, 65535, 3, 1, 1, 0};
    const auto entry = MakeCompletion(task, result, 999, -1);
    REQUIRE(std::memcmp(&entry, &task, 8) == 0);
    REQUIRE(entry.result == result.raw);
    REQUIRE(entry.output_bytes == 0);
    REQUIRE(entry.duration_ns == 0);
    REQUIRE(std::ranges::all_of(entry.reserved, [](u8 value) { return value == 0; }));
    BatchState batch;
    REQUIRE(batch.Initialize(1, 13).IsSuccess());
    const std::array<u8, 1> live{1};
    REQUIRE(batch.ValidateTasks(std::span{&task, 1}, live, 0x600).IsSuccess());
    batch.Commit(task);
    REQUIRE(batch.Reclaim(1, 1).IsSuccess());
    REQUIRE(batch.State().outstanding == 0);
}

TEST_CASE("hwacod raw guard checks domain size handles and descriptor byte lengths", "[hwacod]") {
    u32 offset{};
    const RequestShape open{0x80, 2, InputBuffer::None};
    auto words = Request(open, true);
    REQUIRE(ValidateRequest(words, true, open, offset).IsSuccess());
    REQUIRE(offset == 16);
    REQUIRE(ValidateRequest(std::span{words}.first(offset + 31), true, open, offset).IsFailure());
    words[2] = 2;
    REQUIRE(ValidateRequest(words, true, open, offset) == ResultInvalidHandle);
    words = Request(open, true);
    words[8] -= 1u << 16;
    REQUIRE(ValidateRequest(words, true, open, offset).IsFailure());
    const RequestShape parameter{0, 0, InputBuffer::Parameter};
    REQUIRE(ValidateRequest(Request(parameter, true, 0x120), true, parameter, offset).IsSuccess());
    REQUIRE(ValidateRequest(Request(parameter, true, 0x11f), true, parameter, offset).IsFailure());
    REQUIRE(ValidateRequest(Request(parameter, true, 0x121), true, parameter, offset).IsFailure());
    const RequestShape tasks{0, 0, InputBuffer::Tasks};
    REQUIRE(ValidateRequest(Request(tasks, true, 16), true, tasks, offset).IsSuccess());
    REQUIRE(ValidateRequest(Request(tasks, true, 17), true, tasks, offset).IsFailure());
    REQUIRE(ValidateRequest(Request(tasks, true, 0), true, tasks, offset).IsSuccess());
    const RequestShape client{0, 0, InputBuffer::None};
    REQUIRE(ValidateRequest(Request(client, false), false, client, offset).IsSuccess());
}

TEST_CASE("hwacod production CMIF signature separates copied handles raw scalars and objects", "[hwacod]") {
    using RootArgs = decltype(ArgumentsOf(&IHardwareOpusDecoderManager::GetClientSession));
    const auto root_in = GetReplyInLayout<RootArgs>(true);
    REQUIRE(root_in.copy_handle_count == 0);
    REQUIRE(root_in.move_handle_count == 0);
    REQUIRE(root_in.cmif_raw_data_size == 0);
    const auto root_domain = GetReplyOutLayout<RootArgs>(true);
    REQUIRE(root_domain.cmif_raw_data_size == 0);
    REQUIRE(root_domain.copy_handle_count == 0);
    REQUIRE(root_domain.move_handle_count == 0);
    REQUIRE(root_domain.domain_interface_count == 1);
    const auto root_session = GetReplyOutLayout<RootArgs>(false);
    REQUIRE(root_session.cmif_raw_data_size == 0);
    REQUIRE(root_session.copy_handle_count == 0);
    REQUIRE(root_session.move_handle_count == 1);
    REQUIRE(root_session.domain_interface_count == 0);
    using Args = decltype(ArgumentsOf(&IClientSession::OpenManager));
    const auto in = GetReplyInLayout<Args>(true);
    REQUIRE(in.cmif_raw_data_size == 0x80);
    REQUIRE(in.copy_handle_count == 2);
    REQUIRE(in.move_handle_count == 0);
    const auto domain = GetReplyOutLayout<Args>(true);
    REQUIRE(domain.cmif_raw_data_size == 4);
    REQUIRE(domain.copy_handle_count == 1);
    REQUIRE(domain.domain_interface_count == 1);
    REQUIRE(domain.move_handle_count == 0);
    const auto session = GetReplyOutLayout<Args>(false);
    REQUIRE(session.move_handle_count == 1);
    REQUIRE(session.domain_interface_count == 0);
    using InstanceArgs = decltype(ArgumentsOf(&IClientSession::GetInstanceSize));
    STATIC_REQUIRE(ArgumentTraits<std::tuple_element_t<1, InstanceArgs>>::Type == ArgumentType::InBuffer);
    STATIC_REQUIRE(std::tuple_element_t<1, InstanceArgs>::Attr == (BufferAttr_In | BufferAttr_HipcPointer));
    using TaskArgs = decltype(ArgumentsOf(&IHardwareAudioCodecManagerSession::Execute));
    STATIC_REQUIRE(std::tuple_element_t<0, TaskArgs>::Attr == (BufferAttr_In | BufferAttr_HipcAutoSelect));
}

TEST_CASE("hwacod real CMIF scalar serializer and domain staging need no fake handles", "[hwacod]") {
    ScopedSystemTestContext context;
    Core::System system;
    Core::Memory::Memory memory{system};
    auto tags = std::make_shared<HardwareAudioCodecTags>();
    std::weak_ptr<IClientSession> weak;
    std::weak_ptr<IClientSession> root_weak;
    {
        HLERequestContext ctx{system.Kernel(), memory, nullptr, nullptr};
        OutTemporaryBuffers temporary{};
        using OpenArgs = decltype(ArgumentsOf(&IClientSession::OpenManager));
        using Raw = std::tuple<std::tuple_element_t<3, OpenArgs>, std::tuple_element_t<4, OpenArgs>,
                               std::tuple_element_t<5, OpenArgs>, std::tuple_element_t<6, OpenArgs>,
                               std::tuple_element_t<7, OpenArgs>>;
        std::array<u8, 0x80> raw{};
        const auto layout = Layout();
        const std::array<u64, 4> values{8192, 0x12345000, 16384, 7};
        std::memcpy(raw.data(), &layout, sizeof(layout));
        std::memcpy(raw.data() + 0x60, values.data(), sizeof(values));
        Raw parsed{};
        ReadInArgument<Raw>(true, parsed, raw.data(), ctx, temporary);
        REQUIRE(std::get<0>(parsed).io_count == layout.io_count);
        REQUIRE(std::get<1>(parsed) == values[0]);
        REQUIRE(std::get<2>(parsed) == values[1]);
        REQUIRE(std::get<3>(parsed) == values[2]);
        REQUIRE(std::get<4>(parsed) == values[3]);
        using Outputs = std::tuple<Out<u32>, OutCopyHandle<Kernel::KReadableEvent>,
                                   Out<SharedPointer<IClientSession>>>;
        std::tuple<AutoOut<u32>, AutoOut<Kernel::KReadableEvent*>,
                   AutoOut<SharedPointer<IClientSession>>> output{};
        std::get<0>(output).raw = 0x1234;
        std::get<2>(output).raw = std::make_shared<IClientSession>(system, tags);
        weak = std::get<2>(output).raw;
        std::array<u8, 16> serialized{};
        serialized.fill(0xcd);
        WriteOutArgument<Outputs>(true, output, serialized.data(), ctx, temporary);
        u32 tag{};
        std::memcpy(&tag, serialized.data(), sizeof(tag));
        REQUIRE(tag == 0x1234);
        REQUIRE(std::ranges::all_of(std::span{serialized}.subspan(4), [](u8 value) { return value == 0xcd; }));
        std::get<2>(output).raw.reset();
        REQUIRE_FALSE(weak.expired());
        using RootArgs = decltype(ArgumentsOf(&IHardwareOpusDecoderManager::GetClientSession));
        std::tuple<UnwrapArg<std::tuple_element_t<0, RootArgs>>::Type> root_output{};
        std::get<0>(root_output).raw = std::make_shared<IClientSession>(system, tags);
        root_weak = std::get<0>(root_output).raw;
        serialized.fill(0xcd);
        WriteOutArgument<RootArgs>(true, root_output, serialized.data(), ctx, temporary);
        REQUIRE(std::ranges::all_of(serialized, [](u8 value) { return value == 0xcd; }));
        std::get<0>(root_output).raw.reset();
        REQUIRE_FALSE(root_weak.expired());
    }
    REQUIRE(weak.expired());
    REQUIRE(root_weak.expired());
}

TEST_CASE("hwacod tags remain unique while the shared owner outlives clients", "[hwacod]") {
    auto root = std::make_shared<HardwareAudioCodecTags>();
    auto child = root;
    u16 first{}, second{};
    REQUIRE(root->Acquire(first).IsSuccess());
    REQUIRE(child->Acquire(second).IsSuccess());
    REQUIRE(first != 0);
    REQUIRE(second != 0);
    REQUIRE(first != second);
    root.reset();
    child->Release(first);
    child->Release(second);
    u16 third{};
    REQUIRE(child->Acquire(third).IsSuccess());
    child->Release(third);
}
