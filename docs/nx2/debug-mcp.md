# Debug server and MCP adapter

Agents debug games through a slow loop today: write env flags, ask the owner's PC to build a
package, wait for the owner to launch the game, press a hotkey and send the log back, then copy
dumps. Every new question costs a relaunch, and most cost a rebuild, because every
`EDEN_NX2_*` switch is read once at startup.

This document designs an opt-in debug server inside Lichii and an MCP adapter that agents run on
the PC, so an agent can ask a running game questions directly. Phase 1 is implemented on the
`feat/debug-mcp` branch; the later phases are proposals with the hook points they need.

Line numbers refer to `feat/debug-mcp` at the time of writing.

## Goals and constraints

- The owner's PC (RTX 3080, Windows) runs the game. Agents reach it through bash on a paired
  device: they can run programs and read files there, but cannot see the screen or press keys.
- Game files, keys and firmware never leave that PC, and the server must not give a way to read
  them.
- A normal run must not change: the server is off unless `EDEN_NX2_DEBUG_SERVER` is set, and
  while it is off the only cost is one relaxed atomic load per presented frame.
- No relaunch for a new question, and as few rebuilds as possible for a new tool.

## Architecture

```
agent ──stdio MCP──> tools/nx2/debug_mcp.py ──TCP 127.0.0.1, JSON lines──> Lichii
  (on the PC)          (Python, stdlib only)                               ├─ "DebugServer" thread
                                                                           │    tools: status, screenshot, log_*, ...
                                                                           └─ rasterizer thread
                                                                                takes requests at each frame end
```

### In-process server

`src/core/debugger/nx2_debug_server.cpp` is the transport. It listens on a loopback TCP port
with Boost.Asio, as the GDB stub does (`src/core/debugger/debugger.cpp:147`), and runs one
`io_context` on its own thread named `DebugServer`. Each line a client sends is one request,
`{"id":1,"method":"status","params":{}}`; each answer is one line,
`{"id":1,"result":{...}}` or `{"id":1,"error":"..."}`. The transport knows nothing about the
emulator: it maps method names to handlers. That keeps it unit-testable without a `System`.

`src/core/debugger/nx2_debug_tools.cpp` holds the tools. `Core::System::Impl::Load` starts the
service once a game has loaded (`src/core/core.cpp:476`), and `ShutdownMainProcess` stops it
before anything else (`src/core/core.cpp:481`). The member is declared last
(`src/core/core.cpp:610`), so it is also destroyed first. The tools therefore never see a
half-built or half-destroyed system, and the server works the same under the Qt frontend and
`eden-cli`.

Why not reuse the GDB stub? It speaks the GDB remote protocol and parks emulation on every stop
(`src/core/debugger/debugger.cpp:82`). It is the right tool for guest breakpoints and stays as it
is (`tools/nx2/gdbrsp.py` drives it), but it has no place for frames, shaders or logs. The new
server copies its threading model, not its protocol.

### Why JSON lines over TCP, with a separate adapter

- MCP's own transports are stdio, which needs the agent to launch the process (it cannot launch
  the emulator, which the owner starts), and Streamable HTTP, which needs sessions and
  server-sent events. Neither fits inside the emulator cheaply.
- A JSON-lines socket is about 200 lines of C++ with libraries Lichii already links (Boost.Asio,
  nlohmann::json, both already used by `core`). Anything can drive it: the adapter, a test,
  or `python -c`.
- The adapter owns everything that is MCP: tool descriptions, input schemas, image content, error
  wording. Those change often, and changing them needs no emulator rebuild. That is the point of
  the whole exercise.
- Loopback TCP works the same on Windows and Linux. Named pipes or Unix sockets would need two
  code paths.

### Threading

Three kinds of work, three places:

1. **Server thread.** Parses requests and runs handlers. Handlers that only read thread-safe
   state run here directly: status, log tail, log filter. Handlers run one at a time, so a slow
   request (a capture can take seconds) delays the next one. One agent at a time is the expected
   use.
