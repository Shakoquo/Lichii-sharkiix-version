# Static library shader sweep, 2026-11-04

This is a partial library sweep, not an all-games pass. The inventory identifies
16 titles. Static readers extracted 26,510 distinct code/control allocations from
eight of them, representing 17,916 distinct stage/bytecode combinations. Three
independent fixes remove lowering failures from 1,043 distinct bytecode programs.
The other eight titles still need another asset reader or a guarded runtime capture.

No game was launched for this sweep. The owner had `lichii.exe` running, so the
approved no-GUI safety condition prevented fallback boots. All library access was
read-only. Exported data, logs and builds stayed in separate scratch directories;
no key files were needed or transferred.

## What the counts mean

The baseline compiler is nx2 `935c5bdb03`. The comparison adds only these changes:

| Fix | Commit | Evidence |
|---|---|---|
| [Bananza BRA.U, #55](https://github.com/zortos293/Lichii/pull/55) | `ae4a50d6a780e40c06ac7a711fd70b4e55a91ac9` | All 814 original rejected fragment captures lower afterward. |
| [Register-handle SULD/SUST, #57](https://github.com/zortos293/Lichii/pull/57) | `edf4b8342167d5a5b1f207b8963d9dd7c48c6658` | 80 of the initial 106 Duskbloods cases lower with this fix alone. |
| [Uniform immediate-high-word shifts, #58](https://github.com/zortos293/Lichii/pull/58) | `c8b0b9a83bc621dd9eb9818a23760f697d461198` | One initial case lowers alone; the other 25 advance to the surface instructions. Both fixes together lower all 106. |

The expanded corpus also exercises padded compute allocations. Across all retained
control variants, native-lowering successes rise from **24,208 to 26,045**.
Lowering failures fall from **2,072 to 235**; another **230** allocations cannot
establish a supported, complete native instruction envelope. No previously lowered
allocation regressed. The 1,837 newly lowered allocations represent 1,043 distinct
bytecode programs: 814 Bananza, 228 Duskbloods and one Fast Fusion program.

These are lowering results, not evidence of correct rendering. All 1,837 newly
lowered allocations next lack runtime specialization context. The inspected
Bananza runtime logs contain no branch-convergence rejection, so #55 has no
demonstrated in-game effect in those sessions.

Full replay of the separate, preserved runtime-cache corpus is unchanged:
**10,170 of 12,598 environments validate with `spirv-val`** before and after;
**2,428** retain their known context gaps. This checks already captured environments,
not the never-played variants in the static archives.

For static allocations, **1,275** emissions validate with a compatible cached
environment, and **4,488** validate with explicit synthetic pipeline defaults.
Both are `partial`, not captured pipeline successes. Cache association is inferred
from title, stage, identical lowered instructions and, for graphics, identical
80-byte shader headers. The output records all matching cache candidates and the
one selected for replay. It does not establish that this archive variation used
that environment at runtime.

## Per-game static coverage

"Lowered" below counts a distinct stage/bytecode combination only when every
retained control variant lowers. Allocations with missing envelopes are not passes.

| Title | Program ID | Code/control allocations | Distinct bytecode | Lowered before | Lowered after | Remaining lowering failures |
|---|---|---:|---:|---:|---:|---:|
| Donkey Kong Bananza | `040074A01BF12000` | 9,905 | 4,837 | 4,005 | 4,819 | 0 |
| Drag x Drive Global Jam | `0400A4802437E000` | 68 | 60 | 54 | 54 | 6 |
| Fast Fusion | `040041E0212AE000` | 791 | 297 | 265 | 266 | 8 |
| Fire Emblem: Fortune's Weave | `04007EF01F8AE000` | 2,317 | 1,963 | 1,944 | 1,944 | 4 |
| Mario Kart World | `0400C3F00006E000` | 3,271 | 2,972 | 2,968 | 2,968 | 2 |
| Nintendo Switch Sports Resort | `04008E001F098000` | 4,549 | 2,247 | 2,204 | 2,204 | 43 |
| Raiders, library label | `0400380020056000` | 3,697 | 3,651 | 3,558 | 3,558 | 34 |
| The Duskbloods network test | `0400D1D02399A000` | 1,912 | 1,889 | 1,471 | 1,699 | 94 |

The remaining eight titles are inventory coverage, not shader passes:

| Title | Program ID | Static extraction gap |
|---|---|---|
| Kirby Air Riders | `040077201BE50000` | NUP RomFS exposes `assets.cat` and large `assets_a000.dat` files, not standalone shader archives. |
| Kirby Air Riders Global Test Ride | `04005B902517C000` | The same opaque asset-container family in loose RomFS. |
| Mario Tennis Fever | `0400D4201C6EA000` | Loose RomFS stores assets in the multi-gigabyte file `42`. Previous runtime captures are not full static coverage. |
| Nintendo Switch 2 Welcome Tour | `040057C02159A000` | `.bea` asset archives need a reader. |
| Orbitals Global Friends Pass | `040004F027560000` | Unreal `.utoc`/`.ucas` I/O Store assets need a selective reader. |
| Pokémon Pokopia | `0400A3A01EE4C000` | NUP RomFS exposes `.fdata` containers. |
| Star Fox | `0400E88025B1A000` | Hash-named ZIP members have no supported shader-container prefix in the examined reads. The standard ZIP reader rejected 45,822 of 114,154 member-prefix reads on CRC checks; this is a reader limitation, not proof that the game dump is corrupt. |
| Yoshi and the Mysterious Book demo | `04007C6028AA8000` | Unreal `.utoc`/`.ucas` I/O Store assets need a selective reader. |

All four current NUPs were cataloged with Lichii's `NupContainer` and `RealVFS`.
Explicit export of 30 shader-named members from Bananza and Mario Kart World yields
the same shader identities as their loose counterparts, so those are not counted
twice. The empty `[0] NUPS` directory is not a title.

The extraction manifest has 103 diagnostics: 95 archive/member diagnostics and
eight inventory entries needing separate selection or a different reader. These
include duplicate NUP entries and the empty directory, not 103 distinct bad shaders.
Unsupported BND4 variants, Kraken DCX payloads, legacy shader binaries and metadata
members remain visible in that report.

## Remaining compiler failures

The [before](shader-sweep-all-games-before-20261104.md) and
[after](shader-sweep-all-games-after-20261104.md) tables group diagnostics by game,
stage, phase and opcode. Counts there are code/control allocations.

The largest remaining native failures include Duskbloods global reduction modes
at `0x98e`, Sports Resort `0x4a4`, Raiders multiply-wide addend and uniform-comparison
forms, and geometry exit-setup validation. Geometry and call-lowering work is being
handled separately; this sweep does not bypass those checks. Mesh and task
allocations are preserved but remain context gaps rather than being mislabeled as
ordinary vertex programs.

## Reproduce

See [the static archive workflow](../tools/shader-sweep/README.md#replay-extracted-library-shaders).
The tools preserve source provenance and distinguish missing, inferred, and synthetic
context. Their reports retain failures rather than treating omitted sources as passes.

## Checks

The standalone compiler/exporter build and all four CTest suites passed. The Python
reader and coverage suite has 24 passing tests. On Windows the NUP exporter passed
12 tests; its symlink test was skipped because symlink creation was unavailable.
The equivalent Linux test passed. Each production fix also has captured-word
regressions and independent before/after corpus replay in its own PR.

The previous sweep tables mislabeled `0400C3F00006E000` as Fire Emblem. That ID is
Mario Kart World; Fire Emblem is `04007EF01F8AE000`. The mapping and historical table
labels are corrected here without changing their recorded counts.
