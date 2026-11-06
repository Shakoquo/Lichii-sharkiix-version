# GPU backend profiling and parallel recording

## Phase 1: measure the existing pipeline

The current scheduler already overlaps resource resolution on GPUBack with Vulkan
recording on VulkanWorker. `draw record` measures the producer assembling callbacks,
not the driver's command encoding. The Ocarina sample has 1.7 ms/s in that zone
against 950 ms/s of back-thread CPU time. Star Fox has 5.6 ms/s against 650 ms/s.
Deleting those producer zones alone would save at most 0.18% and 0.86% of that CPU
work. Neither sample measures how much work another recording worker could remove.

Set `EDEN_NX2_GPU_PROFILE=1` and keep `EDEN_NX2_SPLIT_CHECK=1`. Every ten seconds,
the existing `per second` profile lines include fields prefixed `host` for the GPU
threads and VulkanWorker. The existing zone and `host_cpu_busy_ms_per_s` fields
remain available.

| Host phase | What it measures |
| --- | --- |
| `resolve` | Direct/indirect graphics preparation and compute dispatch preparation, excluding nested phases below. This includes pipeline lookup, resource/cache work, dynamic-state calculation and other host preparation. It is not a measure of pure cache lookup time. |
| `enqueue` | Construction and publication of scheduler commands inside `RecordWithUploadBuffer`. Captures evaluated before entering the function remain in preparation. |
| `encode` | Calls to `vkCmd*`, `vkBeginCommandBuffer` and `vkEndCommandBuffer` in the Vulkan wrappers, including upload, copy, barrier, query and compute commands. Forwarding helpers do not count twice. |
| `submit` | Calls to `vkQueueSubmit` and `vkQueueSubmit2`, excluding command encoding. |
| `worker other` | Callback execution and destruction excluding nested driver calls and explicit waits. This includes host descriptor updates, pipeline construction and any uninstrumented blocking inside callbacks. It is not parallelizable encoding. |
| `command pool` | Primary command-buffer acquisition and setup, excluding nested encoding and explicit waits. |
| `wait` | Scheduler worker idle waits, producer `WaitWorker`, and master-semaphore waits. Other mutex/driver waits remain in their containing phase. |

These are exclusive elapsed host times, not GPU execution times or CPU counters.
The separate per-thread CPU counter distinguishes runnable work from blocking.
The new phases overlap the old zone fields by design, so do not add the two
sets together. Within the host-phase fields, nested work is counted once. Work
outside the instrumented paths is not assigned to `resolve`; the old zone report
still shows methods, clears, decoding and other work. Sparse binding and presenting
are not queue-submit samples.

Report windows are per thread and may have different endpoints. Compare several
steady-state windows with the same scene and settings, after shader compilation.
Record frame-time distributions alongside them. Timing every driver call adds
overhead, so repeat the frame-time comparison with profiling unset. The checker
`python3 tools/nx2/check_host_gpu_profile.py` verifies the direct recording and
submission call sites are instrumented.

If encoding takes E ms/s, two perfectly balanced recording workers can remove at
most E/2 ms/s of that recording stage before coordination and repeated-state costs.
That is not an FPS prediction. Resolution and encoding already overlap, and more
recording workers cannot improve throughput when the ordered resolver is slower.
Submission, compute, copies, queries and uncertain dependencies will also remain
serial in the initial experiment, so the total encoding measurement is an upper
bound on the work eligible for graphics parallelism.

## Phase 2: immutable batches with serial replay

At checkpoint `d94ec5585d`, `EDEN_NX2_PARALLEL_RECORD=1` enables immutable command
capture with serial replay. Unset, `0`, or any other value keeps the original path.
That checkpoint tests correctness, not speed. The current implementation adds the
parallel encoder described below.

GPUBack remains the sole owner of guest resource resolution and buffer/texture
cache mutation. VulkanWorker still executes the existing callbacks in FIFO order,
including descriptor allocation, pipeline-build waits and query bookkeeping.
The immutable boundary follows those callbacks. Moving callbacks themselves to
workers would race shared allocators and borrowed descriptor storage.

`PassRecorder` captures resolved Vulkan values through the command-buffer wrapper.
`ResolvedCommand` is a closed variant. Pointer-bearing arguments become owned
arrays, including push-descriptor bytes whose size comes from the pipeline.
`ResolvedBatch` contains an immutable submission epoch, sequence number, compatible
render-pass inheritance, a complete state preamble and draw commands. It contains
no pointer to Maxwell state, guest memory or a cache object. Uploads, layout
transitions, barriers, compute and copies retain their original serial positions.

The state preamble is an exact history of state commands in the current primary
epoch. This preserves partial writes and pipeline/static-state interactions without
guessing at state invalidation rules. A batch covers up to 32 draws. History,
pending pass payload and cumulative replay cost are bounded; a budget overflow
selects serial recording for the remainder of the epoch. History is not a final
performance optimization: repeated state increases secondary encoding work.

