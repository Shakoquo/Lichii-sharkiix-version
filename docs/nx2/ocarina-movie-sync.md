# Ocarina of Time movie playback on Sharkiix 3.1

Ocarina of Time (title ID `0400486024DEC000`) takes one decoded movie frame per game frame. Its 1080p opening uses a 60 Hz AV1 stream. At about 30 game frames per second, the soundtrack can finish while the second half of the picture is still playing.

The video session uses the first picture's presentation timestamp and a monotonic clock to select the picture due for display. It skips pictures that fall more than two frame intervals behind when a newer picture is available. AV1 reference frames are still decoded. The last frame is delivered, and a pause of over one second is excluded from the playback clock. This change is scoped to Ocarina. It does not raise the GPU's rendering rate.

This ports SinCuri0's [Lichii PR 251](https://github.com/zortos293/Lichii/pull/251) at commit [e965abdd](https://github.com/SinCuri0/Lichii/commit/e965abdde60de440fd03d5be2c5288faa5afe04d). The Sharkiix port retains its GPU-channel wake-up after the present reply.

## Reproducing the playback check

1. Create a separate portable profile with a copy of the same save, settings and existing shader cache. Add `EDEN_NX2_MOVIE_CLOCK=1` to the test executable's `nx2_switches.txt` and restart the emulator. This enables one-second timing counters. The timing correction also runs with the counters disabled.
2. Play the long opening movie to completion without skipping or pausing. Listen on the active output device through the transition. Compare the elapsed host time between the movie's first frame and its end-of-stream log line with the final video PTS. Use frame 1 of this movie, since the title sequence can open a separate AV1 session first.
3. Check the diagnostic lines for the video PTS, presentations, audio frames delivered to the sink and padded samples. A functioning sink can still be routed to an inaudible output. Confirm the audible result separately.

## Windows validation

The initial movie-clock comparison binary was rebuilt from the **3.1 release source** with MSVC 19.51 in Release mode. The four `src/audio_core/out` files missing from the source archive were taken from the project repository. The comparison used Ocarina 2.0.0, a Ryzen 7 5700G and Radeon RX 6750 XT, Vulkan, docked 1x, 100% speed, frame generation off and stereo SDL3/WASAPI. The measurements compare complete builds. The PR targets the maintained `sharkiix-v3.1` branch, which also contains later changes to AMD pipeline-library defaults.

| Long opening movie | Original 3.1 | Patched 3.1 |
| --- | ---: | ---: |
| Final video PTS | 154.217 s | 154.217 s |
| First picture to end of stream (host time) | 299.445 s | 154.233 s |
| Pictures delivered, including the terminal frame | 9,254 | 4,660 |
| Late pictures skipped | 0 | 4,594 |

The patched run reached the terminal frame within 0.016 s of the nominal movie duration. That measures **duration error**, not absolute audio latency. Across 150 interior timing windows the renderer averaged 30.6 presentations/s, with zero padded samples on the active audio sink. After selecting the intended Windows output, the tester confirmed that the music and picture stayed together. Fingerprints for all 16 files in the original NAND and active shader cache matched before and after the isolated test.

## AMD driver crash during pipeline compilation

A later in-game cutscene closed both the unpatched 3.1 build and the movie-clock build on the test PC. Windows recorded exception 0xC0000005 in amdvlk64.dll 32.0.21045.16010 at offset 0xAFF2E7. A GPU wait timeout alone did not identify the pipeline.

To reproduce the driver failure at startup, preload the captured 624-pipeline game cache with 15 workers and graphics pipeline libraries disabled. A diagnostic build wrote a BEGIN and END record on each worker around the Vulkan pipeline creation call. The exception thread in the Windows minidump matched an outstanding graphics call for pipeline fb7be4be24d68b5c, vertex shader 00e4eb4772d5076b and fragment shader bb5c27d79d59c88f. The process had logged 603 completed calls. Several other threads were compiling at the time.

With `VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT` for that fragment, the same call returned and all 624 calls finished. The unchanged input cache had SHA-256 `7d4c822e2dda4772be57b02bfb36aaa8982f1ceea60cd4a1491fac09b658fb8d`. The opening movie reached its last frame at 154.217 s. Frame generation was enabled in this diagnostic replay. Over 152 sampled windows with active video after the first second, it averaged 36.4 guest presentations/s with zero padded samples on the active audio sink.

**Skipping a later cutscene.** A tester then skipped a scene and hit the same driver exception and offset on a different graphics pipeline, `61c70430c5c16bc8` (VS `385df28c8d9acebf`, FS `3c2193d0166610b3`). The game had recorded it in its transferable cache. The next launch reproduced the exception during preload: 774 calls had completed out of 776. Both exception threads had entered that exact pipeline and had not returned. This fragment also appears in [Lichii PR 210](https://github.com/zortos293/Lichii/pull/210), for another pipeline state on the same AMD driver version.

**Scope of the workaround.** On AMD proprietary Vulkan driver 2.0.353, create pipelines for these two fragments without driver optimization. Use a monolithic pipeline for those fragments to keep the driver from optimizing a shared fragment library. Apply the same flag to compute shaders `eff3ccbe1438679d` and `f9ea14a22394f5b1`, the last two outstanding calls in the 624-pipeline run and the slow compiles reported in PR 210. All four shaders still execute. Driver caches were retained across runs, so these timings cannot establish a cold-cache speedup.

**Branch build replay.** The Release binary built with the maintained branch sources (SHA-256 `2046b3f0091b02650d5a195c0575badc41d0b04fd29c68dd23546936ec68ea5f`) loaded the expanded 776-pipeline cache (SHA-256 `8b8ba95fce9e0c5caed738b48695b13a2e3c8776613eadc2d6d9120a5e45d0a4`). With 15 workers and compile timing enabled, both previously faulting driver calls returned: 548 ms for `bb5c27d79d59c88f` and 2,132 ms for `3c2193d0166610b3`. The two compute calls overlapped and took 69.8 and 74.7 seconds. The decoder produced its first picture at 89.9 s from process startup.

The tester repeated the cutscene skip with frame generation enabled, reached the next scene and regained control of Link. A second boot in the same process logged `Renderer.frame_gen: false`, loaded an expanded cache of 1,100 pipelines and reached its first decoded frame 48.6 s after the boot request. The tester repeated the same skip with frame generation disabled and confirmed Link remained playable. Both runs used driver caches warmed by earlier tests. GPU wait timeouts still appeared during scene loading and the game continued to present frames. These checks cover this transition and both cache replays.

**GPU preprocessing experiment.** A local OpenCL microbenchmark replayed 514 actual 160-byte graphics pipeline keys from the first cache, checking every hash against the emulator trace. In 101 interleaved warm trials with the game open, median host time was 15.2 us for a single native CPU batch, 222.9 us for a GPU batch and 228.8 us for a half-CPU, half-GPU batch. Median device kernel time for the GPU batch was 1.44 us. Device time spanning upload, execution and readback was 132.28 us. Host times include Python invocation and OpenCL submission overhead. This measures a possible offload of CityHash64 only. It does not measure or change the proprietary shader compiler, and the experiment is not part of the runtime change.

The original profile's 16 saved NAND and shader-cache file fingerprints matched after the diagnostic replay. Windows dumps, trace files, shader data and game saves are local test inputs.
