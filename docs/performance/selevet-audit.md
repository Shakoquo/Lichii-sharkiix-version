# Selevet performance audit

Audited November 5, 2026 against Lichii `30d6061f5b280262286b324e5aab2675a3f26f5e`,
Eglantine `e736dfde9e45bc3640935e373927814883803d95`, and Selevet
`c3aa97d952b768e8798872d74c21094a340041ff`.

Most useful Selevet optimizations already exist in Lichii. This branch ports the
usage-tracker reset guard and adapts TAS input-change suppression. Skipping command dependencies,
native draws, or requested presentation intervals is not a correctness-preserving
optimization. This audit does not attribute the owner's private-build gains to
any particular public change, and makes no measured game-FPS claim.

## Provenance

The public fork has **no merge base** with either nx2 branch, even with full git
history. Its root `c4137dc795` is a source snapshot of Lichii's earlier
`badc91ebce6b6cb44eabe67f7aa0dbbc6abd9e46`. That earlier commit is an ancestor of
current Lichii. The fork's later upstream imports are ordinary single-parent
commits, not git merges:

| Selevet commit | Source or purpose |
|---|---|
| `c4137dc795` | Build-31 snapshot plus mapping comparison, draw count, wait logging and PGO additions |
| `54b34dedb3`, `51a140cc4f` | Settings defaults, then restore stock memory-layout default |
| `f28449539b` | Historical GPU-written-page command-drain shortcut |
| `e8d94946c1`, `64685fe34a` | Present-rate override and x86-64-v3 build target |
| `53c39dfed6` | Squashed Eglantine build-104 import plus sparse-remap policy |
| `d8c95fef47` | Performance build 1: submits, acquires, bitmap scans, usage reset, TAS and host tuning |
| `e902aca643` | Existing Lichii AVX-512 CLZ fix |
| `bce3c83739` | Performance build 2: download batching, queued acquires, polls and experiments |
| `59a1b82e04` | Performance build 3: AVX-512 suppression, DKB upscale/EOS changes |
| `8b076b167f` | Performance build 4: VRR patch, native draw skipping and diagnostics |
| `114b5b06bd` | Performance build 5: squashed build-131 import, NVOF, pacing and translator reconciliation |
| `c3aa97d952` | Performance build 6: Kirby workarounds, selected build-163 imports, retries and experiments |

The audit compared the root to its stated source, every later commit to its
parent, and imported hunks to build-104, build-131 and build-163. It also compared
the surviving behavior to both nx2 tips and relevant unmerged branches. CRLF and
file-mode churn were excluded from semantic comparisons. Reproduce the main
inputs after fetching the three repositories:

```sh
git merge-base --all origin/nx2 selevet/main
git merge-base --all upstream/nx2 selevet/main
git diff --ignore-space-at-eol badc91ebce c4137dc795
git log --reverse --format='%H %s' c4137dc795..selevet/main
git show --ignore-space-at-eol <selevet-commit>
git diff --ignore-space-at-eol build-131 114b5b06bd
git diff --ignore-space-at-eol build-163 selevet/main
```

The two merge-base commands return no commit. Neither whole-tree comparison is
itself a list of Selevet inventions: both include inherited Lichii work and older
upstream implementations. Upstream nx2 is already contained in the audited Lichii
base. Relevant PR/issue surveys and actual patches included upstream #593, #609,
#576, command-copy, deferred-instance, sparse-map, Mii and six-axis work. After
the GitHub API rate limit, PR heads were fetched with git rather than treating
failed API responses as empty diffs.

## Change decisions

“Existing” means present in the audited Lichii nx2 unless a different branch is
named. “No” in the port column includes experiments deliberately not added.
Performance descriptions are expected mechanisms, not benchmark results.

### Mapping, caches and scheduling

