// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "common/logging.h"

// EXPERIMENT (not for nx2): GPU back-thread optimizations for Ocarina of Time.
// split_overlap is quarantined. Frizti build: the options reviewed as exact rewrites are on by
// default; those that change timing or reuse ideas reverted with zortos293/Lichii#161 (its
// slab of wrong geometry in Kokiri Forest) are off until EDEN_NX2_PERF_ON=<name>,... turns them
// on: frames_ahead, clean_words, index_rebind, submit_budget, fence_flush, fragment_barrier,
// front_sleep, async_pipelines, and bounds (this tree keeps its own range cache).
// EDEN_NX2_PERF_OPT=0 turns every one off for the A/B; EDEN_NX2_PERF_OFF=<name>,... turns off
// only those named (Opt below). EDEN_NX2_PERF_VERIFY=1 computes each optimized answer the old
// way too, logs any difference and uses the old answer.
namespace VideoCommon::Nx2Perf {

enum class Opt : unsigned {
    Bounds,            ///< MaxContinuousRange / GetMemoryLayoutSize from map-time runs
    Method,            ///< Maxwell3D fast method dispatch on the back
    CbBatch,           ///< Consecutive constant buffer words uploaded in one write
    CleanWords,        ///< SynchronizeBuffer skips clean tracker words
    IndexRebind,       ///< Repeated vkCmdBindIndexBuffer skipped
    UsageTracker,      ///< UsageTracker::Track skips pages known to be used
    BankCheck,         ///< Native 64 KiB banks: per-frame set checked first
    SplitOverlap,      ///< Quarantined: pending writes are not a proof of safe early MME reads
    FrontSleep,        ///< The split front sleeps instead of yielding while it waits
    SubmitBudget,      ///< 4096-draw native submission budget outside MoltenVK
    FragmentBarrier,   ///< FragmentBarrier deferred to the next draw that reads the pass
    FenceFlush,        ///< 5 ms DeferFlush, command list copies submitted at once
    FramesAhead,       ///< Ocarina presents a frame ahead
    PipelineRetention, ///< Sixteen sessions of used pipelines, saved every 15 s
    WriteIntersections,
    SortedPageBatches,
    AsyncPipelines,    ///< Native graphics SPIR-V emitted by the pipeline worker, not the back
    UnswizzleWorkers,
    Count,
};

inline constexpr std::array<std::string_view, static_cast<size_t>(Opt::Count)> opt_names{
    "bounds",         "method",          "cb_batch",    "clean_words",   "index_rebind",
    "usage_tracker",  "bank_check",      "split_overlap", "front_sleep", "submit_budget",
    "fragment_barrier", "fence_flush",   "frames_ahead", "pipeline_retention",
    "write_intersections", "sorted_page_batches", "async_pipelines", "unswizzle_workers",
};

inline bool Enabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("EDEN_NX2_PERF_OPT");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

inline bool On(Opt opt) {
    static const unsigned off = [] {
        const auto bit = [](Opt opt) { return 1U << static_cast<unsigned>(opt); };
        const auto names = [](const char* variable) {
            unsigned bits = 0;
            std::string_view list = std::getenv(variable) ? std::getenv(variable) : "";
            while (!list.empty()) {
                const size_t comma = list.find(',');
                const std::string_view name = list.substr(0, comma);
                bool known = false;
                for (size_t i = 0; i < opt_names.size(); ++i) {
                    if (name == opt_names[i]) {
                        bits |= 1U << i;
                        known = true;
                    }
                }
                if (!known && !name.empty()) {
                    LOG_WARNING(HW_GPU, "Nx2Perf: unknown name {} in {}", name, variable);
                }
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
            }
            return bits;
        };
        const unsigned default_off = bit(Opt::FramesAhead) | bit(Opt::CleanWords) |
                                     bit(Opt::IndexRebind) | bit(Opt::SubmitBudget) |
                                     bit(Opt::FenceFlush) | bit(Opt::FragmentBarrier) |
                                     bit(Opt::FrontSleep) | bit(Opt::AsyncPipelines) |
                                     bit(Opt::Bounds);
        unsigned mask = (default_off & ~names("EDEN_NX2_PERF_ON")) | names("EDEN_NX2_PERF_OFF");
        mask |= bit(Opt::SplitOverlap);
        if (const char* value = std::getenv("EDEN_NX2_PERF_FRAMES_AHEAD"); value && value[0] == '0') {
            mask |= bit(Opt::FramesAhead);
        }
        std::string state;
        for (size_t i = 0; i < opt_names.size(); ++i) {
            state += fmt::format(" {}={}", opt_names[i], Enabled() && (mask >> i & 1) == 0 ? "on" : "off");
        }
        LOG_INFO(HW_GPU, "Nx2Perf:{}", state);
        return mask;
    }();
    return Enabled() && (off >> static_cast<unsigned>(opt) & 1) == 0;
}

inline bool Verify() {
    static const bool verify = [] {
        const char* value = std::getenv("EDEN_NX2_PERF_VERIFY");
        return value != nullptr && value[0] == '1';
    }();
    return verify;
}

} // namespace VideoCommon::Nx2Perf
