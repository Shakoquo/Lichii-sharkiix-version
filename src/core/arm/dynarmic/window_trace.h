// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include "common/common_types.h"

namespace Kernel {
class KProcess;
}

namespace Core::WindowTrace {

inline constexpr u64 ProgramId = 0x0400E88025B1A000ULL;
constexpr bool EnabledValue(const char* value) {
    return value != nullptr && value[0] != '\0' && std::string_view{value} != "0";
}

inline bool Enabled() {
    static const bool enabled = EnabledValue(std::getenv("EDEN_STARFOX_WINDOW_TRACE"));
    return enabled;
}

enum class Point {
    None,
    WindowInitialize,
    NativeInitialize,
    ValidateTexture,
    NativeInitializeSuccess,
    NativeInitializeFailure,
    WindowInitializeReturn,
    Acquire,
    Finalize,
    FinalizeCleared,
};

struct WindowIdentity {
    u64 window{};
    u64 native{};
};

class Session {
public:
    static constexpr u32 PointLimit = 256;
    static constexpr u32 GsvLimit = 256;

    Session(const Kernel::KProcess* owner_, u64 base_, u64 id_)
        : owner{owner_}, base{base_}, id{id_} {}

    std::optional<WindowIdentity> Record(Point point, u64 tls, u64 window = 0, u64 native = 0) {
        std::scoped_lock lock{mutex};
        if (point_count >= PointLimit) {
            return std::nullopt;
        }
        ++point_count;
        auto& identity = identities[tls];
        if (point == Point::WindowInitialize || point == Point::Acquire || point == Point::Finalize) {
            identity = {window, native};
        } else if (point == Point::NativeInitialize) {
            identity.native = native;
        }
        const auto result = identity;
        if (point == Point::WindowInitializeReturn || point == Point::FinalizeCleared ||
            point == Point::Acquire) {
            identities.erase(tls);
        }
        if (point_count == PointLimit) {
            identities.clear();
        }
        return result;
    }

    bool TakeGsvLog() {
        std::scoped_lock lock{mutex};
        if (gsv_count >= GsvLimit) {
            return false;
        }
        ++gsv_count;
        return true;
    }

    const Kernel::KProcess* const owner;
    const u64 base;
    const u64 id;

private:
    std::mutex mutex;
    std::map<u64, WindowIdentity> identities;
    u32 point_count{};
    u32 gsv_count{};
};

class Sessions {
public:
    void Reset(const Kernel::KProcess* owner, u64 base) {
        auto session =
            base != 0 ? std::make_shared<Session>(owner, base, next_id.fetch_add(1)) : nullptr;
        std::scoped_lock lock{mutex};
        current = std::move(session);
    }

    std::shared_ptr<Session> Find(const Kernel::KProcess* owner) const {
        std::scoped_lock lock{mutex};
        return current && current->owner == owner ? current : nullptr;
    }

private:
    mutable std::mutex mutex;
    std::shared_ptr<Session> current;
    std::atomic<u64> next_id{1};
};

inline Sessions sessions;

constexpr Point Identify(u64 base, u64 address, u32 instruction) {
    if (base == 0 || address < base) {
        return Point::None;
    }
    const u64 offset = address - base;
    if (instruction == 0xD503237F) {
        switch (offset) {
        case 0x675600: return Point::WindowInitialize;
        case 0x5E5980: return Point::NativeInitialize;
        case 0x5E5D50: return Point::Acquire;
        case 0x5E5B90: return Point::Finalize;
        }
    }
    if (instruction == 0xD65F0FFF) {
        switch (offset) {
        case 0x5E5AB0: return Point::NativeInitializeSuccess;
        case 0x5E5B88: return Point::NativeInitializeFailure;
        case 0x6757B4: return Point::WindowInitializeReturn;
        }
    }
    if (offset == 0x5E5D40 && instruction == 0xD50323FF) {
        return Point::FinalizeCleared;
    }
    if (offset == 0x5E6A34 && instruction == (0xB9400000U | (3U << 10) | (25U << 5) | 8U)) {
        return Point::ValidateTexture;
    }
    return Point::None;
}

constexpr std::string_view Name(Point point) {
    switch (point) {
    case Point::WindowInitialize: return "nvn-init-entry";
    case Point::NativeInitialize: return "native-init-entry";
    case Point::ValidateTexture: return "texture-validation";
    case Point::NativeInitializeSuccess: return "native-init-success";
    case Point::NativeInitializeFailure: return "native-init-failure";
    case Point::WindowInitializeReturn: return "nvn-init-return";
    case Point::Acquire: return "acquire";
    case Point::Finalize: return "finalize-entry";
    case Point::FinalizeCleared: return "finalize-cleared";
    case Point::None: return "none";
    }
    return "none";
}

constexpr u32 InstructionFor(u64 base, u64 address, u32 instruction) {
    return Identify(base, address, instruction) == Point::None ? instruction : 0xD4200000;
}

template <typename Jit, typename Memory>
void Resume(Point point, Jit& jit, Memory& memory) {
    switch (point) {
    case Point::ValidateTexture:
        jit.SetRegister(8, memory.Read32(jit.GetRegister(25) + 12));
        break;
    case Point::NativeInitializeSuccess:
    case Point::NativeInitializeFailure:
    case Point::WindowInitializeReturn:
        jit.SetPC(jit.GetRegister(30));
        break;
    default:
        break;
    }
}

} // namespace Core::WindowTrace