| Change | Selevet commit | In Lichii already? | Correct? | Ported here? | Expected impact and risk |
|---|---|---|---|---|---|
| Compare only the changed mapping-table window | `c4137dc795` | Historical class is absent; current registry has no whole-table difference calculation | Historical equivalence passed | Test only | Reduces old comparison work; still scans leases and trims mappings. No current-Lichii speedup without replacing its different mapping model. |
| Use the active draw count, zero for unknown indirect counts | `c4137dc795` | Yes, `226162edf5` | Yes | No duplicate | Avoids stale unused vertex/index counts. Current native draws additionally cannot be discarded. |
| Skip reset of an unchanged usage tracker | `d8c95fef47` | No | Yes, with reset/track invariants tested | Yes | Avoids clearing idle buffers' bitmaps and recent-range caches each frame. Small CPU saving, no dependency changes. |
| Skip empty SlotVector bitmap words | `d8c95fef47` | Yes, `00285925fa` | Increment is sound; fork retains a separate broken `begin()` distance | No duplicate | Current implementation also handles an empty first word correctly. Whole-header replacement would regress it. |
| Find next priority with `countr_zero` | `d8c95fef47` | Yes, `00285925fa` | Current version is safer | No duplicate | Current code retains the generic path above 64 priorities; fork can overflow `to_ullong` or shift beyond a u64. |
| Submit native graphics earlier | `d8c95fef47` | Yes, `226162edf5` and `submission_policy.h` | Current version is safer | No duplicate | Overlaps CPU/GPU work at a submission cost. Fork's counter arithmetic reaches its nominal 128 threshold at call 159, as reproduced in isolation, not measured in-game. It changes legacy behavior too. |
| Drain only historically GPU-written command pages | `f28449539b` | No; newer writer waits/copies exist | No | No | First queued write can be missed. Stale command words can affect addresses and synchronization, not merely one displayed frame. |
| Defer first dirty-command readback; hot-page prediction | `d8c95fef47` | No; newer command-copy design exists | No | No | Front parses before producer/download completion. Atomic history bits do not publish command payloads. |
| Hot-entry hash predictor and command lag | `bce3c83739` | No | No | No | Start addresses omit size/address-space identity; lag deliberately executes old commands. Historical hash state survives sessions. |
| Batch dirty-entry downloads behind one wait | `bce3c83739` | Modern command copies/batching address the same stalls | Not safe as a direct port | No | Sound idea, but old implementation exposes staging pointers, clears dirty state before publication and drives the scheduler from the front. Current back-thread ownership must be preserved. |
| Peek RAM/latched state before an acquire drain | `d8c95fef47` | No | Combined shortcut is unsafe | No | RAM may hold an old matching value while an earlier queued write replaces it. Equal/mask waits are not monotonic counters. |
| Fire-and-forget SEM_EXECUTE fence release | `d8c95fef47` | Current release runs on the back and waits | Not a complete acquire implementation | No | Avoids a drain but permits dependent front reads before side effects; removes the unmet-acquire warning. |
| Satisfy acquires from queued puller/3D releases | `bce3c83739` | No | No | No | Queued CPU work is not GPU completion. Other writers can invalidate the prediction; records survive split restarts and can match reused addresses. |
| Skip acquires at any historical QMD-release address | `bce3c83739` | No | No | No | Proves neither matching payload nor producer completion. Omits actual semaphore dependencies. |
| Poll timeline completion for 300 microseconds | `bce3c83739` | No | Completion condition retained | No | Hardware-dependent latency/CPU tradeoff, not a proven win. Repeated driver calls can increase contention. |
| Poll worker queue for 300 microseconds | `bce3c83739` | No | No lost wake identified | No | Repeated mutex acquisition can delay the producer and consume a core. Needs separate controlled benchmarking. |
| Configurable split spin count | `bce3c83739` | Existing fixed spin remains | Default unchanged | No | Tuning experiment only; unbounded input can cause excessive spinning. |
| Redraw shadow-like targets only every N frames | `bce3c83739` | No | No | No | Saves work by dropping draws/clears. Target dimensions do not prove disposable shadows or absence of buffer writes. |
| Skip unbuilt native pipelines | `8b076b167f` | Deliberately prohibited by `async_draw_policy.h` | No | No | Can permanently omit persistent texture/buffer outputs. Fewer compile stalls are not equivalent rendering. |
| Duplicate GPU-written marking in import | `114b5b06bd` | One mark plus newer copy tracking | Redundant | No | Repeating the atomic OR adds work without improving correctness. |
| Sparse remap capability gate, later enabled in shipped switch files | `53c39dfed6`, `c3aa97d952` | Yes; current capability defaults on | Needed capability, not a new algorithm | No duplicate | Disabling tile remap can exhaust fallback backing or leave unmapped scenery. The fork's claimed device-specific slowdown is not independently verified. |