2. **Rasterizer thread.** Everything that touches the texture cache, pipeline cache, scheduler or
   frame dump must run on the thread that owns them. With the GPU thread split
   (`src/video_core/gpu_split.h:60`), that is the back thread, which also runs
   `RendererVulkan::Composite` and therefore `rasterizer.TickFrame()`
   (`src/video_core/renderer_vulkan/renderer_vulkan.cpp:269`). Requests for it go through
   `src/video_core/nx2_debug_control.h`, a small mutex-guarded block with one atomic "something
   is pending" flag. `RasterizerVulkan::TickFrame` (`src/video_core/renderer_vulkan/vk_rasterizer.cpp:1817`)
   checks the flag and takes the requests at the end of each presented frame. Between frames no
   draw is half-recorded and no pipeline is in use by the caller, so dropping cache entries there
   is safe.
3. **Existing renderer hooks.** The screenshot uses `RendererBase::RequestScreenshot`
   (`src/video_core/renderer_base.cpp:38`), the path the Qt screenshot hotkey uses
   (`src/yuzu/bootmanager.cpp:819`). It is already safe to call from another thread, and the
   renderer fills the buffer at the next present (`renderer_vulkan.cpp:350`).

The server thread waits for rasterizer-thread work with a timeout (5 s for screenshots and
rejections, 120 s by default for a capture). If the game is paused or loading, no frame is
presented and the tool fails with "no frame was presented within N s (paused or loading?)"
instead of hanging. A withdrawn capture request is removed from the queue. On shutdown the
service ends the session first, which wakes every waiter, then stops the `io_context`, so
shutdown waits at most about 50 ms for a handler.

Cost when unused: if `EDEN_NX2_DEBUG_SERVER` is unset, `MaybeStartService` returns at its first
`getenv`. No thread, no socket, no log ring. The per-frame check is one relaxed atomic load. The
runtime reject check in the lowering is one relaxed atomic load per lowered program. When the
server is on but idle, its thread sleeps in `epoll`/IOCP.

## Safety

- **Loopback only.** `ParseEndpoint` accepts `127.0.0.1`, `::1` and `localhost` and refuses
  every other host, `0.0.0.0` included. The server is reachable only from the PC itself, where
  the agent already has a shell.
- **Off by default.** It needs `EDEN_NX2_DEBUG_SERVER`, which the owner's shortcut sets for a
  debug build. Release packages are unchanged unless someone sets it.
- **Writes only under the dump directory.** `EDEN_NX2_DEBUG_DIR`, by default
  `<log dir>/debug-server`. Clients never send paths. The server names every file itself:
  `capture-<session>-<n>/`, `screenshot-<session>-<n>.png`.
- **Nothing the server returns comes from game files, keys or firmware.** It returns rendered
  pixels, log lines, frame-dump manifests and shader hashes, which the owner already sends today.
  Later phases that read memory are limited to guest RAM and GPU memory. They never read host
  memory, the key area or the file system. The adapter's `capture_image` reads only PNGs inside
  capture directories that it created in this session, and checks for path escapes.
- **Logs.** `log_tail` returns lines that already go to `eden_log.txt`.
- **No authentication.** Any local process may connect, the same as with the GDB stub. That is
  acceptable on the owner's single-user PC with the switch off by default. If it ever runs on a
  shared machine, add a token file in the dump directory that the adapter reads.

## Tool surface, by how much each cuts the loop

"Today" is the cost of answering the question without the server.

