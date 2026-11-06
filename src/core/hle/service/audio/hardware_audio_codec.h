// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <bitset>
#include <memory>
#include <mutex>
#include "core/hle/service/audio/hardware_audio_codec_protocol.h"
#include "core/hle/service/cmif_types.h"
#include "core/hle/service/service.h"

namespace Kernel {
class KTransferMemory;
class KProcess;
class KReadableEvent;
}

namespace Service::Audio {

class HardwareAudioCodecTags {
public:
    Result Acquire(u16& tag);
    void Release(u16 tag);

private:
    std::mutex mutex;
    std::bitset<65536> live;
    u16 next{1};
};

bool GuardHardwareAudioCodecRequest(HLERequestContext& ctx, Hwacod::RequestShape shape);

class IHardwareAudioCodecManagerSession final
    : public ServiceFramework<IHardwareAudioCodecManagerSession> {
public:
    IHardwareAudioCodecManagerSession(Core::System& system,
                                     std::shared_ptr<HardwareAudioCodecTags> tags);
    ~IHardwareAudioCodecManagerSession() override;
    Result Initialize(const Hwacod::SharedMemoryWire& layout, u64 transfer_size,
                      u64 process_address, u64 process_size, u64 properties,
                      Kernel::KTransferMemory* transfer, Kernel::KProcess* process);
    u16 GetTag() const;
    Kernel::KReadableEvent* GetEvent();
    Result InitializeInstance(u32 id, InBuffer<BufferAttr_HipcPointer> bytes);
    Result FinalizeInstance(u32 id);
    Result Execute(InBuffer<BufferAttr_HipcAutoSelect> bytes);

private:
    void Dispatch(HLERequestContext& ctx);
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class IClientSession final : public ServiceFramework<IClientSession> {
public:
    IClientSession(Core::System& system, std::shared_ptr<HardwareAudioCodecTags> tags);
    ~IClientSession() override;
    Result GetInstanceSize(Out<u32> bytes, InBuffer<BufferAttr_HipcPointer> parameters);
    Result GetManagerSize(Out<u32> bytes, u32 io_count);
    Result OpenManager(Out<u32> tag, OutCopyHandle<Kernel::KReadableEvent> event,
                       Out<SharedPointer<IHardwareAudioCodecManagerSession>> child,
                       Hwacod::SharedMemoryWire layout, u64 transfer_size, u64 process_address,
                       u64 process_size, u64 properties,
                       InCopyHandle<Kernel::KTransferMemory> transfer,
                       InCopyHandle<Kernel::KProcess> process);

private:
    void Dispatch(HLERequestContext& ctx);
    std::shared_ptr<HardwareAudioCodecTags> tags;
};

}