Current command ordering includes `8f1435f8db`, `9e330271ef`, `9960298c96`,
`e3d8e9c605` and the deferred-map snapshot fix `703da11d1e`. Those changes are not
replaced with the fork's older speculative readback path. A raw-RAM counterexample
is an old value of 1, a queued write of 2, then acquire-equal 1: the peek accepts
the stale 1 without observing the logically earlier write.

### Shaders, games, video and presentation

| Change | Selevet commit | In Lichii already? | Correct? | Ported here? | Expected impact and risk |
|---|---|---|---|---|---|
| Second build-131 translator on lowering failure | `c3aa97d952` | Unified current lowerer instead | Not safe as-is | No | Duplicates 2,896 lines of old upstream code, bypasses later rejection checks, and aliases distinct Instruction types with reinterpret_cast. Acceptance is not correctness. |
| F2I.FTZ.U64.TRUNC, TXQ.B/masks/multiple results, CSMTEST, subword local access | `c3aa97d952` | Yes; upstream imports and integrated local-memory support | Current equivalents retained | No duplicate | Shader coverage rather than a general speedup. Current validation is stronger. |
| Local GPR+uniform addresses and STL | `c3aa97d952` | Yes, `f6a8f51324` | Current version preserves address/value limits and scratch dependencies | No duplicate | Replacing it with the old lowerer risks corrupting multiword spill addresses. |
| Immediate SHF.U64.HI below 32 | `c3aa97d952` | Current lowering supports wider forms | No useful new coverage | No duplicate | Fork also admits a U64 form into an older right-shift shortcut. |
| UPOPC and task/mesh ISBERD/ISBEWR plumbing | `114b5b06bd` | Yes; build-131 imports and later repairs | Existing newer implementation retained | No duplicate | Current code includes later shadow-register fixes, including `b328477840`. |
| F2I cbuf abs/neg modifiers | `114b5b06bd` | Partial in nx2; broader fix on `origin/fix/oot-fog`, `1ad14bebb8` | Candidate already under separate Lichii work | No duplicate branch work | Review/test that existing hunk, not the fork's older translator. Correctness benefit, not established CPU gain. |
| Predicated CALL.NOINC/BRA loop exit to BSYNC despite other RETs | `114b5b06bd` | Current forward-transfer/linkage handling covers it | Current implementation retained | No duplicate | No missing capability established. |
| Zero whole bank-17 loads used by integer add/subtract | `c3aa97d952` | Narrower Kirby fix, `0aabd552a2` | Fork heuristic is too broad | No | Zeroes unrelated uses, offsets and stages. Current code rewrites only proven native fragment dither-bias uses. |
| DKB fallback upscale pairs at cbuf0 +0x60/+0x70 | `59a1b82e04` | Native tensor lowering and identified DLSS contracts replace old fallback | Not safe globally | No | Plausible image dimensions do not prove an upscaler; can overwrite unrelated outputs. |
| Cache rejected tensor code addresses and upscale every dispatch | `c3aa97d952` | Current tensor/hash-based handling instead | No invalidation proof | No | Reusing a code address can substitute an unrelated program indefinitely. |
| EOS uses pictures from current input rather than all queued decoded pictures | `59a1b82e04` | Different, fuller queue/readiness handling | Unproven protocol change | No | May add a synthetic blank/stale output and extra fence completion. Needs a DKB submit/output trace, not a generic one-input/one-output assumption. |
| Video stop/finalize claims | `c3aa97d952` | Actual finalize/readiness fixes already present | Fork hunk adds traces/comments, not finalization | No | Current `031e58e930` and `5c6153bc1d` handling must not regress to the fork's success stub. |
| Force arbitrary present Hz | `e8d94946c1`, `114b5b06bd` | Guest-aware pacing and MKW options exist | Only meaningful with matching guest timing/patch | No | A frame cap is not faster execution. Extreme values can round the interval to zero. |
| Fixed-vblank alignment with 2-ms grace | `8b076b167f` | No exact heuristic | Tradeoff, with unsafe zero-interval case | No | Can add latency/drop cadence and sleeps the host guest-core thread instead of using current kernel sleep. |
| Ignore swap intervals above 1 for 120 stable presents | `114b5b06bd` | No | No as a global policy | No | Ignores legitimate 30-FPS timing for about two seconds and indefinitely for changing timing; can alter game speed. |
| Patch SDK QueryVrrSupported to return true | `8b076b167f` | No | No as a general capability implementation | No | No title/version/opcode validation; trusts guest-derived symbol offsets. Changes reported display behavior without implementing VRR. |
| Mii u32 BuildRandom arguments and six-axis ranges | `c3aa97d952` | Yes, upstream `c1c2a0a220` / `e7fe1ddba5` | Existing ABI/sensor fixes retained | No duplicate | Exact build-163 imports, not new Selevet game fixes. |
| Disable ignoring global exclusive monitor in Unsafe defaults | `c3aa97d952` | No | Safer cross-core semantics, incomplete fix | No | Current Auto sets IgnoreGlobalMonitor independently. Copying the default alone does not repair Auto or saved overrides; requires coordinated CPU correctness work and may add cost. |
| NVOF backend and fallback/rebuild chain | `114b5b06bd` | No; FSR3/LSFG are different backends | Not safe as-is | No | Serial graphics finish plus optical-flow host wait removes overlap. Timeout cleanup can destroy resources while the separate flow queue still runs; explicit cross-queue dependencies also need validation. Generated presentation FPS is not guest FPS. |

