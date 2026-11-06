// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include "audio_core/opus/batch_decoder.h"
#include "core/hle/result.h"

namespace Service::Audio::Hwacod {

struct SharedMemoryWire {
    u32 version;
    u32 manager_size, manager_offset;
    u32 instances_size, instances_offset;
    u32 transfer_queue_size, transfer_queue_offset, transfer_size;
    u32 io_size, io_offset, completion_size, completion_offset, process_size;
    u32 instance_stride, instance_count, input_stride, output_stride, io_count;
    std::array<u32, 6> reserved;
};
static_assert(sizeof(SharedMemoryWire) == 0x60);
static_assert(offsetof(SharedMemoryWire, completion_offset) == 0x2c);
static_assert(offsetof(SharedMemoryWire, instance_stride) == 0x34);

struct Task {
    u16 tag, instance, io, sequence;
    u32 input_bytes;
    u8 reset, notify;
    u16 reserved;
};
static_assert(sizeof(Task) == 0x10);
static_assert(offsetof(Task, reset) == 12);

struct Completion {
    u16 tag, instance, io, sequence;
    s64 duration_ns;
    u32 output_bytes, result;
    std::array<u8, 40> reserved;
};
static_assert(sizeof(Completion) == 0x40);
static_assert(offsetof(Completion, result) == 0x14);

struct ManagerState {
    u32 version, tag, write_index, reclaimed_read_index;
    u32 io_count, live_instances, outstanding, reserved;
    std::array<u8, 32> padding;
};
struct IoLease {
    u32 occupied, completion_slot;
    u16 instance, sequence;
    u32 reserved;
};
static_assert(sizeof(ManagerState) == 64);
static_assert(sizeof(IoLease) == 16);

enum class InputBuffer { None, Parameter, Tasks };
struct RequestShape {
    u32 scalar_bytes;
    u32 copy_handles;
    InputBuffer buffer;
};

Result ValidateRequest(std::span<const u32> words, bool domain, RequestShape shape,
                       u32& scalar_offset);
Result ParseParameters(std::span<const u8> bytes, AudioCore::OpusDecoder::CodecConfig& config);
Result GetManagerSize(u32 io_count, u32& bytes);
Result ValidateLayout(const SharedMemoryWire& layout, u64 transfer_size, u64 process_address,
                      u64 process_size, u64 properties);
Completion MakeCompletion(const Task& task, Result result, u32 bytes, s64 duration_ns);

class BatchState {
public:
    Result Initialize(u32 io_count, u16 tag);
    ManagerState& State();
    const ManagerState& State() const;
    std::span<IoLease> Leases();
    std::span<const IoLease> Leases() const;
    Result Reclaim(u32 consumer, u32 producer);
    Result ValidateTasks(std::span<const Task> tasks, std::span<const u8> live_instances,
                         u32 input_stride) const;
    void Commit(const Task& task);
    bool HasOutstanding(u32 instance) const;
    u32 StorageBytes() const;

private:
    std::unique_ptr<u8[]> storage;
    u32 storage_bytes{};
};

}
