# Bananza timing trace reference

`EDEN_DKB_FRAME_TRACE=1` enables the `DKB frame`, `DKB renderer`, `DKB cpu`, `DKB host_pace`, and
`DKB gpu_frame` records. The value must be exactly `1`. The process caches it on first
use. Unset, empty, `0`, and other values disable these diagnostics. The flag does not
select a title, change a swap interval, bypass a wait, or enable a rendering workaround.

The records use the existing logger at Info level. `DKB frame` uses the Service category.
The Vulkan records use Render_Vulkan. A log filter that excludes those categories also
excludes these records. Existing diagnostics and their switches remain independent.

## Guest presents and pacing

`DKB frame` samples one completed `nx2:vi2` present at most once per second per session.
Times describe that present, not an average over the reporting interval. Requested
timing changes are counted on every present, even between samples and while speed
limiting is disabled.

| Field | Meaning |
| --- | --- |
| `present` | Session-local present counter. |
| `word12` | Raw present command word 12. |
| `requested_interval`, `requested_hz` | Raw high and low 16-bit halves of word 12. |
| `effective_interval`, `effective_hz` | Values selected by the existing pacing calculation. The interval is clamped to 1 through 4 unless overridden. Refresh rates outside 30 through 240 become 60 Hz. These values do not imply that pacing is enabled. |
| `forced_interval` | Parsed `EDEN_NX2_SWAP_INTERVAL`, with zero meaning no override. |
| `speed_limit`, `speed_percent` | Current speed-limit setting and percentage. Disabled speed limiting or zero percent skips guest vblank pacing. |
| `switches` | Number of raw word-12 changes since the preceding trace record. The first request is not a switch. |
| `last_switch` | Most recently observed old and new raw timing words. Zero to zero means none has been observed. This is not a lossless event stream. |
| `cpu_frame_wall_ms` | Wall time between consecutive present-handler entries. The first sample is `-1`. Includes guest execution, scheduling, and waits between entries. It is not CPU utilization or active CPU execution time. |
| `cpu_present_wall_ms` | Wall time from handler entry through completion of guest pacing. Excludes trace logging, the existing statistics log, and reply construction. |
| `fence_wall_ms` | Wall time in the existing current or previous guest-fence wait policy. This is a host wait, not a hardware GPU duration. |
| `composite_wall_ms` | Wall time in the service's `Composite` call. This is not the Vulkan renderer's device execution time. |
| `target_ms` | Requested guest pacing period after interval, refresh, and speed percentage. Zero when guest pacing is bypassed. |
| `sleep_requested_ms` | Duration supplied to the guest-kernel sleep, before its existing 2 ns deadline adjustment. Zero when no sleep occurs. |
| `sleep_actual_wall_ms` | Host steady-clock elapsed time around that guest-kernel sleep call, including descheduling and wake-up latency. Zero when no sleep occurs. |
| `pace_wall_ms` | Wall time for the whole `PaceToVblank` call, including calculation and existing logs. |
| `deadline_reset` | The sampled call discarded its old deadline because it was more than one selected interval late. The first paced present normally resets the uninitialized deadline too. |

A raw request of interval 2 at 60 Hz requests one frame per two refreshes, nominally
30 fps at 100% speed. It establishes the requested cadence, not why the game chose it.
A 33 ms CPU interval with a raw interval-1 request does not establish a game-requested
30 fps cap. Fence waits, CPU work, host presentation, and pacing must be examined
separately. A sampled reset or a long GPU duration alone does not identify a cause.

## Renderer CPU timing and host pacing

`DKB renderer` samples one visible Vulkan `Composite` call at most once per second per
renderer. `renderer_frame` counts visible composites, starting at 1. It is independent
of the service's present counter. Hidden-window composites do not advance it.

`cpu_frame_wall_ms` measures entry-to-entry wall time, with `-1` for the first visible
frame. `cpu_composite_wall_ms` measures that call through `TickFrame`, excluding trace
logging and the final `OnFrameDisplayed` callback. The phase fields divide that interval:

- `capture_wall_ms` covers applet capture and screenshots before frame acquisition.
- `acquire_frame_wall_ms` covers `GetRenderFrame`, including waiting for free frame resources.
- `record_submit_wall_ms` covers compositor command recording and submission. It includes
  generated-frame processing when enabled, and any waits those calls perform.
- `present_call_wall_ms` covers `Present`. With asynchronous presentation this queues work.
  Without it, this can include swapchain acquisition, pacing, and presentation.
- `frame_end_wall_ms` covers the subsequent dispatch, frame-end notification, and `TickFrame`.

These are wall-clock CPU scopes, not active CPU times. In particular, acquisition and
presentation waits are not renderer computation. Asynchronous presentation happens on
another thread and is not measured by `present_call_wall_ms`.