The adapter delays the physical render-pass begin while it captures a candidate
pass. A late unsupported command materializes the prefix inline and keeps that
pass inline through its end. This emits the original clear/load operation once,
without an extra pass break. Queries, conditional rendering and transform feedback
prevent parallel capture while active. Unsupported persistent state and raw command
buffer escape disable capture for the entire primary epoch.

`WaitWorker` places a FIFO drain marker before waiting. An unfinished captured pass
becomes inline at that marker, so descriptor-arena reuse cannot wait for an end-pass
command that the blocked producer has not emitted. Frame retirement also drains the
worker before rotating payload arenas or releasing cache objects.

The submission epoch is the existing primary's signal value, checked against the
ordered submit callback. Batches never leave that submission or extend references
past its existing GPU retirement conditions. Cache-owned resources retain their
existing timeline/ring ownership; the batch does not pretend a raw handle is an
owning reference. The frame/drain gates enforce CPU recording completion before
retirement, and submission order remains unchanged.

With profiling enabled, `host resolved passes`, `host resolved batches`,
`host inline passes` and `host budget fallbacks` show the path taken. The parallel
encoder also reports `host parallel passes` and `host parallel batches`.

## Phase 3: parallel secondary recording

The same switch enables capture and starts two recording workers only when a pass
first qualifies for parallel recording. Each worker owns its command pool and
secondary buffers. VulkanWorker remains the
sole primary recorder and submitter; recording workers never call the scheduler,
read guest memory or mutate resource caches.

Passes with fewer than two batches stay inline because they offer no recording
parallelism. This test runs before copying initial-state snapshots into batches.
An eligible pass reserves all its secondary slots before dispatch. The queue holds
at most eight waiting jobs, with 256 retained secondary buffers across the two
workers. If a whole pass cannot fit, it replays inline instead of waiting for its
own unsubmitted GPU tick. Each slot carries the primary submission's explicit tick
and can be reused only after that tick completes on the GPU.

Workers can finish out of order. VulkanWorker waits for every job and executes the
secondary handles in original order, using one physical render-pass begin and end.
It restores the complete logical state onto the primary afterward because
`vkCmdExecuteCommands` invalidates primary command-buffer state. Callback side
effects are never repeated. Recording exceptions propagate rather than pretending
that a device loss was a successful serial fallback.

This first implementation records batches in parallel within one pass. It joins
them at the pass boundary, not at a later submission. Shutdown joins the recording
threads and waits for the last successful submission before destroying their pools.
Unsupported commands, active queries and exhausted budgets still use serial
recording. A low eligible-pass fraction or repeated-state overhead can make the
experiment slower; no game-speed improvement follows from compiling it.

### Cheap fallback when no parallel work is available

The Ocarina and Star Fox harness runs on `a1e3570839` executed no parallel batches.
Star Fox's worker-other time increased from 0.68 to 3.68 ms/frame while it kept
copying state for inline passes. That work is unnecessary once the production
policy commits the current submission to serial recording.

Production now stops retaining state after a known query, conditional,
transform-feedback, unsupported-command, raw-handle, drain or budget fallback.
The pending prefix is replayed once, then wrappers forward values directly for the
rest of that primary submission. Inline render-pass begins forward the caller's
clear array without copying it. A fresh primary restores capture eligibility.
This is deliberately conservative: a later otherwise-eligible pass in that same
submission remains serial. Empty and short passes do not disable later eligibility.

The collector's `retain_inline_state` option preserves its original same-submission
recovery contract for callers that need it; production selects the cheaper policy.
`host resolved passes/batches` count snapshots actually constructed, so small-pass
preflight rejection appears under `host inline single batch` without increasing
those counts. Later bypassed passes inherit the epoch's first disabling reason.
There is still a small recorder check before direct calls and some work before the
first unsupported operation. A game run must measure the residual cost; the change
does not claim a mathematically zero-cost enabled switch.

## Verification

The full tests binary includes deterministic capture/replay and queue tests.
They cover owned payloads, overlapping state updates, late fallback, an open-pass
drain, reversed worker completion, capacity exhaustion, explicit tick retirement,
exception draining and shutdown.

The opt-in headless test uses synthetic shaders and offscreen readback. It compares
serial, captured and parallel pixels, requires four actual secondary buffers for
97 draws, exercises query/indirect/capacity fallback and repeats pool reuse after
retirement. A second pass in the same primary uses restored state without rebinding.
The test requires a Vulkan device and Khronos validation when enabled;
otherwise it reports a skip rather than claiming GPU coverage.

```sh
EDEN_TEST_VULKAN=1 \
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json \
VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT \
build/bin/tests
```

