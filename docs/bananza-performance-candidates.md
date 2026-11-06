# Bananza performance candidates

This is the historical audit of the first trace. The later profile and upstream
integration supersede its port-selection status; see
[post-profile fixes and upstream coverage](bananza-profile-fixes.md).

No performance PR is selected until Nx2Profile zones identify a relevant cost.
The frame trace supports investigating CPU-side delays, but it does not identify
a hot path or prove an FPS improvement. Bulk-porting these PRs is not justified.

This audit compares fetched refs against `fix/bananza-frame-diagnostics` at
`8644f32fba441c3d703384a9bc56bed0c35346bd`, after upstream merge `3483630`.
Evidence is the supplied `dkb-frame-trace-live.txt`, stored locally under
`~/.capy/work/dkb/`. From 110 seconds onward, 26 GPU samples have median span
41.812 ms, submission sum 5.831 ms, and 91 submissions per frame. Draw summaries
show roughly 5,300 attempts per frame. There are no Nx2Profile zone reports.
The timestamp difference is not a measurement of CPU busy time.

## Exact fetched commits

Each listed head is also the sole code-changing commit in its PR, except #233.
For #233, both commits below change code; the second is the fetched head.

| Ref | Fetched head / code-changing commit |
| --- | --- |
| `upstream/pr256` | `863730d82e830e5c8590bcfcaeb9ffbc80614b4a` |
| `upstream/pr257` | `aaf28a7578aedbe2ed622a839857c75fac2be5c8` |
| `upstream/pr261` | `366b2a6ade60f76ba4d720d6a444d4883b5ef39c` |
| `upstream/pr262` | `1744f7fb05ae56173290fd3cf0cc4557e272479a` |
| `upstream/pr233` | `b547bda6255655136439262042308320ab22352f` |
| `upstream/pr157` | `00ac69475ea6a316b82dd6aa87ac3adc1ec0bd85` |

#233 first adds chunked uploads in `073b8779655a18410ab31277fe5935a746822759`,
then adds waits between chunks in `b547bda6255655136439262042308320ab22352f`.

## Candidates and required evidence

- **#256 is the strongest candidate for an isolated comparison, not a selected port.**
  It replaces index-to-ID hash lookups in `TextureCache::GetSamplerId` and
  `VisitImageView` with descriptor-indexed arrays. Current code still does those
  lookups, but already has single-page descriptor reads and deferred invalidation.
  `DescriptorTable::Refresh` rounds capacity to at least 524,289 entries, so the
  new four-byte IDs add about 2 MiB per initialized table, up to four per channel.
  Require substantial `read handles` or `fill image views` time; preserve native
  texture-index normalization, descriptor changes, and image-deletion invalidation.
- **#262 needs allocation evidence.** It raises inline capacities from 64/64 to
  160 views/96 samplers and specializes away texture-buffer traversal when absent.
  `GraphicsPipeline::ConfigureImpl` already reserves exact counts; allocation
  savings require more than 64 entries. Unbinding remains necessary and is retained.
  Check descriptor counts and `configure`/`texture buffers` time. Compare frame
  timing with profiling off too, because the patch removes the profiling scope
  from the no-buffer case; compiler elimination may already remove unused traversal.
- **#257 is a lower-priority comparison.** It guards empty storage/texture binding
  calls and compares shader hashes before pipeline fixed state. Existing callees
  already iterate enabled masks; empty masks perform no bindings. Existing pipeline
  equality already starts its `memcmp` at the shader hashes. Require significant
  `bind stage buffers` or `pipeline lookup` time before selecting either change.
- **#261 only guards repeated state updates.** It skips unchanged uniform-mask/
  size-pointer assignments and already-empty graphics unbinds. Current code already
  avoids dirty-state resets when masks match. Despite the title, this commit does
  not change `SynchronizeBuffer()` or GPU synchronization. Require relevant
  `configure`/`read handles` cost; do not claim reduced synchronization.
- **#233 does not match the observed workload.** It chunks eligible large 2D
  uploads at a 64 MiB threshold, using 32 MiB chunks and `runtime.Finish()` between
  them. Logged upload maxima are 18 MiB overall and 8–16 MiB in later windows,
  with ample heap budget. Current sparse, layered, and asynchronous upload paths
  already handle other cases. This is a staging-memory change with added waits
  and a new layout algorithm, not an evidenced fix for steady frame time.

## Why #157 cannot be ported unchanged

`GuestMemory::Read/TrySetSpan` already uses `GetSpan()` without copying. #157 adds
a persistent `pb_cached_ptr` to avoid repeated mapping/contiguity checks, plus
fewer acquire checks. Its pointer cache has no map/unmap invalidation.

A concrete counterexample is a fetch at GPU VA G mapped to backing A, followed
by an ordered unmap and remap of G to backing B, then another fetch of the same
range. #157's range-only cache hit returns A's pointer instead of translating G
to B. `MemoryManager::Map/Unmap` changes the tables without clearing that pointer;
`nvhost_as_gpu::Nx2Unmap` explicitly queues unmaps in GPU command order. Writes to
unchanged backing remain visible; the defect is a stale address translation.

Current `DmaPusher::Step` also has newer snapshot eligibility, suspension checks,
split-thread draining, GPU-written command reads, and fallback flushing absent
from #157's source version. Preserve these safeguards. The acquire-check change
is separable, but requires parser tests and evidence that parsing is costly.

## Next run

Retain both GPU thread and GPU back thread reports. Compare `split drain` with
`split wait`, then the descriptor/binding zones above, before choosing any port.
Use the same scene with `EDEN_DKB_FRAME_TRACE=1` and `EDEN_NX2_GPU_PROFILE=1`.
Keep indirect readback off: unset `EDEN_DKB_INDIRECT_READBACK` or set it to `0`.
