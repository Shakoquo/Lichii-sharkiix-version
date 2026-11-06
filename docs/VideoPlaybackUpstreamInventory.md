# Upstream video search inventory

Snapshot from 2026-11-04, Djihads80/Eglantine. Separate `gh pr list` and
`gh issue list` queries used `--state all --limit 1000 --search` with each of
`video`, `movie`, `vdec`, `H.264`, `VP9`, `AV1`, `codec`, and `cutscene`.
All 72 PR and 53 issue matches are retained, including incidental matches in
`video_core` names and logs. These are search hits, not 72 decoder fixes.
PR heads were fetched through `refs/pull/<number>/head` and git diffs were used
because the PR diff API returned a rate-limit error.

Fetched upstream video branches:

- `fix/nx2-gsv-906-video-session`
- `fix/nx2-gsv-912-decoder-image`
- `fix/nx2-video-av1`
- `fix/nx2-video-decode`
- `fix/nx2-video-newer-sdk`

See [port decisions](VideoPlaybackUpstreamAudit.md) for the selected changes.

## Pull requests

| Number | State | Title |
| --- | --- | --- |
| [#18](https://github.com/Djihads80/Eglantine/pull/18) | CLOSED | nx2: crop Mario Kart World's frame to 1920x1080 in handheld mode |
| [#24](https://github.com/Djihads80/Eglantine/pull/24) | MERGED | Fix/nx2 video decode |
| [#25](https://github.com/Djihads80/Eglantine/pull/25) | MERGED | nx2: support mapping modify so NVN2 places compressible textures |
| [#27](https://github.com/Djihads80/Eglantine/pull/27) | OPEN | nx2: native Switch 2 bring-up through title rendering (Welcome Tour, Kirby Air Riders) |
| [#37](https://github.com/Djihads80/Eglantine/pull/37) | MERGED | video_core: arm64 spin hint in the GPU split |
| [#51](https://github.com/Djihads80/Eglantine/pull/51) | MERGED | video_core: draw deferred instances before the state after their loop |
| [#86](https://github.com/Djihads80/Eglantine/pull/86) | MERGED | MKW : nvdrv: clear the sparse region state with the other Switch 2 globals |
| [#100](https://github.com/Djihads80/Eglantine/pull/100) | MERGED | Fix game list crash, unresponsive macOS menu, and missing SDL gamepad input |
| [#109](https://github.com/Djihads80/Eglantine/pull/109) | CLOSED | video_core: bound macro binds and discard misparsed host blocks |
| [#118](https://github.com/Djihads80/Eglantine/pull/118) | CLOSED | nx2: fix vdecsvc end-of-stream abort and release fences hang |
| [#123](https://github.com/Djihads80/Eglantine/pull/123) | OPEN | nx2: Yoshi and the Mysterious Book demo boots to the title screen |
| [#124](https://github.com/Djihads80/Eglantine/pull/124) | MERGED | video_core: align GPU split ring atomics to 128B and gate TC diagnostic sorting |
| [#134](https://github.com/Djihads80/Eglantine/pull/134) | OPEN | video_core: granular profiling for GPU split, push parsing, cache locks, and pipeline metrics |
| [#149](https://github.com/Djihads80/Eglantine/pull/149) | OPEN | nx2: merge PokeDinner fork (sparse regions, vi2 notifications, rendering fixes) |
| [#157](https://github.com/Djihads80/Eglantine/pull/157) | OPEN | video_core: dma pusher pushbuffer span fast-path and branchless dispatch |
| [#160](https://github.com/Djihads80/Eglantine/pull/160) | CLOSED | video_core: early bailout for rejected shader draws before flush |
| [#162](https://github.com/Djihads80/Eglantine/pull/162) | CLOSED | video_core/buffer_cache: reuse unchanged small uniform buffers within command buffer |
| [#163](https://github.com/Djihads80/Eglantine/pull/163) | CLOSED | video_core: coalesce FlushRegion buffer downloads and clip query ranges |
| [#166](https://github.com/Djihads80/Eglantine/pull/166) | CLOSED | Bulk merge of the jojo-chaechae fork |
| [#170](https://github.com/Djihads80/Eglantine/pull/170) | OPEN | Fix replacement texture handling in AMD implementation |
| [#182](https://github.com/Djihads80/Eglantine/pull/182) | CLOSED | video_core: guard MKW macro positions and handle class 0x0600 |
| [#199](https://github.com/Djihads80/Eglantine/pull/199) | CLOSED | video_core: reject out-of-bounds constant buffer uploads |
| [#211](https://github.com/Djihads80/Eglantine/pull/211) | CLOSED | video_core: handle unbacked sparse GPU reads at the GPU boundary |
| [#218](https://github.com/Djihads80/Eglantine/pull/218) | MERGED | nx2: play AV1 movies (FFmpeg with dav1d on Windows and Linux x86_64) |
| [#219](https://github.com/Djihads80/Eglantine/pull/219) | CLOSED | video_core: do not divide by a zero imageGranularity in InitSparseLayers |
| [#222](https://github.com/Djihads80/Eglantine/pull/222) | OPEN | nx2/ampere: lower the FFMA register form (0x598-0x59f) |
| [#233](https://github.com/Djihads80/Eglantine/pull/233) | MERGED | video_core/texture_cache: zero-copy chunked staging uploads for large 2D images |
| [#234](https://github.com/Djihads80/Eglantine/pull/234) | MERGED | video_core/dma_pusher: fast-path batching of consecutive non-execution methods |
| [#235](https://github.com/Djihads80/Eglantine/pull/235) | MERGED | core: do not index registered_processes with an unregistered ASID |
| [#247](https://github.com/Djihads80/Eglantine/pull/247) | MERGED | [Critical] Restores trees and mesh shader functionality to MKW and other titles |
| [#250](https://github.com/Djihads80/Eglantine/pull/250) | OPEN | texture_cache: cap the collector at 60% and 75% of the driver budget |
| [#256](https://github.com/Djihads80/Eglantine/pull/256) | MERGED | video_core/texture_cache: flat array lookup for image views and samplers in DescriptorTable |
| [#257](https://github.com/Djihads80/Eglantine/pull/257) | MERGED | video_core: fast-guard buffer stage bindings and short-circuit pipeline key comparison |
| [#261](https://github.com/Djihads80/Eglantine/pull/261) | MERGED | video_core/buffer_cache: fast-guard buffer state transitions and unbind redundant writes |
| [#262](https://github.com/Djihads80/Eglantine/pull/262) | MERGED | video_core/renderer_vulkan: eliminate descriptor reallocations and optimize stage texture buffer binding |
| [#264](https://github.com/Djihads80/Eglantine/pull/264) | MERGED | video_core/buffer_cache: fast-guard stage buffer updates and uniform buffer disable |
| [#292](https://github.com/Djihads80/Eglantine/pull/292) | CLOSED | kernel: let the client's core run before an HLE reply wakes it |
| [#293](https://github.com/Djihads80/Eglantine/pull/293) | CLOSED | vk_texture_cache: give framebuffers the fewest layers of their attachments |
| [#304](https://github.com/Djihads80/Eglantine/pull/304) | MERGED | shader: lower F2I.F16, TLD/TLD4.F16 and partial-mask TXD (Hyrule Warriors black gameplay) |
| [#305](https://github.com/Djihads80/Eglantine/pull/305) | MERGED | video_core: bump the shader cache versions for the FSEL lowering fix |
| [#313](https://github.com/Djihads80/Eglantine/pull/313) | MERGED | Adds MK W multiplayer support / Wireless and LAN Play through emulator rooms |
| [#349](https://github.com/Djihads80/Eglantine/pull/349) | MERGED | core: detect Switch 2 programs in NSP containers |
| [#356](https://github.com/Djihads80/Eglantine/pull/356) | MERGED | nx2: video decoding fixes (green movies, end of stream, H.264 detection) |
| [#363](https://github.com/Djihads80/Eglantine/pull/363) | OPEN | Experimental Donkey Kong Bananza terrain rendering fixes |
| [#364](https://github.com/Djihads80/Eglantine/pull/364) | MERGED | video_core: fix the -Wshadow error in the FSR 3.1 upscaler |
| [#374](https://github.com/Djihads80/Eglantine/pull/374) | MERGED | video_core: cut per-draw work on the GPU back thread in crowded scenes |
| [#380](https://github.com/Djihads80/Eglantine/pull/380) | OPEN | Windows: select the AV1 FFmpeg package, and document MSVC build pitfalls |
| [#391](https://github.com/Djihads80/Eglantine/pull/391) | MERGED | Fixed variable shadowing |
| [#398](https://github.com/Djihads80/Eglantine/pull/398) | MERGED | tests: fix the tests target build on nx2 |
| [#401](https://github.com/Djihads80/Eglantine/pull/401) | MERGED | ci: skip the release binaries for test-only pushes |
| [#403](https://github.com/Djihads80/Eglantine/pull/403) | MERGED | Adds Mario Kart World-specific 30 FPS and experimental Dynamic FPS options to improve frame pacing and gameplay smoothness, especially on lower-performing hardware. |
| [#411](https://github.com/Djihads80/Eglantine/pull/411) | MERGED | video_core: bound-check macro position writes |
| [#412](https://github.com/Djihads80/Eglantine/pull/412) | MERGED | video_core: drop constant buffer words past the buffer end |
| [#413](https://github.com/Djihads80/Eglantine/pull/413) | MERGED | shader: lower more Ampere forms used by Pokemon Pokopia's compute shaders |
| [#421](https://github.com/Djihads80/Eglantine/pull/421) | CLOSED | Add a built-in MCP server for AI agents (off by default) |
| [#425](https://github.com/Djihads80/Eglantine/pull/425) | OPEN | ci: add native Windows ARM64 build |
| [#427](https://github.com/Djihads80/Eglantine/pull/427) | OPEN | vulkan: fix two macOS crashes (mesh pipelines on MoltenVK, swapchain recreation) |
| [#430](https://github.com/Djihads80/Eglantine/pull/430) | MERGED | video_core: log and fall back on an undecodable render target format |
| [#434](https://github.com/Djihads80/Eglantine/pull/434) | MERGED | video_core: NVIDIA DLSS through NVIDIA Streamline in place of a game's DLSS |
| [#443](https://github.com/Djihads80/Eglantine/pull/443) | MERGED | Fast Fusion Rendering Fixes |
| [#444](https://github.com/Djihads80/Eglantine/pull/444) | MERGED | host1x: detect H.264 packet framing per stream |
| [#446](https://github.com/Djihads80/Eglantine/pull/446) | OPEN | vulkan: don't create task/mesh pipelines without VK_EXT_mesh_shader |
| [#455](https://github.com/Djihads80/Eglantine/pull/455) | MERGED | Sync nx2-experimental with nx2 |
| [#456](https://github.com/Djihads80/Eglantine/pull/456) | MERGED | shader: lower guarded ULEA without .HI or a carry out (Pokopia's sky) |
| [#460](https://github.com/Djihads80/Eglantine/pull/460) | MERGED | video_core: download NVIDIA Streamline's headers so CI builds have NVIDIA DLSS |
| [#462](https://github.com/Djihads80/Eglantine/pull/462) | OPEN | Fix bugs found by an ASan, UBSan and TSan sweep |
| [#464](https://github.com/Djihads80/Eglantine/pull/464) | MERGED | shader_recompiler: fix lost writes that froze DLSS |
| [#465](https://github.com/Djihads80/Eglantine/pull/465) | MERGED | Run Fire Emblem's own DLSS network, with FSR 3.1 as the fallback |
| [#472](https://github.com/Djihads80/Eglantine/pull/472) | OPEN | fix/switch-sports-resort |
| [#475](https://github.com/Djihads80/Eglantine/pull/475) | OPEN | vulkan: only chain the required subgroup size when the device supports it for compute |
| [#476](https://github.com/Djihads80/Eglantine/pull/476) | CLOSED | video_core: build the DLSS network probe on GCC 16 and X11 |
| [#478](https://github.com/Djihads80/Eglantine/pull/478) | MERGED | Fix a black screen with NVIDIA DLSS in games without DLSS (follow-up to #465) |

## Issues

| Number | State | Title |
| --- | --- | --- |
| [#50](https://github.com/Djihads80/Eglantine/issues/50) | CLOSED | Crashes at any NS1/NS2 title while Loading. |
| [#55](https://github.com/Djihads80/Eglantine/issues/55) | OPEN | MKW: 0xC0000094 (INT_DIVIDE_BY_ZERO) in the ea33ad1 lineage blocks the sparse/VRAM fixes from working |
| [#60](https://github.com/Djihads80/Eglantine/issues/60) | OPEN | MKW 1.8.0: lighting/shadows alternate every other frame (plus black/stale frames) — RTX 5060 8 GB |
| [#73](https://github.com/Djihads80/Eglantine/issues/73) | OPEN | any build above 47 crashes during mario kart world's loading |
| [#74](https://github.com/Djihads80/Eglantine/issues/74) | OPEN | [Mario Kart World] Mid-race crash in SyncpointManager::Increment (Vulkan, build 55) |
| [#110](https://github.com/Djihads80/Eglantine/issues/110) | OPEN | mario kart world crash on loading screen |
| [#111](https://github.com/Djihads80/Eglantine/issues/111) | OPEN | Crash 0xc0000094 (divide by zero) at eglantine.exe+0xd3dfcb on Windows build-74 (ee2d55e) |
| [#115](https://github.com/Djihads80/Eglantine/issues/115) | OPEN | General mario kart world rendering issues |
| [#130](https://github.com/Djihads80/Eglantine/issues/130) | OPEN | Graphical glitches |
| [#133](https://github.com/Djihads80/Eglantine/issues/133) | OPEN | Too much brightness on Splatoon Raiders |
| [#161](https://github.com/Djihads80/Eglantine/issues/161) | OPEN | AMD build still crashes with 9060 XT 8GB, unbacked sparse image. Crashes start of the race. |
| [#164](https://github.com/Djihads80/Eglantine/issues/164) | OPEN | Donkey Kong Bananza Gray Screen when loading save file |
| [#167](https://github.com/Djihads80/Eglantine/issues/167) | OPEN | Linux packaging: AUR (eglantine-bin, eglantine-git) and a Nix flake |
| [#180](https://github.com/Djihads80/Eglantine/issues/180) | OPEN | Rainbow Road (Mario Kart World) stutters with read traps enabled (regression since build 83) |
| [#192](https://github.com/Djihads80/Eglantine/issues/192) | OPEN | Emulator crashes instead of booting a game |
| [#224](https://github.com/Djihads80/Eglantine/issues/224) | OPEN | Emulator crashing on interaction |
| [#227](https://github.com/Djihads80/Eglantine/issues/227) | OPEN | tests: 'DeviceMemoryManager: UpdatePagesCachedBatch basic' fails on nx2 HEAD (UpdateCalls 0 != 1) |
| [#242](https://github.com/Djihads80/Eglantine/issues/242) | OPEN | [Mario Kart World] Heavy Micro Stuttering and Crashing |
| [#244](https://github.com/Djihads80/Eglantine/issues/244) | OPEN | Mario Kart World: vegetation flickers, textures partially loaded, then closes (texture cache over budget -> unbacked sparse images, RX 6650 XT 8 GB) |
| [#255](https://github.com/Djihads80/Eglantine/issues/255) | OPEN | Garbled graphics in Splatoon Raiders on RX 6750XT 12GB |
| [#275](https://github.com/Djihads80/Eglantine/issues/275) | OPEN | M5 / macOS 27: Mario Kart World device loss after 'Unmapped Device ReadBlock' (works further on M1 Pro, macOS 26) |
| [#279](https://github.com/Djihads80/Eglantine/issues/279) | OPEN | [Mario Kart World] Sparse remap (build 104+) fixed terrain memory, but VRAM still goes ~850 MB over budget; texture cache refills the freed space (RTX 5070 12 GB) |
| [#289](https://github.com/Djihads80/Eglantine/issues/289) | OPEN | Donkey Kong Bananza launches with Vulkan but crashes after loading |
| [#290](https://github.com/Djihads80/Eglantine/issues/290) | OPEN | In Fire Emblem: Fortune Weave, when running at a resolution higher than native x1 (1440p or 4K), the screen flickers/shakes. |
| [#300](https://github.com/Djihads80/Eglantine/issues/300) | OPEN | the game never stops building shaders you run the same over and over again and still building 150 shaders |
| [#311](https://github.com/Djihads80/Eglantine/issues/311) | OPEN | [Fortunes Weave] Rendered cutscenes being skipped automatically |
| [#319](https://github.com/Djihads80/Eglantine/issues/319) | CLOSED | [MKW] Complete freeze on loading screen (segfault?) |
| [#321](https://github.com/Djihads80/Eglantine/issues/321) | OPEN | [Crash] Pokémon Legends: Z-A: repeated format_lookup_table.cpp:277 assert (texture format=0) then abort 0xc0000409 |
| [#324](https://github.com/Djihads80/Eglantine/issues/324) | OPEN | [Splatoon Raiders] Ink doesn't land properly anymore. |
| [#335](https://github.com/Djihads80/Eglantine/issues/335) | OPEN | Loose dump booted from base/exefs/main does not see the update RomFS (DKB dies on a null pointer) |
| [#340](https://github.com/Djihads80/Eglantine/issues/340) | OPEN | Donkey Kong Bananza: driver access violation on level load after native shader rejections (build 135) |
| [#341](https://github.com/Djihads80/Eglantine/issues/341) | OPEN | AMDGPU Driver crash when booting Mario Kart World (RDNA3) |
| [#348](https://github.com/Djihads80/Eglantine/issues/348) | OPEN | HDR/Color rendering issue |
| [#350](https://github.com/Djihads80/Eglantine/issues/350) | OPEN | [DKB] Loose dump: 41135 unmapped writes to 0x0 before the update RomFS layers, host segfault right after the Switch 2 NPDM |
| [#351](https://github.com/Djihads80/Eglantine/issues/351) | OPEN | Mario Tennis Fever - No textures and in-game text doesn't appear, Intro music stutters |
| [#362](https://github.com/Djihads80/Eglantine/issues/362) | CLOSED | nx2 video: H.264 packets whose first NAL is 256–511 bytes are mistaken for Annex-B and fail to decode |
| [#368](https://github.com/Djihads80/Eglantine/issues/368) | OPEN | [Crash] Fire Emblem: Fortune's Weave (nx2 builds 135, 158, 167) - (assert texture format=0) at the end of the Prologue calendar animation. |
| [#369](https://github.com/Djihads80/Eglantine/issues/369) | OPEN | [Donkey Kong Bananza] Freeze on heavy particle effects — kepler_memory/kepler_compute GPU-engine assert cascade (nx2 build 167) |
| [#376](https://github.com/Djihads80/Eglantine/issues/376) | OPEN | Crash Among Loading File on MacOS (Preparing game files...) |
| [#387](https://github.com/Djihads80/Eglantine/issues/387) | OPEN | Pokémon Pokopia: colors washed out with a purple haze at normal camera angle, correct when camera looks straight down |
| [#395](https://github.com/Djihads80/Eglantine/issues/395) | OPEN | MKW 1.8.0 on a Ryzen 7 170 laptop: GPUBack-bound profile data, and a frame-gen pacer fix (patch included) |
| [#399](https://github.com/Djihads80/Eglantine/issues/399) | OPEN | [MKW] Windows / RDNA1 field notes: frame times by build, the accuracy setting that matters, 2h32 stability, and what did not help |
| [#402](https://github.com/Djihads80/Eglantine/issues/402) | OPEN | Hyrule Warriors Blurry Gameplay |
| [#407](https://github.com/Djihads80/Eglantine/issues/407) | OPEN | FEFW: PGO (MinGW) build skips opening CG |
| [#409](https://github.com/Djihads80/Eglantine/issues/409) | OPEN | [MKW] Audio and Video Stuttering During Initial Cutscene + Very Dark Shadows |
| [#410](https://github.com/Djihads80/Eglantine/issues/410) | OPEN | [fix/mkw-missing-effects] Mario Kart is missing certain shader effects on maps |
| [#414](https://github.com/Djihads80/Eglantine/issues/414) | OPEN | [Pokemon Pokopia] Panics with "No file system interface available" from base/exefs/main, but boots fine from updates/655360/exefs/main |
| [#435](https://github.com/Djihads80/Eglantine/issues/435) | CLOSED | Donkey Kong Bananza Crash |
| [#441](https://github.com/Djihads80/Eglantine/issues/441) | OPEN | [Feature] Fire Emblem: Fortune's Weave: 60 fps without speedup (target frame rate) |
| [#453](https://github.com/Djihads80/Eglantine/issues/453) | CLOSED | Proposta de otimizações de desempenho para hardwares de baixo desempenho e GPUs com 4 GB de VRAM |
| [#457](https://github.com/Djihads80/Eglantine/issues/457) | OPEN | [GBug] Mario Tennis Fever boots initially , cutscene included but after the L+R combination to start goes into a white screen and stops |
| [#459](https://github.com/Djihads80/Eglantine/issues/459) | OPEN | [VBug] Purple screen |
| [#484](https://github.com/Djihads80/Eglantine/issues/484) | OPEN | [Crash] Crash at first fight in Hyrule Warriors: Age of Imprisonment (build 189, Windows, AMD) – native compute shaders rejected, then MME "instruction limit exceeded" |