### Host policy, inputs and build choices

| Change | Selevet commit | In Lichii already? | Correct? | Ported here? | Expected impact and risk |
|---|---|---|---|---|---|
| Retry deferred Windows guest commits up to about ten seconds | `c3aa97d952` | Current allocator commits backing upfront | Not transplantable alone | No | Retries all errors and cannot cancel; may recover transient commit pressure but stalls the caller. Requires the older deferred-access contract and Windows exhaustion testing. |
| Reinitialize on any memory-layout enum change | `bce3c83739` | Yes, `d5b2582104` from upstream PR #472 | Yes | No duplicate | Prevents incorrect backing-size reuse, not a steady-state FPS improvement. |
| AVX-512 CLZ narrows all eight lanes | `e902aca643` | Yes, Lichii `9c34939757` / `8cd5c6a44b` | Yes | No duplicate | Copied from Lichii originally. No effect on the owner's AVX2-only Ryzen 5900X. |
| Disable all AVX-512 JIT paths by default | `59a1b82e04` | No | Unproven broad workaround | No | CPU-dependent frequency/throughput tradeoff; diagnose specific emitters rather than disable an ISA family globally. |
| Scan and evict apparently zero Windows mapped pages | `d8c95fef47` | No | Unsuitable general policy | No | Reads whole 1-MiB chunks when any page is resident, potentially faulting pages in. Detached worker has no lifecycle and can outlive logging/game shutdown. |
| Default Windows process high priority | `d8c95fef47` | No; selected worker priority already exists | Host scheduling tradeoff | No | Raises UI/compiler/background work too; may starve competing work without reducing emulator work. |
| Suppress unchanged TAS inputs and repeated idle resets | `d8c95fef47` | No | Adapted to authoritative state instead of the fork's cache | Yes | Reduces callback dispatch during TAS playback/idle clearing. Reads GetButton/GetAxis so configuration-skipped writes and public resets cannot leave a stale shadow cache. No game-FPS claim. |
| TAS autostart and flushed per-frame benchmark file | `d8c95fef47` | No exact helper | Diagnostic needs redesign | No | Existence-based autostart treats '=0' as enabled; static file spans sessions. Per-frame fflush perturbs measurement; Qt display callbacks are not scanout times. |
| Unsafe CPU/GPU/DMA, async, VRAM/cache/flushing defaults | `54b34dedb3` | Existing options, not the same global defaults | Workload/accuracy tradeoffs | No | Skip as requested. Fewer waits, lower accuracy or missing work cannot establish an equivalent-code speedup. |
| Half resolution, handheld, filter/sharpen/post-processing defaults; MKW fence override | `54b34dedb3` | Some defaults already match; MKW presets exist | Quality/user-policy choices | No | Skip as requested. Match effective settings in comparisons; override block can replace a custom global preference. |
| Memory default 8 GiB then back to stock | `54b34dedb3`, `51a140cc4f` | Final default matches | No net change | No | Do not confuse this with the full-enum reinitialization fix. |
| ASTC stays uncompressed rather than imported BC3 default | `114b5b06bd` | Current default BC3 | Quality/VRAM tradeoff | No | More VRAM on non-ASTC hosts; investigate dark-ground format issue instead of reversing all titles' defaults. |
| Periodic PGO profile writes in training builds | `c4137dc795` | No exact helper | Training convenience, incomplete lifecycle | No | Detached infinite thread with no save-error handling. No normal-release execution benefit. |
| PGO/ThinLTO and Zen 3 then x86-64-v3 flags/profile | `c4137dc795`, `64685fe34a`, `d8c95fef47` | LTO and architecture selection supported | Requires matched validation | No build changes | Recommendation below. Do not import stale single-game profile or suppress coverage warnings blindly. |

