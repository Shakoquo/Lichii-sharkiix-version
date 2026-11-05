// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yuzu_cmd/nx2_sampler.h"
#include "common/logging.h"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>
#include <psapi.h>

namespace Nx2Sampler {
namespace {

constexpr size_t MaxDepth = 40;

constexpr size_t GuestDepth = 12;

struct Sample {
    unsigned thread;
    unsigned depth;
    std::array<DWORD64, MaxDepth> frames;
    // In JIT code (guest code): the guest's return address and frame-pointer chain, its stack
    // pointer (which guest thread) and where guest memory is mapped (dynarmic's r13).
    unsigned guest_depth;
    std::array<DWORD64, GuestDepth> guest;
    DWORD64 guest_sp;
    DWORD64 fastmem;
};

// Reads memory that may not be mapped, without faulting and without taking user-mode locks.
bool SafeRead(DWORD64 address, void* out, size_t size) {
    SIZE_T done = 0;
    return address != 0 &&
           ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size,
                             &done) &&
           done == size;
}

// dynarmic keeps its A64JitState in r15 (reg[0..30], sp, pc first) and the base of guest memory
// in r13 while it runs guest code.
void GuestFrames(const CONTEXT& context, Sample& sample) {
    sample.guest_depth = 0;
    sample.guest_sp = 0;
    sample.fastmem = context.R13;
    std::array<unsigned long long, 32> regs{};
    if (!SafeRead(context.R15, regs.data(), sizeof(regs))) {
        return;
    }
    sample.guest_sp = regs[31];
    sample.guest[sample.guest_depth++] = regs[30]; // LR
    unsigned long long fp = regs[29];
    while (sample.guest_depth < GuestDepth && fp != 0 && (fp & 7) == 0) {
        std::array<unsigned long long, 2> record{}; // previous fp, return address
        if (!SafeRead(context.R13 + fp, record.data(), sizeof(record)) || record[1] == 0) {
            break;
        }
        sample.guest[sample.guest_depth++] = record[1];
        if (record[0] <= fp) {
            break;
        }
        fp = record[0];
    }
}

// EDEN_NX2_JIT_MAP (written by dynarmic): host start, size, "a64_<guest pc>_fpcr...".
struct JitMap {
    std::vector<std::tuple<DWORD64, DWORD64, DWORD64>> blocks; // host start, end, guest pc
    JitMap() {
        const char* path = std::getenv("EDEN_NX2_JIT_MAP");
        std::FILE* const file = path != nullptr ? std::fopen(path, "r") : nullptr;
        if (file == nullptr) {
            return;
        }
        unsigned long long start{}, size{}, pc{};
        char line[256];
        while (std::fgets(line, sizeof(line), file) != nullptr) {
            if (std::sscanf(line, "%llx %llx a64_%llx", &start, &size, &pc) == 3) {
                blocks.emplace_back(start, start + size, pc);
            }
        }
        std::fclose(file);
        // A cleared code cache reuses addresses: stable sort, the last entry wins below.
        std::stable_sort(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) {
            return std::get<0>(a) < std::get<0>(b);
        });
    }
    DWORD64 GuestPc(DWORD64 host) const {
        auto it = std::upper_bound(blocks.begin(), blocks.end(), host,
                                   [](DWORD64 value, const auto& block) {
                                       return value < std::get<0>(block);
                                   });
        for (int tries = 0; it != blocks.begin() && tries < 64; ++tries) {
            --it;
            if (host >= std::get<0>(*it) && host < std::get<1>(*it)) {
                return std::get<2>(*it);
            }
        }
        return 0;
    }
};

struct Target {
    HANDLE handle;
    std::string name;
};

double EnvSeconds(const char* name, double fallback) {
    const char* text = std::getenv(name);
    return text != nullptr ? std::strtod(text, nullptr) : fallback;
}

