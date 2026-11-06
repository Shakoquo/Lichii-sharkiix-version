# Bananza global atomics: unmapped append counters

The owner's live log still rejects nine Donkey Kong Bananza compute programs with `Global atomic add with no routable storage buffer is not implemented`. Those rejections account for 24 of the 32 `Native QMD failed` lines. The earlier emitter fix (`docs/bananza-atomic-fix.md`) did not help, because the storage-buffer pass never gave these atomics a buffer.

## Root cause

Each failing atomic is an append-counter add. Its address is the whole 64-bit pointer at `c0[0x1d0]` or `c0[0x1e0]`, for example `GetCbufU32x2 #0, #464`. The append records are then stored at that pointer plus the returned index.

For native programs, `CollectStorageBuffers` drops the pointer that `TrackPointerAddend` finds when `IsCbufStorageBufferPointer` reports the slot as unmapped at compile time. If `NativePointerCandidates` finds no other pointer, the access stays global. A global atomic has no fallback, so emission fails. This rule came from the resolution of the first upstream merge, 3483630; neither merge parent had it. The candidate binding itself comes from upstream 9e56ed7 (PR #464). Current `upstream/nx2` (01dda0f47d) still has the throwing atomic stub, so there is no upstream fix to port.

All nine programs match compute environments in the owner's v46 cache with the same hash and key. Marking only `c0[0x1d0]` or only `c0[0x1e0]` as unmapped in those environments reproduces the exact error.

## Fix

`CollectAtomicPointers` records every pointer that is a global atomic's whole address, apart from an immediate offset. When a native access has no mapped pointer, it now falls back to its paired pointer, but only if that pointer is in this set. The atomic and the record stores based on it then use the pointer's storage buffer.

The binding reads the pointer again on every dispatch. `StorageBufferBinding` returns `NULL_BINDING` for a zero or unmapped address.

The restriction matters. A broader fallback that accepted any paired pointer changed five v47/v48 compute modules. They read a 32-bit value of `0x20` from `c1[0x0]` and would have bound it as a buffer. Those modules are byte-identical to the pre-fix output with this version. Plain loads and stores through an unmapped pointer, with no atomic, keep the 3483630 behavior.

## Verification

- Replay of the nine real v46 environments in four scenarios (all mapped, `0x1d0` unmapped, `0x1e0` unmapped, both unmapped): before the fix, every unmapped scenario fails with the exact error. After the fix, all 36 compile and pass `spirv-val --target-env vulkan1.2`. Each module has two `OpAtomicIAdd`. For each program, the SPIR-V is identical across all four scenarios and identical to the pre-fix all-mapped output.
- `tools/shader-sweep/fixtures/bananza-unmapped-atomic-counter.json` is the real `c7a1778ec38f628` environment with the counter slots unmapped. The pre-fix compiler fails it; the fixed one passes.
- Full Bananza sweep (2,239 dumps and the v46/v47/v48 pipeline caches): the summary is unchanged, and all 10,906 emitted modules are byte-identical to the pre-fix output. No pipeline cache version bump is needed.
- `[storage_buffer]` tests: 12 cases, 2,953 assertions. They cover rebasing of the counter and record stores, an atomic offset from an unmapped pointer that stays global, and the unchanged scalar rule.

The fix has not been run in the game. The two `native surface reduction mode` (op `0x59f`) failures and six unattributed `Native QMD failed` lines are separate issues.