### Diagnostics and import-only changes

| Change | Selevet commit | In Lichii already? | Correctness / impact | Ported here? |
|---|---|---|---|---|
| Pipeline worker-wait aggregation | `c4137dc795` | No exact counter; related pipeline profiling exists | Diagnostic only, clocks/logs under build mutex; assumes one worker | No |
| Per-drain-site timings | `d8c95fef47` | Yes, newer per-thread implementation | Fork's optional timer emplacement destroys a temporary and later its copy, double-counting waits | No duplicate |
| Compile out profiling scopes | `d8c95fef47` | Runtime profiling defaults off | Incomplete compile-out leaves other counters/timers; unmeasured build tradeoff | No |
| Command/acquire download statistics | `d8c95fef47`, `bce3c83739` | Overlapping command/drain diagnostics | Some clocks run even when stats are off | No |
| Disable old automatic upscale sampling/tracing | `8b076b167f` | Affected old path is gone; current captures opt-in | Sensible cleanup, no current port target | No |
| Compute/fragment cbuf handle scans; repeated/before-upscale samples; trace_frame file trigger | `59a1b82e04`, `c3aa97d952` | Actual-binding sampled frame dumps and live capture exist | Guessed handles can be false positives; downloads stall. File trigger probes filesystem every fallback upscale and static state spans games | No |
| Grid-based compute skipping | `c3aa97d952` | Full-hash rejection exists | Distinct shaders can share a grid; deliberately omits writes | No |
| Root-table, handle-program, deferred-flush and lazy-descriptor off switches | `c3aa97d952` | Actual upstream fixes are unconditional | Only the A/B switches are unique; disabling can drop required bindings/state or add clearing work | No |
| Video post-stop/EOS/empty-reply traces and scaled/software Fermi2D blit logs | `c3aa97d952` | Opt-in video/frame tracing exists | Logs do not implement the claimed gameplay fixes; unnecessary always-on output | No |
| SVC per-thread ring dumped on Break | `c3aa97d952` | Different SVC tracing exists | Process-global mutex, unbounded static thread map, cross-process handle ambiguity and edits to generated source | No |
| ClearEvent wrong-type warning | `c3aa97d952` | Arguments/results visible through SVC tracing | Low-risk diagnostic but ungated/unlimited, no speed or behavior fix | No |
| Graphics-pool allocation free-space diagnostic | `114b5b06bd` | Simpler allocation-error log exists | Failure-path diagnostic, no allocation or performance change | No |
| Missing audio-out sources and removed release workflow | `c4137dc795` | Lichii retains them | Snapshot omissions, not optimizations; fork CMake still names the missing audio sources | No |
| Icon/README/package switch files, CRLF and executable-mode changes | Multiple | Branding/build packaging differs | Not runtime optimizations; shipped unsafe switches change comparisons materially | No |

Build-104/build-131 imports already cover sparse tile backing, task/mesh support,
NUP/loose loaders, vi2 events/display queries, camera services, root tables,
deferred instance flushing, command snapshots, uploaded-memory cold hints, buffer
GC thresholds, pipeline retention/eviction, staging release, vertex repacking,
Mii stub reply shapes and named SVC waits. They are not attributed to Selevet.
The duplicated written-page mark and fork-specific reconciliation policies are
listed separately above.