std::vector<std::string> SplitNames(const char* text) {
    std::vector<std::string> names;
    std::string current;
    for (const char* c = text; *c != '\0'; ++c) {
        if (*c == ',') {
            names.push_back(current);
            current.clear();
        } else {
            current += *c;
        }
    }
    if (!current.empty()) {
        names.push_back(current);
    }
    return names;
}

bool Matches(const std::string& thread_name, const std::vector<std::string>& patterns) {
    for (const auto& pattern : patterns) {
        if (!pattern.empty() && pattern.back() == '*') {
            if (thread_name.compare(0, pattern.size() - 1, pattern, 0, pattern.size() - 1) == 0) {
                return true;
            }
        } else if (thread_name == pattern) {
            return true;
        }
    }
    return false;
}

std::vector<Target> FindThreads(const std::vector<std::string>& patterns) {
    using GetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
    const auto get_description = reinterpret_cast<GetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleA("KernelBase.dll"), "GetThreadDescription"));
    std::vector<Target> targets;
    if (get_description == nullptr) {
        return targets;
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return targets;
    }
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    const DWORD self = GetCurrentThreadId();
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == self) {
            continue;
        }
        const HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                             THREAD_QUERY_LIMITED_INFORMATION,
                                         FALSE, entry.th32ThreadID);
        if (handle == nullptr) {
            continue;
        }
        PWSTR wide{};
        std::string name;
        if (SUCCEEDED(get_description(handle, &wide)) && wide != nullptr) {
            char buffer[256]{};
            WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer, sizeof(buffer) - 1, nullptr,
                                nullptr);
            name = buffer;
            LocalFree(wide);
        }
        if (Matches(name, patterns)) {
            targets.push_back({handle, name});
        } else {
            CloseHandle(handle);
        }
    }
    CloseHandle(snapshot);
    return targets;
}

// Address ranges of the loaded images, taken before sampling starts. Unwinding is only safe
// inside them: for any other address (JIT code) RtlLookupFunctionEntry takes ntdll's dynamic
// function table lock, which a suspended CPU thread may hold while it registers new JIT code,
// and the sampler then waits forever with that thread paused (the game froze).
std::vector<std::pair<DWORD64, DWORD64>> g_images;

void CollectImages() {
    HMODULE modules[1024];
    DWORD needed = 0;
    if (!K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        return;
    }
    const DWORD count = (std::min)(needed / static_cast<DWORD>(sizeof(HMODULE)), DWORD{1024});
    for (DWORD i = 0; i < count; ++i) {
        MODULEINFO info{};
        if (K32GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info))) {
            const auto base = reinterpret_cast<DWORD64>(info.lpBaseOfDll);
            g_images.emplace_back(base, base + info.SizeOfImage);
        }
    }
}

bool InImage(DWORD64 address) {
    for (const auto& [begin, end] : g_images) {
        if (address >= begin && address < end) {
            return true;
        }
    }
    return false;
}

// Walks a suspended thread's stack from its context. Nothing here may allocate: the thread
// could be paused holding the heap lock.
unsigned Walk(CONTEXT context, std::array<DWORD64, MaxDepth>& frames) {
    unsigned depth = 0;
    while (depth < MaxDepth && context.Rip != 0) {
        frames[depth++] = context.Rip;
        if (!InImage(context.Rip)) {
            break; // JIT code: no unwind data worth the lock (see g_images)
        }
        DWORD64 image_base{};
        const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image_base, nullptr);
        if (function == nullptr) {
            // A leaf function (no unwind data): its return address is on top of the stack. JIT
            // code has none either; past the first frame, stop there.
            if (depth != 1) {
                break;
            }
            context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
            context.Rsp += 8;
            continue;
        }
        PVOID handler_data{};
        DWORD64 establisher_frame{};
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, context.Rip, function, &context,
                         &handler_data, &establisher_frame, nullptr);
    }
    return depth;
}