| Tool | Today | With the server | Phase |
|---|---|---|---|
| `status` | Ask the owner what is on screen, read the log header | One call: title, program id, build, paused, presents, fps, renderer, rejected hashes | 1 |
| `screenshot` | Owner takes and sends a screenshot | PNG of the next presented frame, as MCP image content | 1 |
| `log_tail`, `log_filter` | Relaunch with a new filter, owner sends the log | Read new lines since the last call, change the filter live | 1 |
| `capture_frame` | Relaunch with `EDEN_NX2_FRAME_DUMP`, owner touches `capture.now`, agent copies the dump | Capture the next frame now, sampled inputs optional, get the manifest summary back; `capture_image` returns any pass PNG | 1 |
| `reject_shaders` | Rebuild the env, relaunch, replay to the spot, once per bisection step | Reject or restore hashes live; bisection becomes a loop of reject, screenshot | 1 |
| `pause`, `resume`, `step_frames` | Not possible | Freeze on the broken frame, capture it, step one present | 2 |
| `input` (buttons, sticks, timed sequences) | Owner plays to the spot | Agent walks to the spot itself and replays a repro | 2 |
| `shaders` (inventory) | Shader dump and offline sweep | List loaded programs: key, hash, stage, address, lowered or rejected with the error | 2 |
| `switches` (live `EDEN_NX2_*`) | Relaunch per switch | Toggle the observer switches live (classification below) | 2 |
| `read_guest_memory`, `read_gpu_memory` | GDB stub session or log prints plus a rebuild | Read a range by address | 3 |
| `dump_texture`, `dump_buffer` | Frame dump of the whole frame | One resource by GPU address, as PNG plus raw | 3 |
| `gpu_timings` | `EDEN_NX2_GPU_PROFILE` relaunch, zones only | Per-pass GPU time of one frame | 3 |
| Capture options (pass range, shader filter) | Not possible | Smaller captures, for example only passes 40 to 60 | 3 |
| Save and load state | Not possible | Not possible: see "Save states" | none |

### Phase 1 (implemented)

**`status`** `{fps_window_ms?}`. It reads the title and program id that `Load` saw, the build from
`Common::g_build_fullname` and `g_scm_rev`, and `System::IsPaused`. It measures fps from
`Core::g_nx2_present_count` (`src/core/core.cpp:78`) over a short window. It does not call
`GetAndResetPerfStats`, which would reset the Qt status bar's counters.

**`screenshot`** `{height?, save?, timeout_s?}`. It requests a 16:9 layout from
`Layout::DefaultFrameLayout`, and the renderer blits the presented framebuffers into it. The
BGRA result is turned into RGB and encoded as PNG with stb, which `common` already compiles. The
pixel buffer is owned by the completion callback, so a request that times out cannot leave the
renderer writing into freed memory. If the Qt screenshot hotkey has a request pending, the tool
fails instead of taking over the hotkey's request.

**`log_tail`** `{after?, limit?, contains?}`. A `RingBackend` in `src/common/logging.cpp:308`
keeps the last 20,000 formatted lines. It is an ordinary backend that `FmtLogMessageImpl` already
fans out to (`logging.cpp:488`), and it does nothing while its capacity is 0, which is the
default. The service turns it on at boot. A call without `after` returns the newest lines.
`next` pages forward, so an agent can poll "everything since my last look".

**`log_filter`** `{filter}`. It parses the filter with `Filter::ParseFilterString` and applies
it with `SetGlobalFilter` (`logging.cpp:449`), the call the Qt settings dialog makes. Lines
already filtered out were never formatted, so the new filter applies only to later lines.

**`capture_frame`** `{sampled?, timeout_s?, manifest_lines?, index?}`. It reuses `FrameDump`.
`FrameDump::Arm` (`vk_frame_dump.cpp:222`) points the next capture at a fresh directory and resets
the per-capture counters, so `EDEN_NX2_FRAME_DUMP` is not needed. When to capture is decided by
`FrameDumpSchedule`, which has no Vulkan state and is unit-tested. It handles both the env
triggers and the debug server's requests. It also sets `RecordingSampledTextures`, which the
graphics and compute pipelines check before they keep the textures each draw and dispatch
samples. The pipelines used to read `EDEN_NX2_FRAME_DUMP_SAMPLED` once at startup, so a sampled
request at runtime found no sampled textures to read back. Now the pipelines keep them only during
a sampled capture, under either trigger. `FrameDump::TickFrame` (`vk_frame_dump.cpp:693`) starts
the capture exactly as `capture.now` would. The writer thread reports completion after it writes
the manifest summary (`WriterLoop`, `vk_frame_dump.cpp:568`). The tool returns the directory, the
frame and summary lines, counts per entry kind, the first entries, and the `frame_dump_index.py`
command. With `index: true`, the adapter runs that command itself. The adapter-only
`capture_image {directory, name}` returns one pass PNG as image content.

