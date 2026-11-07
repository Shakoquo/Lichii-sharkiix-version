# Ocarina of Time movie playback on Sharkiix 3.1

Ocarina of Time (title ID `0400486024DEC000`) takes one decoded movie frame per game frame. Its 1080p opening uses a 60 Hz AV1 stream. At about 30 game frames per second, the soundtrack can finish while the second half of the picture is still playing.

The video session uses the first picture's presentation timestamp and a monotonic clock to select the picture due for display. It skips pictures that fall more than two frame intervals behind when a newer picture is available. AV1 reference frames are still decoded. The last frame is delivered, and a pause of over one second is excluded from the playback clock. This change is scoped to Ocarina. It does not raise the GPU's rendering rate.

This ports SinCuri0's [Lichii PR 251](https://github.com/zortos293/Lichii/pull/251) at commit [e965abdd](https://github.com/SinCuri0/Lichii/commit/e965abdde60de440fd03d5be2c5288faa5afe04d). The Sharkiix port retains its GPU-channel wake-up after the present reply.

## Reproducing the playback check

1. Create a separate portable profile with a copy of the same save, settings and existing shader cache. Add `EDEN_NX2_MOVIE_CLOCK=1` to the test executable's `nx2_switches.txt` and restart the emulator. This enables one-second timing counters. The timing correction also runs with the counters disabled.
2. Play the long opening movie to completion without skipping or pausing. Listen on the active output device through the transition. Compare the elapsed host time between the movie's first frame and its end-of-stream log line with the final video PTS. Use frame 1 of this movie, since the title sequence can open a separate AV1 session first.
3. Check the diagnostic lines for the video PTS, presentations, audio frames delivered to the sink and padded samples. A functioning sink can still be routed to an inaudible output. Confirm the audible result separately.

## Windows validation

The tested binary was rebuilt from the **3.1 release source** with MSVC 19.51 in Release mode. The four `src/audio_core/out` files missing from the source archive were taken from the project repository. The comparison used Ocarina 2.0.0, a Ryzen 7 5700G and Radeon RX 6750 XT, Vulkan, docked 1x, 100% speed, frame generation off and stereo SDL3/WASAPI. The measurements compare complete builds. The PR targets the maintained `sharkiix-v3.1` branch, which also contains later changes to AMD pipeline-library defaults.

| Long opening movie | Original 3.1 | Patched 3.1 |
| --- | ---: | ---: |
| Final video PTS | 154.217 s | 154.217 s |
| First picture to end of stream (host time) | 299.445 s | 154.233 s |
| Pictures delivered, including the terminal frame | 9,254 | 4,660 |
| Late pictures skipped | 0 | 4,594 |

The patched run reached the terminal frame within 0.016 s of the nominal movie duration. That measures **duration error**, not absolute audio latency. Across 150 interior timing windows the renderer averaged 30.6 presentations/s, with zero padded samples on the active audio sink. After selecting the intended Windows output, the tester confirmed that the music and picture stayed together. Fingerprints for all 16 files in the original NAND and active shader cache matched before and after the isolated test.

**Remaining stability issue.** A later in-game cutscene closed the process. Windows recorded an access violation (`0xC0000005`) in `amdvlk64.dll` at offset `0xAFF2E7`, also observed on unpatched 3.1 with this driver. The final emulator log showed graphics pipeline builds and a GPU wait timeout. These observations do not identify the failing shader. See the [AMD pipeline-crash investigation](https://github.com/Djihads80/Eglantine/issues/618).
