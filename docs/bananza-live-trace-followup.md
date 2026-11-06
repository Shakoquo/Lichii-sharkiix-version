# Bananza live trace follow-up

The supplied Windows log is from `8644f32fba`, with a Ryzen 9 5900X,
RTX 3080 and NVIDIA driver 610.88. The settings show multicore Dynarmic,
1x resolution, asynchronous GPU emulation and Low GPU accuracy.

## What the capture establishes

The guest does change its requested swap interval. The log contains 74 interval
lines, including interval 3 during loading and repeated interval 1/2 transitions
during later gameplay. For example, present 3961 requests interval 2 at 133.739 s,
and present 3973 requests interval 1 at 134.256 s. The interval is not always 1.

Every sampled service present after 90 s requests zero pacing sleep. The existing
guest vblank pacing path therefore does not impose the measured cadence in those
samples. This does not establish why the guest changes its requested interval.

Across the 16 samples after 120 s, medians are:

| Measurement | Median |
| --- | --- |
| Time between service presents | 42.477 ms |
| Service present handler | 3.867 ms |
| Fence wait inside that handler | 3.859 ms |
| GPU first-to-last timestamp span | 42.155 ms |
| Sum of GPU submission intervals | 5.196 ms |
| Submissions per sampled renderer frame | 92 |
| Draw attempts per rasterizer frame | 5,374.375 |
| Zero-count direct draws per rasterizer frame | 349 |

The draw values divide each summary's totals by its rasterizer-frame delta. The
new build prints that denominator and those averages directly. GPU span minus
submission sum is not CPU busy time, and neither number is GPU utilization.
The capture supports investigating host-side gaps, but contains no Nx2Profile
zone reports and does not distinguish guest-core work from GPU-command work.

Nine compute-creation errors say `SPIR-V Instruction is not implemented`, first
at 36.198 s. They occur before Vulkan shader-module creation. The generic
`Native compute pipeline unavailable` message on subsequent dispatches comes
from a cached failed build, not proof of an asynchronous cache miss.
The old messages do not identify the IR opcode, and the QMD descriptors do not
contain its instruction stream. No specific missing F2I/LDC/VOTEU form can be
deduced from this capture. See [compute investigation](bananza-compute-followup.md).

`NativeQmdScheduler::Signal` still marks a failed dispatch complete and performs
its releases. This capture has no actual four-second frame-fence timeout. Its
five `ChannelSetTimeout` lines configure timeouts; they are not timeout events.

## Next Windows run

Restart from a PowerShell shell with:

```powershell
$env:EDEN_DKB_FRAME_TRACE = "1"
$env:EDEN_NX2_GPU_PROFILE = "1"
Remove-Item Env:EDEN_DKB_INDIRECT_READBACK -ErrorAction SilentlyContinue
Remove-Item Env:EDEN_NX2_SWAP_INTERVAL -ErrorAction SilentlyContinue
& .\eden.exe
```

Use the actual executable filename and log filter `*:Info`. Keep the same game
scene and settings. Hold each troublesome/recovered camera direction for at least
20 seconds so that both directions contain ten-second zone reports. Preserve
the full log from startup, including the first compute failure, and note the
camera-transition times. No shader-cache deletion is required for failed builds.
The existing zone profiler adds overhead. For an absolute-FPS comparison, repeat
with `EDEN_NX2_GPU_PROFILE` unset while keeping `EDEN_DKB_FRAME_TRACE=1`.

The next log should show the exact unsupported IR opcode and retained compute
failure reason, `DKB cpu` host-thread CPU counters and per-frame averages, and
the existing `GPU thread per second` / `GPU back thread per second` zone reports.
The shader opcode selects the rendering fix. The busy-thread and zone breakdown
selects a performance patch; none of the reviewed performance PRs is yet justified
as the slowdown fix. See [candidate audit](bananza-performance-candidates.md).

These changes improve diagnosis, not known shader execution or frame rate. No
game was run in the cloud, and Windows runtime validation remains owner-side.

A Linux overhead check using the real implementation read ten captured role
clocks 10,000 times, averaging 5.712 microseconds per snapshot. All ten roles were
owned by that benchmark thread. This checks local sampling cost, not Windows
overhead or game performance; production snapshots run at most once per second.

The integrated Clang 20/Ninja headless build passed all 486 test cases with
tracing disabled and enabled. Focused tests cover retained shader failures,
cross-thread CPU-clock reads, generation changes and unregister races.