The `Written` sparse-block state transitions in `53c39dfed6` adapt build-104's
`4d6a13e3d9` code to an inherited Lichii enum. After normalizing those enum names,
the tile lifecycle functions match that upstream implementation; current nx2 also
has newer overflow-safe request bounds. Inherited generation-checked pending-block
tokens are separate older Lichii work, not a new Selevet optimization.

Older Lichii work retained in the snapshot but absent or changed in current nx2
includes deferred Windows backing, idempotent Windows cleanup, JIT commit-failure
checks, and bounds-checked direct NSO decompression. These originate in
`60ef6d2750`/`badc91ebce`, not Selevet. Restoring them requires separate scoped
review, especially the deferred-memory contract; they were not silently bundled
with a purported fork performance fix.

## Mapping equivalence evidence

`tools/nx2/test_mapping_table_equivalence.py` extracts the two pinned historical
headers from git, renames their namespaces in temporary copies, and compares
exact updates and state. It checks ordered change ranges, map/native flags,
success, lease IDs, effective mappings, internal mappings/leases, lookups,
backing/native overlap and image bounds. It does not modify production headers.

```sh
git remote add selevet https://github.com/sylvieistaken/selevet-eglantine.git
git fetch selevet main
git fetch origin badc91ebce6b6cb44eabe67f7aa0dbbc6abd9e46
python3 tools/nx2/test_mapping_table_equivalence.py --cxx clang++-20
python3 tools/nx2/test_mapping_table_equivalence.py --cxx g++-13 \
  --sanitize --seed 0xC4137DC795 --operations 250000
```

Skip `remote add` when that remote is already configured. The explicit origin
fetch supplies the historical reference even in a shallow checkout. An optional
`--work-dir` selects the parent directory for temporary compiler files.

Both passed. Clang compared 2,000,000 randomized operations plus 4,440 fixtures,
including 1,184,564 returned changes. GCC 13 ASan/UBSan passed another 250,000
randomized operations and those fixtures with a different seed. Cases include
zero/overflow/max addresses, splits, duplicate/nested/adjacent leases, alias
conflicts, stale release IDs, reset and 1,024 disjoint mappings.

The historical algorithm is equivalent because validated updates affect only
their interval and mappings are sorted/disjoint. It still scans all leases and
trims the mapping vector. Current `Nx2GpuMemoryRegistry` instead preserves
handle/offset metadata, trims its registry and emits the requested interval to
GPU-ordered listeners directly. It has no `Effective()`/`Difference()` work to
remove. Adding the old optimized class alone would create unused code.

## Release workflow recommendation

Train a new PGO profile with Lichii's release compiler/ABI and representative
games, cold/warm shader states and gameplay paths. Record profile coverage and
staleness instead of hiding the warnings as the fork's cache files do. Its bundled
profile predates later source edits and was trained on one game's workload; do
not reuse it across MSVC and MinGW ABIs or assume its benchmark numbers transfer.

Evaluate ThinLTO with the existing kernel-TLS LTO exclusion intact. The prior
integration documents why migrating fibers require those TLS lookups to remain
out of line. Run the full tests and compare matched routes, effective settings,
driver, game version and shader-cache state before enabling a release variant.

Keep a baseline x86-64 release. An explicitly labelled x86-64-v3 build is reasonable
for AVX2-capable hosts, but raising the only binary's minimum excludes older CPUs.
Native compiler targeting and Dynarmic's runtime guest-code ISA selection are
different decisions. The owner's Ryzen 5900X does not benefit from disabling
AVX-512 paths it cannot execute.

Measure guest frame times, GPU/CPU contention, visual correctness, audio and
shutdown behavior, not just displayed FPS. Half-resolution, discarded draws,
altered guest timing and generated NVOF frames must be separated from equal-output
algorithm improvements. No release flags, game defaults or unsafe experiment
switches are changed by this branch.

## Validation

The usage-tracker boundary tests also found an existing shift by 64 when a mask
contains zero bits, including exact page-end tails. Cross-review caught that a
zero-mask guard alone would lose nonempty ranges smaller than 64 bytes. The
companion fix covers every intersecting block, including partial blocks,
and returns zero only for an empty range. This is not attributed to Selevet.