struct Symbolizer {
    using SymInitializeFn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
    using SymSetOptionsFn = DWORD(WINAPI*)(DWORD);
    using SymFromAddrFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);

    Symbolizer() {
        const HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        if (dbghelp == nullptr) {
            return;
        }
        const auto set_options =
            reinterpret_cast<SymSetOptionsFn>(GetProcAddress(dbghelp, "SymSetOptions"));
        const auto initialize =
            reinterpret_cast<SymInitializeFn>(GetProcAddress(dbghelp, "SymInitialize"));
        from_address = reinterpret_cast<SymFromAddrFn>(GetProcAddress(dbghelp, "SymFromAddr"));
        if (set_options == nullptr || initialize == nullptr || from_address == nullptr) {
            from_address = nullptr;
            return;
        }
        set_options(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        if (!initialize(GetCurrentProcess(), nullptr, TRUE)) {
            from_address = nullptr;
        }
    }

    const std::string& Name(DWORD64 address) {
        const auto [it, is_new] = cache.try_emplace(address);
        if (!is_new) {
            return it->second;
        }
        if (from_address != nullptr) {
            alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 1024]{};
            auto* const symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 1024;
            DWORD64 displacement{};
            if (from_address(GetCurrentProcess(), address, &displacement, symbol)) {
                it->second.assign(symbol->Name, std::min<size_t>(symbol->NameLen, 160));
                return it->second;
            }
        }
        HMODULE module{};
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(address), &module)) {
            char path[MAX_PATH] = "?";
            GetModuleFileNameA(module, path, sizeof(path));
            const char* base = std::strrchr(path, '\\');
            it->second = std::string{base != nullptr ? base + 1 : path} + "!?";
        } else {
            it->second = "(jit)";
        }
        return it->second;
    }

    SymFromAddrFn from_address{};
    std::unordered_map<DWORD64, std::string> cache;
};

