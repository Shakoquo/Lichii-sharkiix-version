# Offline shader sweep, 2026-11-04

The sweep compiled 12,132 cases into SPIR-V modules that pass `spirv-val` after the fixes.
No compiler crashes, lowering failures, emission failures, or invalid modules remain in the
replayable corpus. Another 2,432 cases lack captured context and are reported as incomplete,
not passed. This does not establish correct in-game rendering.

## Corpus and method

- Mario Tennis Fever provided 667 `mtf-shaders-run2` files and 663 `mtf-shaders` files. The
  1,330 files contain 482 distinct byte sequences, including one header-bearing rejection dump.
- Twelve serialized Vulkan cache files cover ten title IDs, including Bananza, Star Fox,
  Kirby Air Riders, Yoshi, Mario Tennis, Mario Kart World and Fire Emblem. They contain 12,598 environments.
  Nine driver blobs or used-pipeline indexes were not shader-environment inputs.
- Native-to-cache matching supplied captured headers, stages and resource specialization
  data for 1,325 native files. The five unmatched files belong to the previously rejected
  tennis tessellation pipeline and reduce to four distinct capture byte sequences.
- The run contains 14,564 cases after deduplication and native replay against distinct cached
  contexts. Counts below are compiler cases, not draw counts or unique GPU instructions.
- Graphics stages come from cache records, native headers, or explicit trace-established
  stage annotations. The annotations were `15b2f0501bd24b1d=tess-eval` and
  `31eced459be1ef3e=vertex`. Missing headers were not invented.
- The baseline compiler used `origin/nx2` at `9a435b5`. The final run includes upstream merge
  `13f533027c`, based on Eglantine `a666e4400b`, plus the fixes below. It uses the same
  Vulkan-style SPIR-V 1.4 profile and validates for Vulkan 1.2. Descriptor aliasing and
  direct narrow storage are disabled to exercise portable paths. See
  [the compiler profile](../tools/shader-sweep/README.md#offline-context-and-profile).

## Before and after

The generated [before](shader-sweep-before-20261104.md) and
[after](shader-sweep-after-20261104.md) tables retain every failure group by game and stage.

| Cause | Game | Stage / opcode | Before | After |
|---|---|---|---:|---:|
| Missing CAS helper, compiler abort | Donkey Kong Bananza | 124 vertex + 2 geometry / `WriteStorageU8` fallback | 126 | 0 |
| Global helpers reference unavailable descriptor aliases | `04008e001f098000` | Vertex / global memory | 2 | 0 |
| Passthrough mask declares illegal stage built-ins | Mario Tennis Fever | Geometry / `TessCoord`, `VertexIndex`, `FrontFacing` | 108 | 0 |
| Native stage-buffer access rejected | Mario Tennis Fever | Tessellation control / `0x923` ISBERD | 2 | 0 |
| Native coordinate read rejected | Mario Tennis Fever | Tessellation evaluation / `0x321` ALD.O | 1 | 0 |
| Missing captured context | Multiple titles | See below | 2,429 | 2,432 |
| Compiled and validated | Multiple titles | All represented stages | 11,896 | 12,132 |
| **Total cases** | | | **14,564** | **14,564** |

The context count rises by three because the TCS/TES programs now pass lowering and reach
their missing header or constant-buffer data. This is not three new compiler regressions.

## What changed

**Ported or integrated work.** The storage CAS-helper guard was ported from
[Eglantine PR #178](https://github.com/Djihads80/Eglantine/pull/178). The second upstream merge
also brings the equivalent `ad98585`, whose implementation is retained. The tool includes a
complete captured Bananza regression environment that aborts before the guard change and
validates afterward.

The coordinated tennis commits `7a9dd37` and `078b720`, Star Fox ULDC.U16 commit `4a58842`,
and routed GlobalAtomicIAdd32 commit `ca6b8e3` were integrated rather than reimplemented.
The atomic commit requires the shader-only storage-routing changes from `8644f32` and opcode
diagnostics from `ba2d291`; those prerequisites and their tests are included, without the
unrelated profiling changes. The peer's TES regression explicitly uses a five-word captured
header prefix and a synthetic quad-tessellation context. It is a synthetic-context test,
not a fully captured pipeline and not an extra validated sweep row. Star Fox's rejected
native ULDC program and Bananza's rejected atomic program are likewise covered by the peer
regressions, not falsely counted as newly recovered cache entries.

**New fixes.** A zero geometry skip mask in Mario Tennis inverted into every attribute bit.
The SPIR-V emitter treated those passthrough bits as actual reads, declaring vertex-only,
fragment-only and tessellation-only built-ins in a geometry shader. It now uses actual loads
for built-ins, while retaining passthrough inputs for position and generic varyings. A complete
captured geometry environment reproduces the original validator failure.

The remaining two crashes came from global-memory helpers using `U32x2` constant-buffer
aliases and vector storage aliases even when the profile had disabled descriptor aliasing.
The helpers now use the existing constant-buffer accessors and split vector global reads and
writes into routed 32-bit accesses on that profile. The captured failing vertex environment
and native/NVN atomic-routing tests cover this path. No equivalent fix was found in the
upstream history or the relevant open shader PRs checked before implementation.

## Remaining context gaps

| Missing data | Cases | Why replay stops |
|---|---:|---|
| Storage-pointer classification | 2,427 | v46 caches predate the captured mapped-pointer flags needed by current native storage analysis. |
| Texture pixel format | 1 | A Yoshi v46 compute environment lacks the requested texture-format query result. |
| Graphics header/environment | 3 | Uncached tennis vertex, TCS and TES instruction-only captures contain no program header. |
| Constant-buffer value | 1 | The header-bearing tennis TCS capture has no specialization values. |

All 2,428 incomplete cached cases are v46 records. Every replayed v47 environment completes.
The tool also accepts v48 because the required upstream merge bumps the cache version without
changing this environment or pipeline-key layout. Fresh full environments are needed to
finish the incomplete rows. Already-lowered caches cannot reveal native instructions that
were rejected before caching, and successful compilation cannot detect missing synchronization,
incorrect shader semantics, or every draw-state interaction.

## Reproduce

Build and run the commands in [the sweep guide](../tools/nx2/shader-sweep.md). Add the two
`--native-stage` annotations above and use `--contexts-from` when comparing across lowering
changes. The run writes its grouped table, machine-readable results and one extracted
environment/key/SPIR-V module per case. The captured regression fixtures record their source
cache SHA-256 and byte offset.

`ctest --test-dir build/shader-sweep --output-on-failure` runs the CLI, cache parser and
captured compiler/validator regressions. Bulk captures and generated SPIR-V files are not
committed. Gameplay still needs an owner-side run on the final integrated build.

Verification passed on the merged base: 17 CLI tests, five parser tests, three complete captured
compiler/validator regressions, and a focused Catch2 build covering 54 shader test cases with
576,491 assertions. The Catch2 selection includes tennis tessellation, Star Fox conversions,
global-memory coherence and atomics, storage-pointer tracking, built-in capabilities and
geometry lowering.