`host_pacing_mode` and `vsync_mode` are the numeric configured settings from
`src/common/settings_enums.h`, not measured display refresh or actual Vulkan present mode.
`DKB host_pace` reports the separate scheduler pacer, at most once per second per calling
thread, when a positive host target FPS is supplied. Auto mode supplies zero and does
not emit this record. Android API 30 and later bypass this host pacer.

`target_fps` is the host target, not the guest request. `enabled` is the speed-limit
setting. `may_flush` distinguishes the recording-thread path from the asynchronous
presentation path. `cpu_wait_wall_ms` covers the pre-pacing submission and GPU-tick wait.
`pace_requested_ms` is the remaining time to the host pacing deadline.
`sleep_requested_ms` is the host `sleep_for` portion, excluding the existing spin tail.
`sleep_actual_wall_ms` measures that sleep. `pace_actual_wall_ms` measures all pacing,
including the yield loop and scheduling overhead. Disabled pacing has zero requested
sleep and only the small elapsed cost of checking the setting.

## Host-thread CPU execution time

`DKB cpu` reports OS-accounted CPU execution time, separate from the wall-clock scopes
above. The Vulkan rasterizer samples registered host threads at most once per second
at `TickFrame`. The first snapshot establishes a baseline and does not emit a window.
`through_frame` is the rasterizer's frame counter at the end of the window. `frames`
is the number of frame ticks since the previous snapshot. `window_wall_ms` measures
the actual steady-clock interval, which can exceed one second if frames stall.

For each role, `<role>_cpu_ms` is its user-mode plus kernel-mode CPU-time increase over
the window. `<role>_cpu_ms_per_frame` divides that increase by `frames`. This is an
average over the window, not the CPU execution time of a particular frame. Thread
clock reads occur sequentially within a snapshot, so their boundaries have a small
read-time skew. CPU execution in the window can belong to work queued for other frames.

| Role | Host thread measured |
| --- | --- |
| `guest_core_0_host` through `guest_core_5_host` | The six multicore guest-core host threads. Includes guest JIT execution, JIT compilation, emulated kernel work, services, and other work run on those host threads. Not CPU time for individual guest threads, which can migrate between guest cores. |
| `guest_single_core_host` | The single host CPU thread used in single-core mode. Its time cannot be split into six guest-core measurements. The multicore roles are inactive in this mode. |
| `gpu_command` | The host GPU command-processing thread, or the front thread when the GPU split is active. |
| `gpu_back` | The GPU split's back thread, when active. Includes engine methods, rasterizer work, and cache work executed there. |
| `vulkan_worker` | The scheduler worker that records Vulkan commands and submits them. It is not the GPU device. |

An unregistered role is `inactive`. An active role is `unavailable` if the platform
does not support its clock, a clock read fails, the thread changed between snapshots,
or its counter regressed. Neither state is zero CPU time. A role's generation changes
when it is registered again, preventing a delta across thread or emulation lifetimes.
These roles do not cover every emulator thread, such as compiler-pool, presentation,
audio, and driver helper threads, and their sum is not total process CPU usage.

Windows uses `GetThreadTimes`, adding user and kernel time and converting 100 ns units
to nanoseconds. Registration duplicates the current thread's pseudo-handle into an
owned real handle, so later reads from the rasterizer still measure the registered
thread. Linux uses `pthread_getcpuclockid` at registration and `clock_gettime` at each
snapshot. Linux-based Android uses the same implementation. Only these Windows and
Linux paths are implemented. Other targets explicitly return unavailable.

Registration and removal hold the same mutex as snapshot reads. A registration remains
on the owning thread's stack until that thread exits its entry function. Removal occurs
before the Windows handle closes or the Linux owner thread exits, preventing a reader
from using an expired clock. Disabled frame tracing registers no clocks and opens no
thread handles. There are no per-instruction, per-draw, or guest-core polling reads.

OS CPU accounting excludes time spent asleep or descheduled, but counts active spinning
and CPU work performed inside driver calls. Its accounting resolution can be coarser
than the nanosecond representation, especially on Windows. Do not interpret small
window-to-window changes as exact scheduling boundaries or assume that a busy thread
is necessarily on the frame's critical path.

With both `EDEN_DKB_FRAME_TRACE=1` and `EDEN_NX2_GPU_PROFILE=1`, the existing GPU-thread
profiler supplies its exclusive wall-time subzones, including draw, pipeline lookup,
buffer work, texture work, and waits. Those zones remain behind their existing flag
and are not CPU-busy measurements. Its existing roughly ten-second report now also
includes `host_cpu_busy_ms_per_s`, measured with the current thread's OS CPU clock,
and `window_wall_ms`. The CPU value is normalized to milliseconds of CPU execution
per second of that wall window. Unsupported or failed readings report `unavailable`.
Use log timestamps and the printed window lengths to compare these longer windows
with `DKB cpu`; the two reports do not share exact frame boundaries. None of the
service, renderer, or profiler-zone wall metrics has been relabeled as CPU execution.