void Report(const std::vector<Target>& targets, const std::vector<Sample>& samples,
            const char* path) {
    Symbolizer symbolizer;
    const JitMap jit_map;
    // r13 in a guest block is the base of guest memory; elsewhere it is anything: most common wins.
    std::map<DWORD64, unsigned> fastmem_votes;
    std::FILE* const file = std::fopen(path, "w");
    if (file == nullptr) {
        return;
    }
    for (unsigned thread = 0; thread < targets.size(); ++thread) {
        std::map<std::string, unsigned> self;
        std::map<std::string, unsigned> inclusive;
        std::map<std::string, unsigned> self_caller;
        unsigned total = 0;
        for (const Sample& sample : samples) {
            if (sample.thread != thread || sample.depth == 0) {
                continue;
            }
            ++total;
            const std::string& top = symbolizer.Name(sample.frames[0]);
            ++self[top];
            if (sample.depth > 1) {
                ++self_caller[top + "  <-  " + symbolizer.Name(sample.frames[1])];
            }
            std::set<std::string> seen;
            for (unsigned i = 0; i < sample.depth; ++i) {
                const std::string& name = symbolizer.Name(sample.frames[i]);
                if (seen.insert(name).second) {
                    ++inclusive[name];
                }
            }
        }
        const auto print = [&](const char* title, const std::map<std::string, unsigned>& counts,
                               size_t limit) {
            std::vector<std::pair<unsigned, std::string>> sorted;
            for (const auto& [name, count] : counts) {
                sorted.emplace_back(count, name);
            }
            std::sort(sorted.rbegin(), sorted.rend());
            std::fprintf(file, "-- %s\n", title);
            for (size_t i = 0; i < (std::min)(limit, sorted.size()); ++i) {
                std::fprintf(file, "%6u %5.1f%% %s\n", sorted[i].first,
                             100.0 * sorted[i].first / (std::max)(total, 1U),
                             sorted[i].second.c_str());
            }
        };
        std::fprintf(file, "=== thread \"%s\": %u samples\n", targets[thread].name.c_str(), total);
        print("self (where the thread is)", self, 60);
        print("self <- caller", self_caller, 50);
        print("inclusive (on the stack)", inclusive, 90);
        // Guest code under the JIT samples (EDEN_NX2_JIT_MAP): which guest block, its callers.
        std::map<std::string, unsigned> guest_block, guest_stack, guest_thread;
        unsigned guest_total = 0;
        for (const Sample& sample : samples) {
            if (sample.thread != thread || sample.depth == 0 || InImage(sample.frames[0])) {
                continue;
            }
            ++guest_total;
            const DWORD64 pc = jit_map.GuestPc(sample.frames[0]);
            char text[64];
            std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(pc));
            ++guest_block[text];
            std::string chain = text;
            for (unsigned i = 0; i < (std::min)(sample.guest_depth, 8U); ++i) {
                std::snprintf(text, sizeof(text), " <- %llx",
                              static_cast<unsigned long long>(sample.guest[i]));
                chain += text;
            }
            ++guest_stack[chain];
            std::snprintf(text, sizeof(text), "stack %llx",
                          static_cast<unsigned long long>(sample.guest_sp >> 16));
            ++guest_thread[text];
            if (sample.fastmem != 0 && pc != 0) {
                ++fastmem_votes[sample.fastmem];
            }
        }
        if (guest_total != 0) {
            const unsigned saved_total = total;
            total = guest_total;
            std::fprintf(file, "-- guest: %u samples in JIT code\n", guest_total);
            print("guest block (guest pc)", guest_block, 60);
            print("guest stack (pc <- lr <- frame-pointer chain)", guest_stack, 60);
            print("guest thread (stack pointer / 64 KiB)", guest_thread, 20);
            total = saved_total;
        }
        std::fprintf(file, "\n");
    }
    std::fclose(file);

    // <out>.folded: every distinct stack with its count, root first ("thread;a;b;c count").
    std::map<std::string, unsigned> folded;
    for (const Sample& sample : samples) {
        std::string line = targets[sample.thread].name;
        for (unsigned i = sample.depth; i-- > 0;) {
            line += ';';
            line += symbolizer.Name(sample.frames[i]);
        }
        ++folded[line];
    }
    if (std::FILE* const out = std::fopen((std::string{path} + ".folded").c_str(), "w")) {
        for (const auto& [line, count] : folded) {
            std::fprintf(out, "%s %u\n", line.c_str(), count);
        }
        std::fclose(out);
    }
    // EDEN_NX2_SAMPLE_GUEST_DUMP=<start>:<size> (hex): guest memory written to <out>.guest.bin
    // (unreadable pages as zeros), to disassemble the hot guest code offline.
    DWORD64 fastmem_base = 0;
    unsigned best_votes = 0;
    for (const auto& [base, votes] : fastmem_votes) {
        if (votes > best_votes) {
            best_votes = votes;
            fastmem_base = base;
        }
    }
    LOG_INFO(Frontend, "nx2 sampler: guest memory base {:#x} ({} votes, {} bases)", fastmem_base,
             best_votes, fastmem_votes.size());
    if (const char* range = std::getenv("EDEN_NX2_SAMPLE_GUEST_DUMP");
        range != nullptr && fastmem_base != 0) {
        unsigned long long start{}, size{};
        if (std::sscanf(range, "%llx:%llx", &start, &size) == 2) {
            if (std::FILE* const out =
                    std::fopen((std::string{path} + ".guest.bin").c_str(), "wb")) {
                std::vector<char> page(0x10000);
                for (unsigned long long offset = 0; offset < size; offset += page.size()) {
                    if (!SafeRead(fastmem_base + start + offset, page.data(), page.size())) {
                        std::fill(page.begin(), page.end(), char{0});
                        for (size_t part = 0; part < page.size(); part += 0x1000) {
                            SafeRead(fastmem_base + start + offset + part, page.data() + part,
                                     0x1000);
                        }
                    }
                    std::fwrite(page.data(), 1, page.size(), out);
                }
                std::fclose(out);
            }
        }
    }
}

