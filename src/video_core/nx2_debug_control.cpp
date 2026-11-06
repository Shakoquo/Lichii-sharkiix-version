// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>

#include "video_core/nx2_debug_control.h"

namespace VideoCore::Nx2Debug {
namespace {

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<CaptureRequest> captures;
    std::unordered_map<u64, CaptureResult> finished;
    std::vector<u64> rejected;
    u64 reject_generation{};
    u64 applied_generation{};
    bool reject_pending{};
    bool unrejected{};
    bool open{};
};

State state;
std::atomic<bool> pending{};
std::atomic<bool> any_rejected{};

void UpdatePending() {
    pending.store(!state.captures.empty() || state.reject_pending, std::memory_order_relaxed);
}

} // Anonymous namespace

void BeginSession() {
    std::scoped_lock lock{state.mutex};
    state.captures.clear();
    state.finished.clear();
    state.rejected.clear();
    state.reject_pending = false;
    state.unrejected = false;
    state.applied_generation = state.reject_generation;
    state.open = true;
    any_rejected.store(false, std::memory_order_relaxed);
    UpdatePending();
}

void EndSession() {
    {
        std::scoped_lock lock{state.mutex};
        state.open = false;
        state.captures.clear();
        UpdatePending();
    }
    state.changed.notify_all();
}

bool HasRequests() noexcept {
    return pending.load(std::memory_order_relaxed);
}

void RequestCapture(CaptureRequest request) {
    std::scoped_lock lock{state.mutex};
    state.captures.push_back(std::move(request));
    UpdatePending();
}

void CancelCapture(u64 id) {
    std::scoped_lock lock{state.mutex};
    std::erase_if(state.captures, [id](const CaptureRequest& request) { return request.id == id; });
    state.finished.erase(id);
    UpdatePending();
}

std::optional<CaptureResult> WaitCapture(u64 id, std::chrono::milliseconds timeout) {
    std::unique_lock lock{state.mutex};
    state.changed.wait_for(lock, timeout, [id] { return !state.open || state.finished.contains(id); });
    const auto it = state.finished.find(id);
    if (it == state.finished.end()) {
        return std::nullopt;
    }
    CaptureResult result = std::move(it->second);
    state.finished.erase(it);
    return result;
}

std::optional<CaptureRequest> TakeCapture() {
    std::scoped_lock lock{state.mutex};
    if (state.captures.empty()) {
        return std::nullopt;
    }
    CaptureRequest request = std::move(state.captures.front());
    state.captures.pop_front();
    UpdatePending();
    return request;
}

void FinishCapture(u64 id, CaptureResult result) {
    {
        std::scoped_lock lock{state.mutex};
        if (!state.open) {
            return;
        }
        state.finished.insert_or_assign(id, std::move(result));
    }
    state.changed.notify_all();
}

u64 SetRejected(std::span<const u64> hashes, bool rejected) {
    std::scoped_lock lock{state.mutex};
    bool changed = false;
    for (const u64 hash : hashes) {
        const auto it = std::ranges::find(state.rejected, hash);
        if (rejected && it == state.rejected.end()) {
            state.rejected.push_back(hash);
            changed = true;
        } else if (!rejected && it != state.rejected.end()) {
            state.rejected.erase(it);
            state.unrejected = true;
            changed = true;
        }
    }
    any_rejected.store(!state.rejected.empty(), std::memory_order_relaxed);
    if (changed) {
        ++state.reject_generation;
        state.reject_pending = true;
        UpdatePending();
    }
    return state.reject_generation;
}

std::vector<u64> RejectedHashes() {
    std::scoped_lock lock{state.mutex};
    return state.rejected;
}

bool IsRejected(u64 hash) {
    if (!any_rejected.load(std::memory_order_relaxed)) {
        return false;
    }
    std::scoped_lock lock{state.mutex};
    return std::ranges::find(state.rejected, hash) != state.rejected.end();
}

bool AnyRejected() noexcept {
    return any_rejected.load(std::memory_order_relaxed);
}

std::optional<RejectChange> TakeRejectChange() {
    std::scoped_lock lock{state.mutex};
    if (!state.reject_pending) {
        return std::nullopt;
    }
    const RejectChange change{.generation = state.reject_generation,
                              .unrejected = state.unrejected};
    state.reject_pending = false;
    state.unrejected = false;
    UpdatePending();
    return change;
}

void NoteRejectApplied(u64 generation) {
    {
        std::scoped_lock lock{state.mutex};
        state.applied_generation = (std::max)(state.applied_generation, generation);
    }
    state.changed.notify_all();
}

bool WaitRejectApplied(u64 generation, std::chrono::milliseconds timeout) {
    std::unique_lock lock{state.mutex};
    return state.changed.wait_for(lock, timeout, [generation] {
        return !state.open || state.applied_generation >= generation;
    }) && state.applied_generation >= generation;
}

} // namespace VideoCore::Nx2Debug