## GPU timestamps grouped by renderer frame

`DKB gpu_frame` reports a sampled frame's GPU timeline span and sum of GPU submission
intervals, not CPU elapsed time. It uses Vulkan timestamp queries on the scheduler's graphics queue.
Each interval begins at TOP_OF_PIPE in the submission's upload command buffer and ends
at BOTTOM_OF_PIPE in its main command buffer. The sum includes uploads, guest commands,
and compositor work submitted through this scheduler between two renderer boundaries.

A renderer boundary is the final flush for a visible `Composite`, before its `Present`
call. Frame 1 is not sampled because its preceding boundary is unknown. Each later sample
belongs to the `renderer_frame` named in the record. Frame generation's intermediate
flushes belong to the same renderer frame. Separate present-manager command buffers,
presentation, and scanout are outside these GPU intervals.

| Field | Meaning |
| --- | --- |
| `renderer_frame` | Frame whose scheduler submissions were sampled. CPU and service sampling are independent, so a CPU record for this exact frame is not guaranteed. |
| `last_submission_tick` | Scheduler completion tick of the last measured submission. |
| `gpu_frame_span_ms` | GPU timestamp difference from the first submission's begin to the final submission's end. `-1` means the submission cap truncated the sample, so a full-frame span is unavailable. |
| `gpu_submission_sum_ms` | Sum of the available timestamp differences, multiplied by the device's nanoseconds-per-tick value. |
| `measured_submissions` | Number of submission intervals in the sum. |
| `dropped_submissions` | Submissions omitted because this frame exceeded the fixed 512-pair capacity. A nonzero value means the frame's sum is incomplete. |
| `valid_bits`, `period_ns` | Queue-family timestamp width and physical-device timestamp period used for conversion. |

The span includes gaps between submissions, queue dependencies, and stalls within that
GPU timeline interval. It does not include presentation or scanout. The sum omits gaps
between submissions, but its intervals can overlap because submissions can execute in
a pipeline. Those intervals can also include dependencies or stalls. Neither measurement
is GPU utilization. Do not subtract either value from CPU frame wall time to derive CPU
work. Timestamp differences account for the queue's valid-bit wrap, but
intervals longer than a full hardware-counter wrap cannot be disambiguated.

Only one sampled frame can be outstanding. The next sample starts at a renderer boundary
at least one second after the preceding result is reported. A slow or stalled queue skips
later frames rather than waiting for diagnostic results. The worker checks the existing
completion tick, then reads results with 64-bit values and availability bits, without
`VK_QUERY_RESULT_WAIT_BIT`. Unavailable results stay pending. Queries are reset only
after their prior results have been read and their submission has completed. The worker
is drained and joined at teardown, and the last query-using submission completes before
the query pool is destroyed. No extra runtime flush or GPU wait is added for timestamps.

Unsupported timestamps or query-pool allocation failure produce one unavailable message.
A query-read error disables further GPU timing and logs the error once. Missing GPU
records are not zero-duration frames. Startup, shutdown, a hidden window, an incomplete
frame, unavailable results, or an unsupported device can leave no completed sample.

## Existing controls

- `EDEN_NX2_SWAP_INTERVAL=<n>` forces a nonzero guest interval. It is not a trace switch
  and can change emulation speed. Zero leaves the guest interval selection in effect.
- `EDEN_NX2_FRAMES_AHEAD` defaults to enabled. A value starting with `0` restores waiting
  for the current frame. The default waits for the previous frame and also waits for
  the current one when the previous fence is behind. Trace logging does not change it.
- `EDEN_NX2_GPU_PROFILE`, enabled by presence even with value `0`, reports host GPU-thread
  zones and scheduler counters every 10 seconds. Its zone milliseconds remain CPU-thread
  wall durations, not hardware GPU timestamps. The separate `host_cpu_busy_ms_per_s`
  field uses OS CPU accounting.
- The existing swap-word change logs and 600-present summaries remain unchanged.
  Their rate is separate from the opt-in trace's one-second limits.

No game run is part of the instrumentation's automated checks. The helper tests cover
rate limiting and valid-bit timestamp subtraction, including wrap. Device-specific
runtime validation and a user's trace are still required before attributing Bananza's
30 fps behavior to the game, pacing, CPU work, or GPU work.

The focused Catch2 filter is `[dkb_frame_trace]`. The flag test also runs in separate
processes with the variable unset and with values empty, `0`, `1`, `10`, and `true`,
because each process caches the flag on first use.

The `[dkb_cpu_trace]` tests cover clock monotonicity and positive CPU execution, reads
from another thread, registration removal and replacement, unavailable or regressing
counters, disabled registration, and concurrent snapshots during unregister. Enabled
and disabled flag cases run in separate processes. They do not assert a fixed ratio
between wall time and CPU execution under scheduler load.