**`reject_shaders`** `{hashes?, reject?, timeout_s?}`. This is `EDEN_NX2_REJECT_HASH` at
runtime: the same keys (native program key or shader hash) and the same effect. Pipeline
invalidation turned out to be tractable:

1. The lowering checks the runtime list right after the env list
   (`src/video_core/shader_environment.cpp:642` and the block after it). A rejected program
   throws the same `Shader::RuntimeError`, which the existing rejection path handles: the draw is
   skipped, the error is logged once, and the address is remembered.
2. `ShaderCache::RefreshDebugRejections` (`src/video_core/shader_cache.cpp:67`) runs on the
   rasterizer thread. It finds cached `ShaderInfo`s of lowered native programs whose
   `unique_hash` or `native_program_key` is on the list. It drops them through the same
   `InvalidatePagesInRegion` and `RemovePendingShaders` path (`shader_cache.cpp:369`, `:390`)
   that guest writes to shader memory take. For native code `unique_hash` is the shader hash
   (`GenericEnvironment::Analyze` returns `NativeShaderHash(code)`).
3. It sets `Dirty::Shaders` on every active channel. `RefreshStages` returns early while that
   flag is clear (`shader_cache.cpp:120`), and its `shader_infos` would still point at the
   dropped entries. With the flag set, the next draw looks the program up again, lowers it again,
   and hits the rejection. Compute looks up its program on every dispatch
   (`ComputeShader`, `shader_cache.cpp:254`), so it needs no flag.
4. After an un-reject, every remembered rejection (`rejected_native`) is cleared, so programs
   rejected earlier are lowered again. Programs that really fail are rejected again and logged
   once. The lowered result is identical, so the old pipeline still sits in the pipeline cache
   under the same key and is reused: restoring costs nothing.

The tool waits until the rasterizer applies the change and reports `applied`. A bisection is a
loop: reject half of the candidates, screenshot, compare. Today each step is a rebuild of the
environment plus a relaunch plus a replay.

### Phase 2: control and input (about 4 to 5 days)

- **pause, resume, step_frames.** The core has `System::Pause` and `Run` (`src/core/core.cpp:279`).
  The frontends own the run state, though: Qt's `EmuThread` decides when to call `Run`, so a
  server that called `Pause` directly would desynchronise the UI. The frontend registers
  pause and resume callbacks with the service, and Qt implements them with the same slot its
  pause button uses. `step_frames {n}` resumes, waits until `g_nx2_present_count` advances by
  `n`, then pauses. That is present-granular, not exact, because the GPU drains a frame or two
  behind the CPU. Use a capture to get an exact frame.
- **input.** Inject at `hid_core`'s `EmulatedController`
  (`src/hid_core/frontend/emulated_controller.h:505`, private `SetButton` and `SetStick`)
  with a debug overlay: injected buttons are ORed in, and an injected stick replaces the real one
  while it is held. This works whatever the owner's mapping is. The `VirtualGamepad` engine
  (`src/input_common/drivers/virtual_gamepad.h:51`) only reaches the game when the player's
  profile maps to it, and `EDEN_NX2_KEY_SCRIPT` (`src/yuzu_cmd/yuzu.cpp:599`) presses
  keyboard keys in `eden-cli` only. The tool takes `{buttons, stick_l, stick_r, hold_ms}` and a
  sequence form `[{at_ms, ...}]`, timed by presents, not wall time, so a slow frame does not
  shift the input.