void Run(std::vector<std::string> patterns, double start, double seconds, std::string out) {
    std::this_thread::sleep_for(std::chrono::duration<double>(start));
    CollectImages();
    const std::vector<Target> targets = FindThreads(patterns);
    // How busy each thread is over the window (CPU time / wall time), safe for every thread.
    const auto cpu_time = [](HANDLE handle) {
        FILETIME created, exited, kernel, user;
        if (!GetThreadTimes(handle, &created, &exited, &kernel, &user)) {
            return 0.0;
        }
        const auto to_ms = [](FILETIME t) {
            return static_cast<double>((static_cast<unsigned long long>(t.dwHighDateTime) << 32) |
                                       t.dwLowDateTime) / 10000.0;
        };
        return to_ms(kernel) + to_ms(user);
    };
    std::vector<double> cpu_start;
    for (const auto& target : targets) {
        cpu_start.push_back(cpu_time(target.handle));
    }
    const auto wall_start = std::chrono::steady_clock::now();
    std::fprintf(stderr, "nx2 sampler: %zu threads match, sampling %.0f s\n", targets.size(),
                 seconds);
    if (targets.empty()) {
        return;
    }
    std::vector<Sample> samples;
    samples.reserve(static_cast<size_t>(seconds * 1100.0) * targets.size() + 16);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < deadline &&
           samples.size() + targets.size() <= samples.capacity()) {
        for (unsigned i = 0; i < targets.size(); ++i) {
            const HANDLE handle = targets[i].handle;
            if (SuspendThread(handle) == static_cast<DWORD>(-1)) {
                continue;
            }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_FULL;
            if (GetThreadContext(handle, &context)) {
                Sample& sample = samples.emplace_back();
                sample.thread = i;
                sample.depth = Walk(context, sample.frames);
                sample.guest_depth = 0;
                if (sample.depth != 0 && !InImage(sample.frames[0])) {
                    GuestFrames(context, sample);
                }
            }
            ResumeThread(handle);
        }
        Sleep(1);
    }
    const double wall_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall_start)
            .count();
    if (std::FILE* const cpu = std::fopen((out + ".cpu.txt").c_str(), "w")) {
        for (size_t i = 0; i < targets.size(); ++i) {
            std::fprintf(cpu, "%-24s %5.1f%% busy\n", targets[i].name.c_str(),
                         100.0 * (cpu_time(targets[i].handle) - cpu_start[i]) / wall_ms);
        }
        std::fclose(cpu);
    }
    Report(targets, samples, out.c_str());
    std::fprintf(stderr, "nx2 sampler: %zu samples written to %s\n", samples.size(), out.c_str());
    for (const auto& target : targets) {
        CloseHandle(target.handle);
    }
    // EDEN_NX2_SAMPLE_QUIT: close the emulator once the report is written (one-shot profiling runs).
    if (std::getenv("EDEN_NX2_SAMPLE_QUIT") != nullptr) {
        Common::Log::Stop();
        std::_Exit(0);
    }
}

} // Anonymous namespace

void StartFromEnvironment() {
    const char* names = std::getenv("EDEN_NX2_SAMPLE");
    if (names == nullptr || *names == '\0') {
        return;
    }
    const char* out = std::getenv("EDEN_NX2_SAMPLE_OUT");
    std::thread{Run, SplitNames(names), EnvSeconds("EDEN_NX2_SAMPLE_START", 60.0),
                EnvSeconds("EDEN_NX2_SAMPLE_SECONDS", 30.0),
                std::string{out != nullptr ? out : "nx2_samples.txt"}}
        .detach();
}

} // namespace Nx2Sampler

#else

namespace Nx2Sampler {
void StartFromEnvironment() {}
} // namespace Nx2Sampler

#endif
