# Lichii (sharkiix's version)

> **VERY EXPERIMENTAL.** Mainly for **AMD** GPUs, optimised for **Zelda: Ocarina of Time** (Switch 2). Expect bugs and crashes.

A modified build of [Lichii](https://github.com/zortos293/Lichii) by **zortos293** and its contributors,
tuned for *The Legend of Zelda: Ocarina of Time* (Switch 2) on AMD Radeon GPUs (tested on an RX 9060 XT).
All credit for the emulator itself goes to the Lichii / Eden / yuzu authors. This is an unofficial fork.

## Changes
- Faster tensor-op emulation on AMD (title screen ~15 -> 50-60 fps).
- Fewer waits between GPU channels (semaphore acquires resume as soon as a matching release is queued).
- Cheaper texture-cache garbage collection; `EDEN_NX2_TC_MIN_AGE` keeps recently used images (avoids flashes).

## Recommended settings for Ocarina of Time
Set these environment variables before launching `lichii.exe`:
- `EDEN_NX2_TC_MIN_AGE=600`
- `EDEN_NX2_REJECT_HASH=c72b6ea746c00433` (removes the ground-mist shader, which currently renders as a white veil over the whole screen)
- `EDEN_NX2_RESUME_ADD=0`

## Download
Ready-to-use Windows build: see the **Releases** page.

You must provide your own keys, firmware and game, dumped from your own console. None of them are included.

## License
GPL-3.0-or-later, like Lichii. The full source of this build is in this repository.