- **shaders.** An inventory from `ShaderCache::lookup_cache` (key, hash, size, address) and
  `rejected_native` (address plus the exception message), copied on the rasterizer thread like
  the reject change. Each entry carries its stage and whether it was lowered, rejected by the
  lowering, or rejected by a hash.
- **switches.** A registry that replaces the `static const bool x = std::getenv(...)` pattern for
  switches that can change safely. It keeps the env var as the initial value, and the getter
  becomes a relaxed atomic load.

Live switch classification, from the switches in `src/`:

| Class | Switches | Live? |
|---|---|---|
| Observers (trace and log only) | `RENDER_TRACE`, `IPC_TRACE`, `IPC_SUMMARY`, `VDEC_TRACE`, `GPU_PROFILE`, `DLSS_CONTRACT_TRACE`, `QUERY_TRACE`, `METHOD_TRACE`, `SVC_TRACE`, `MEM_TRACE`, `PULLER_TRACE`, `QMD_TRACE`, `CB24_TRACE`, `RT_TRACE`, `TC_LOG`, `MME_LOG`, `WINDOW_TRACE`, `FPS_LOG`, `PB_CHECK`, `SPLIT_CHECK`, `EDEN_DKB_FRAME_TRACE` | Yes. Flip at a frame end. Some keep per-frame counters (`render_trace_frame`) that must reset when turned on. `GPU_PROFILE` is an `inline const bool` (`src/video_core/nx2_profile.h:74`) read by every zone; it becomes an atomic. |
| Dumps | `SHADER_DUMP`, `VDEC_DUMP`, `TEXTURE_DUMP`, `PB_DUMP`, `MME_DUMP` | Yes, as on and off only, writing into a subdirectory of the debug dump directory. Clients never pass the path. |
| Behaviour, cached | `REJECT`, `REJECT_HASH` (done), `EXPERIMENTAL_GEOMETRY`, `NO_MESH`, `NO_TENSOR`, `SKIP_VS`, `UNDEF_ZERO`, `FORCE_LOD0` | With invalidation. The first group changes lowering, so it needs the reject path above for every native program. `FORCE_LOD0` changes samplers, so it needs a sampler cache flush. |
| Behaviour, A/B races | `PB_SNAPSHOT`, `FETCH_DRAIN`, `FLUSH_DRAWS`, `SYNC_SUBMIT`, `FENCE_FLUSH_US`, `IDLE_SPIN_US` | Probably, at a frame end. They change timing, not state, but the point of an A/B race test is a clean run, so a relaunch is often still the honest answer. |
| Not live | `SPLIT` (thread topology set at boot, `gpu_split.cpp:89`), `CNTFRQ` (guest-visible clock rate, set in `Load`), `VRAM_LIMIT_MIB` (device creation), `SPARSE_*` and `PRIVATE_DEVICE_RANGES` (decided when memory is mapped), `EMPTY_SYSTEM_DATA`, `AUTO_STUB` (service setup), `MME_JIT` (compiled macros), `DLSS_REPLACEMENT` and `DLSS_PRESET` (pipelines and models built at first use), `FRAMES_AHEAD`, `SWAP_INTERVAL` and `VSYNC_HZ` (pacing state in `nx2_gsv.h`) | No. Changing them mid-run either has no effect or leaves state built under the old value. |

### Phase 3: memory, resources and timing (about 1 to 1.5 weeks)

- **read_guest_memory** `{address, size ≤ 1 MiB}`. Use the application process memory
  (`KProcess::GetMemory().ReadBlock`), as the GDB stub's `m` packet does
  (`src/core/debugger/gdbstub.cpp:237`), with unmapped ranges reported instead of read. It can
  run on the server thread: a racy read of guest RAM is what a debugger does.
- **read_gpu_memory** `{address, size}`. Use `Tegra::MemoryManager::ReadBlockUnsafe` on the
  rasterizer thread. The safe read flushes GPU-written pages through the caches, which only that
  thread may do.