TAS tests exercise real playback and callback dispatch, including unchanged and
changed frames, shorter player scripts, stop/restart, loop/completion, reload,
configuration entry/exit, public resets and rebind ForceUpdate. They do not rely
on a second cache of supposedly applied input. Usage tests compare reset/reused
trackers with fresh trackers and cover repeated resets, recent-range reuse,
empty ranges, sizes 1 through 63, unaligned tails and exact page boundaries. A
separate byte bitmap is the oracle for the no-false-negative requirement; the
fresh-tracker comparison alone cannot prove that property.

Clang 20.1.2 and Ninja built `tests` and `yuzu-cmd` in Release mode with Qt disabled.
The final full tests binary passed **15,846,410 assertions in 658 test cases** after the
review correction. The focused usage suite passed **95,392 assertions in five
cases** with Clang `-O2 -Wall -Wextra -Werror` and GCC 13 UBSan. The TAS suite
passed **209 assertions in eight cases**, and is also included in the full run.
Historical mapping results are recorded above.

The full GCC 13 Release build passed with `-Werror` on project targets, and its
tests binary passed **15,957,266 assertions in 658 test cases**. Vendored libraries
retain their own warning policy. No warning suppression or
release-configuration change is committed. An initial `-O1` verification build
hit a GCC maybe-uninitialized diagnostic in the unchanged cheat VM's
`std::variant`; the same translation unit compiled cleanly at standard Release
`-O3 -Werror`, so the full verification uses standard Release optimization.

Both build configurations use the repository-pinned fmt and SPIRV-Headers rather
than Ubuntu's older packages, bundled SDL3, tests enabled, and these optional
components disabled: Qt, room/server, web service and LLVM disassembly. Example
Clang configuration:

```sh
cmake -S . -B build/clang20 -G Ninja \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 \
  -DCMAKE_BUILD_TYPE=Release -DENABLE_QT=OFF -DYUZU_CMD=ON \
  -DYUZU_TESTS=ON -DDYNARMIC_TESTS=OFF -DYUZU_ROOM=OFF \
  -DYUZU_ROOM_STANDALONE=OFF -DYUZU_USE_BUNDLED_SDL3=ON \
  -DENABLE_WEB_SERVICE=OFF -DYUZU_DISABLE_LLVM=ON \
  -Dfmt_FORCE_BUNDLED=ON -DSPIRV-Headers_FORCE_BUNDLED=ON
cmake --build build/clang20 --target tests yuzu-cmd --parallel 4
build/clang20/bin/tests
```

The GCC build uses the same options with `gcc-13`/`g++-13`, `ENABLE_WERROR=ON`
and the normal `-O3 -DNDEBUG` Release flags. To add literal `-Werror` to project
targets without overriding vendored libraries' policies, the verification used
this temporary file through `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`:

```cmake
function(project_werror directory)
    if(directory MATCHES "/src/dynarmic($|/)")
        return()
    endif()
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(kind "${target}" TYPE)
        if(kind STREQUAL "STATIC_LIBRARY" OR kind STREQUAL "SHARED_LIBRARY" OR
           kind STREQUAL "OBJECT_LIBRARY" OR kind STREQUAL "EXECUTABLE")
            target_compile_options("${target}" PRIVATE -Werror)
        endif()
    endforeach()
    get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(child IN LISTS children)
        project_werror("${child}")
    endforeach()
endfunction()
cmake_language(DEFER CALL project_werror "${CMAKE_SOURCE_DIR}/src")
```

All 1,266 project compile commands contain `-O3` and `-Werror`; existing
repository warning exclusions remain in force.

Cross-model review by Claude Sonnet 5.5 found the small-range mask regression in
the first draft. That draft was corrected before publication, and the reviewer
reported no remaining blockers after checking the final masks with an independent
sanitized byte-range test. Existing conservative false positives in same-page
usage queries remain; they can prevent reordering, not permit unsafe reordering.

Windows pagefile recovery, NVIDIA optical flow and the private
`b8d34f6ee4-selevet-perf5-lichii67` binary are not available in this public-source
audit. No emulator/game session was started or stopped.
