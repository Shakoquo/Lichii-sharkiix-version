// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include "common/page_table.h"
#include "common/scope_exit.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/kernel/k_shared_memory.h"
#include "core/hle/kernel/k_thread.h"
#include "core/hle/kernel/svc.h"
#include "core/hle/service/ngc/ngc.h"
#include "core/hle/service/nx2_streamplay.h"
#include "tests/core/system_test_context.h"

TEST_CASE("Streamplay command 300 returns mappable guest HID memory", "[streamplay][starfox]") {
    const bool registered_service = GENERATE(false, true);
    const bool domain = GENERATE(false, true);
    CAPTURE(registered_service, domain);
    ScopedSystemTestContext environment;
    Core::System system;
    system.Initialize();
    auto& kernel = system.Kernel();
    Kernel::KThread bootstrap{kernel};
    REQUIRE(Kernel::KThread::InitializeDummyThread(system, &bootstrap, nullptr).IsSuccess());
    kernel.SetCurrentEmuThread(&bootstrap);
    kernel.Initialize();
    SCOPE_EXIT {
        system.CoreTiming().ClearPendingEvents();
        kernel.Shutdown();
        kernel.SetCurrentEmuThread(nullptr);
        kernel.UnregisterKernelObject(&bootstrap);
    };

    Kernel::KProcess process{kernel};
    Kernel::KAutoObject::Create(&process);
    SCOPE_EXIT {
        kernel.UnregisterKernelObject(&process);
    };
    auto& page_table = process.GetPageTable();
    auto& memory = process.GetMemory();
    REQUIRE(page_table
                .Initialize(Kernel::Svc::CreateProcessFlag::AddressSpace64Bit, false, false, false,
                            Kernel::KMemoryManager::Pool::Application, 0x8000000, 0x1000,
                            &kernel.GetSystemSystemResource(), kernel.GetSystemResourceLimit(),
                            memory, 0)
                .IsSuccess());
    SCOPE_EXIT {
        page_table.Finalize();
    };
    Kernel::Svc::CreateProcessParameter parameters{};
    parameters.flags = Kernel::Svc::CreateProcessFlag::AddressSpace64Bit;
    REQUIRE(
        process.Initialize(kernel, parameters, kernel.GetSystemResourceLimit(), false).IsSuccess());
    SCOPE_EXIT {
        process.GetResourceLimit()->Close(kernel);
    };
    memory.SetCurrentPageTable(process);
    alignas(0x1000) std::array<u32, 0x400> tls{};
    page_table.GetImpl().entries.GetAndFault(0).Store(false, Common::PageType::Memory, 1,
                                                      reinterpret_cast<uintptr_t>(tls.data()));
    SCOPE_EXIT {
        page_table.GetImpl().entries.GetAndFault(0).Store(false, Common::PageType::Unmapped, 1, 0);
    };
    REQUIRE(process.GetHandleTable().Initialize(kernel, 32).IsSuccess());
    SCOPE_EXIT {
        process.GetHandleTable().Finalize(kernel);
    };

    Kernel::KThread client{kernel};
    Kernel::KAutoObject::Create(&client);
    REQUIRE(Kernel::KThread::InitializeDummyThread(system, &client, &process).IsSuccess());
    kernel.SetCurrentEmuThread(&client);
    SCOPE_EXIT {
        process.UnregisterThread(kernel, &client);
        process.Close(kernel);
        kernel.SetCurrentEmuThread(&bootstrap);
        kernel.UnregisterKernelObject(&client);
    };

    std::optional<std::jthread> server_thread;
    Service::ServerManager server{system};
    server_thread.emplace(
        kernel.RunOnHostCoreProcess("streamplay-test", [&] { server.LoopProcess(); }));
    auto manager = std::make_shared<Service::SessionRequestManager>(kernel, server);
    std::shared_ptr<Service::ServiceFrameworkBase> service;
    if (registered_service) {
        service = Service::NGC::CreateStreamPlayUserService(system);
    } else {
        service = std::make_shared<Service::INx2StreamPlayUserService>(system);
    }
    manager->SetSessionHandler(std::shared_ptr<Service::ServiceFrameworkBase>{service});
    if (domain) {
        manager->ConvertToDomain();
    }

    Kernel::KSharedMemory* first_memory{};
    for (unsigned request = 0; request < 2; ++request) {
        CAPTURE(request);
        tls.fill(0xa5a5a5a5);
        std::array<u32_le, IPC::COMMAND_BUFFER_LENGTH> words{};
        IPC::CommandHeader header{};
        header.type.Assign(IPC::CommandType::RequestWithContext);
        header.data_size.Assign(domain ? 12 : 8);
        std::memcpy(words.data(), &header, sizeof(header));
        const u32 payload = domain ? 8 : 4;
        if (domain) {
            IPC::DomainMessageHeader domain_header{};
            domain_header.command.Assign(IPC::DomainMessageHeader::CommandType::SendMessage);
            domain_header.size.Assign(16);
            domain_header.object_id = 1;
            std::memcpy(words.data() + 4, &domain_header, sizeof(domain_header));
        }
        words[payload] = 0x49434653;
        words[payload + 1] = 1;
        words[payload + 2] = 300;
        Service::HLERequestContext ctx{kernel, memory, nullptr, &client};
        ctx.SetSessionRequestManager(manager);
        REQUIRE(ctx.PopulateFromIncomingCommandBuffer(words.data()).IsSuccess());
        service->InvokeRequest(ctx);
        REQUIRE(ctx.WriteToOutgoingCommandBuffer().IsSuccess());
        IPC::HandleDescriptorHeader handles{};
        std::memcpy(&handles, tls.data() + 2, sizeof(handles));
        REQUIRE(handles.num_handles_to_copy == 1);
        REQUIRE(handles.num_handles_to_move == 0);
        REQUIRE(tls[payload] == 0x4f434653);
        REQUIRE(tls[payload + 2] == ResultSuccess.raw);
        u64 size{};
        u64 offset{};
        std::memcpy(&size, tls.data() + payload + 4, sizeof(size));
        std::memcpy(&offset, tls.data() + payload + 6, sizeof(offset));
        REQUIRE(size == 0x9b000);
        REQUIRE(offset == 0);
        auto shared = process.GetHandleTable().GetObject<Kernel::KSharedMemory>(kernel, tls[3]);
        REQUIRE(shared.IsNotNull());
        if (request == 0) {
            first_memory = shared.GetPointerUnsafe();
        } else {
            REQUIRE(shared.GetPointerUnsafe() == first_memory);
        }
        const auto address = GetInteger(page_table.GetAliasCodeRegionStart()) + 0x100000;
        REQUIRE(Kernel::Svc::MapSharedMemory(system, tls[3], address, size - 0x1000,
                                             Kernel::Svc::MemoryPermission::Read) ==
                Kernel::ResultInvalidSize);
        REQUIRE(Kernel::Svc::MapSharedMemory(system, tls[3], address, size,
                                             Kernel::Svc::MemoryPermission::ReadWrite) ==
                Kernel::ResultInvalidNewMemoryPermission);
        REQUIRE(Kernel::Svc::MapSharedMemory(system, tls[3], address, size,
                                             Kernel::Svc::MemoryPermission::Read)
                    .IsSuccess());
        SCOPE_EXIT {
            Kernel::Svc::UnmapSharedMemory(system, tls[3], address, size);
        };
        std::vector<u8> bytes(0x9b000, 0xff);
        REQUIRE(memory.ReadBlock(address + offset, bytes.data(), bytes.size()));
        REQUIRE(std::ranges::all_of(bytes, [](u8 value) { return value == 0; }));
    }
    manager.reset();
    service.reset();
    auto shared = process.GetHandleTable().GetObject<Kernel::KSharedMemory>(kernel, tls[3]);
    REQUIRE(shared.IsNotNull());
    REQUIRE(shared.GetPointerUnsafe() == first_memory);
    REQUIRE(shared->GetPointer()[0x9a7ff] == 0);
}