- **dump_texture** `{gpu_address}` and **dump_buffer** `{gpu_address, size}`. Look the address
  up in the texture cache on the rasterizer thread and reuse `FrameDump::Record`'s readback,
  decode and PNG path for one image.
- **gpu_timings**. Bracket each frame-dump pass with timestamp queries. The scheduler already
  writes timestamps into a query pool for its trace (`src/video_core/renderer_vulkan/vk_scheduler.h:95`).
  Return pass index, draw range, shaders and GPU microseconds for one frame. This extends
  `EDEN_NX2_GPU_PROFILE`, which only splits CPU time on the GPU thread into zones.
- **capture options**: `pass_range` and `shader` filters in `FrameDump::BeginPass` and
  `CaptureDispatch`, so a sampled capture of a 300-pass frame does not write gigabytes.

### Save states

The core cannot save state. There is no serialisation of the kernel, services, the dynarmic and
NCE CPU state, or the GPU caches, and Eden and yuzu never had it. The `saveState` in
`src/yuzu/main_window.cpp` is Qt window geometry. Adding save states is a project of its own,
not a tool. The practical substitute is a reproduction script: phase 2 input sequences, recorded
once (`EDEN_NX2_KEY_RECORD` already records keys in `eden-cli`) and replayed after boot.

## Running it on the PC

1. Build the branch as usual:
   `bash C:/lx/build-fix.sh feat/debug-mcp debug-mcp "Any - debug server" "EDEN_NX2_DEBUG_SERVER=127.0.0.1:6545"`.
   Optionally add `EDEN_NX2_DEBUG_DIR=C:/Users/Zortos/.capy/work/lichii-debug/debug-mcp/dumps`
   so dumps land next to the build.
2. The owner launches the game from the shortcut. The log shows
   `Debug server listening on 127.0.0.1:6545, dump dir ...`.
3. The agent runs the adapter with native Windows Python. The MSYS `python` in Git Bash rejects
   `C:/` paths, so call the Windows interpreter by its full path. From bash, `--call` runs one
   request, prints its JSON result, and saves any PNG to `--out`:

   ```
   PY=/c/Users/Zortos/AppData/Local/Programs/Python/Python314/python.exe
   $PY -I C:/lx/src/tools/nx2/debug_mcp.py --server 127.0.0.1:6545 --call '{"method":"status"}'
   $PY -I C:/lx/src/tools/nx2/debug_mcp.py --server 127.0.0.1:6545 \
       --call '{"method":"screenshot","params":{"height":1080}}' --out C:/lx/shot.png
   $PY -I C:/lx/src/tools/nx2/debug_mcp.py --server 127.0.0.1:6545 \
       --call '{"method":"reject_shaders","params":{"hashes":["3ddfb1d7621c4755"]}}'
   ```

   The request is `{"method": <tool>, "params": {...}}`, the same format the socket uses. Adapter-only
   tools work too (`capture_image`, and `index` on `capture_frame`). On failure, `--call` prints
   `{"error": ...}` and exits with status 1. Without `--call`, the script is a stdio MCP server for
   an MCP client.

   Hashes must be all 16 hex digits. A shorter hash is refused rather than zero-padded, because a
   padded hash matches nothing. Returned paths use forward slashes.

The adapter reconnects after the game restarts. A boot through a guest relaunch stops and starts
the server again on the same port.

## Phase 1 limits

- Vulkan only for `capture_frame` and `reject_shaders`. The drain point is in
  `RasterizerVulkan`. OpenGL would need the same three lines in `RasterizerOpenGL::TickFrame`,
  plus a frame dump it does not have.
- A runtime capture uses up an `EDEN_NX2_FRAME_DUMP_AT` capture that has not fired yet. They share
  one `FrameDump`.
- One request at a time. A second client waits while a capture runs.
- `log_tail` sees lines logged after the game finished loading. The boot itself is in
  `eden_log.txt`.
- Runtime rejections last for one boot. Starting the next session clears them.
