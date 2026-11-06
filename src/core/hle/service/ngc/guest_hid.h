// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/core.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/svc_results.h"
#include "core/hle/service/ipc_helpers.h"

namespace Service::NGC {

struct GuestHidMemoryConfig {
    u64 size;
    u64 offset;
};
static_assert(sizeof(GuestHidMemoryConfig) == 0x10);

inline void ReplyGuestHidSharedMemory(HLERequestContext& ctx, Core::System& system,
                                      Kernel::KSharedMemory*& guest_hid_memory) {
    constexpr u64 GuestHidMemorySize = 0x9b000;
    auto& kernel = system.Kernel();
    if (!guest_hid_memory) {
        auto* memory = Kernel::KSharedMemory::Create(kernel);
        const auto result =
            memory ? memory->Initialize(kernel, system.DeviceMemory(), nullptr,
                                        Kernel::Svc::MemoryPermission::ReadWrite,
                                        Kernel::Svc::MemoryPermission::Read, GuestHidMemorySize)
                   : Kernel::ResultOutOfResource;
        if (R_FAILED(result)) {
            if (memory) {
                memory->Close(kernel);
            }
            IPC::ResponseBuilder rb{ctx, 2};
            rb.Push(result);
            return;
        }
        Kernel::KSharedMemory::Register(kernel, memory);
        guest_hid_memory = memory;
        LOG_INFO(Service_NGC,
                 "GameShare guest HID shared memory: size={:#x}, offset=0, no guests connected",
                 GuestHidMemorySize);
    }
    IPC::ResponseBuilder rb{ctx, 6, 1};
    rb.Push(ResultSuccess);
    rb.PushRaw(GuestHidMemoryConfig{GuestHidMemorySize, 0});
    rb.PushCopyObjects(ctx, guest_hid_memory);
}

} // namespace Service::NGC