Use the installed ICD manifest on other systems. The production device-selection
policy is not part of this fixture; it creates a minimal graphics device so unrelated
optional extensions do not hide recorder validation failures.

Further resolution parallelism needs separate measurements. Guest reads, uploads,
hazards and cache mutation remain ordered; per-stage tasks would need snapshotted
inputs and must not mutate shared caches.

## Phase 4: attribute and reduce GPUBack work

Ocarina's stationary Kokiri chest run on package `a1e3570839` measured 41.882 FPS
with recording off and 41.816 FPS with it on. The ON run produced about 83 resolved
passes and 83 resolved batches per second, but zero parallel batches. Every eligible
pass was a single batch, so the minimum-two-batch rule excluded all of them. There
were no budget fallbacks. The draw trace reported no indirect draws and substantial
occlusion-query activity, but the original counters could not assign each remaining
inline pass to its cause.

The profiler now assigns each completed inline pass one first-cause counter:
active/late query, conditional rendering, transform feedback, indirect/mesh,
timestamp/capture, serial command, unsupported state, explicit drain, no draws,
budget, single batch or unavailable encoder. These reason counts sum to
`host inline passes`; they do not change eligibility or thresholds.

Five complete OFF windows ending at 106.56–146.56 seconds, normalized by 41.815
presents/second, give this ordering. These are exclusive elapsed times, not CPU
samples or promises of savings.

| Work | ms/present | Safe direction |
| --- | ---: | --- |
| Method processing | 6.650 | Reduce redundant dispatch for proven state-only writes; preserve executable methods and shadow/deferred-draw semantics. |
| Other | 4.951 | Separate deferred callbacks and ring/profiler overhead before changing them; mappings, cache flushes and completion signals must stay ordered. |
| Split wait | 2.832 | Separate spin, idle-hook work and sleeping; removing spin may save CPU without improving latency. |
| Compute preparation | 1.381 | Keep shared cache mutation and guest-memory dependencies ordered. |
| Stage-buffer binding | 1.367 | Coordinate with the buffer-cache work; do not parallelize mutable bindings. |
| Fence preparation | 1.263 | Reduce unnecessary download work without weakening visibility. |
| Buffer updates | 0.891 | Avoid redundant cache work rather than introduce concurrent mutation. |
| Flush work | 0.867 | Preserve submission and writer visibility when reducing publication overhead. |
| Handle reads | 0.624 | Snapshot live inputs before considering parallel evaluation. |
| Remaining zones | 3.088 | Measure their individual shares before adding complexity. |

The sum is 23.914 ms elapsed per present, against 22.516 ms of measured thread CPU.
The broad `host resolve` field overlaps this table and must not be added again.
The profiler does not rescale elapsed values to manufacture a CPU breakdown.
Native CPU sampling is needed for fine-grained CPU attribution; per-method OS CPU
clock reads would be too expensive at roughly 3.35 million calls per second.

New exclusive zones separate `method state`, `method multi`, `method sink`,
`split call`, `split dispatch`, `split spin`, `split idle hook` and `split sleep`.
Compare the sum of all method zones with the old `method` total. `split dispatch`
includes ring processing, sink enqueueing and nested-scope bookkeeping not charged
to child scopes. The deferred-call zone ports the attribution from Eglantine
PR #134, commit `a21c9b5d9f`. The split's waits and publication ordering are unchanged.

`EDEN_NX2_METHOD_FAST=1` selects a specialized entrypoint only for non-executable
Direct macro state writes after the pending method sink is consumed. It removes
redundant macro/execution dispatch checks, not guest commands. Write history,
profiling counters, shadow RAM, unchanged-value filtering, dirty flags and deferred
draw flushing remain equivalent to `CallMethod`. Executable writes, including
constant-buffer data, query and semaphore methods, stay on the original path.
Unset or any value other than `1` leaves the experiment disabled.

For the next A/B, keep `EDEN_NX2_PARALLEL_RECORD=0` in both runs and change only
`EDEN_NX2_METHOD_FAST`. `fast state calls` reports its use; the new `method state`
zone measures its eligible workload. Repeat with GPU profiling unset to check
observer overhead. The method table's 6.650 ms is an upper bound on the whole
method family, not the expected saving from this narrower experiment.

The later uninstrumented chest baseline measured 43.38 FPS and 23.05 ms mean,
against the instrumented 23.87 ms. GPUBack and the front each used about 795 ms CPU
per second without zones. Treat that front/back pair together; saturation in the
instrumented GPUBack sample does not establish saturation without profiling.

Upstream `nx2` was already contained in the phase-1 base `99ac3d5955`.
Eglantine PR #134 adds producer-side profiling but does not measure actual Vulkan
encoding or submission separately. Its diff and PR #593's split-drain changes
were inspected; neither implements immutable batches or parallel recording.
