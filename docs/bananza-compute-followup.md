# Bananza compute compilation follow-up

The live trace proves that compute shaders reach an unimplemented SPIR-V emitter. It does not identify the IR opcode, and it does not prove that these failures cause the reported frame cadence. This change preserves the missing diagnostic context; it does not enable an unverified shader operation.

The investigation uses `~/.capy/work/dkb/dkb-frame-trace-live.txt` and source base `8644f32`, after the upstream merge and the first Bananza diagnostic build. No game was run during this investigation.

## What the log establishes

At line 1155, time 36.198010 seconds, `CreateComputePipeline` reports `SPIR-V Instruction is not implemented`. Line 1157 identifies the immediately rejected compute dispatch at `0x211ddd9000`, but its reason is only `Native compute pipeline unavailable`.

The log contains nine occurrences of the original SPIR-V error, at 36.198010, 64.968596, 64.978761, 66.527823, 66.537259, 66.568497, 66.601756, 67.051230, and 83.496157 seconds. The rate-limited rejection trace names seven distinct addresses:

- `0x211ddd9000`
- `0x21ebdfa000`
- `0x21ec779500`
- `0x2120633c00`
- `0x21205e4000`
- `0x21ec94f000`
- `0x2120614d00`

The later errors cannot all be paired with an address from this trace. `DispatchNativeCompute` rate-limits its trace across dispatches, and its `count` accumulates rejections across addresses. The printed count is not a per-shader failure count.

`EmitSPIRV` runs before `BuildShader` and before the `ComputePipeline` constructor. The exact error text comes from 95 explicit `NotImplementedException("SPIR-V Instruction")` sites in the SPIR-V backend. Candidates include global byte and halfword memory operations, global atomics, image atomics, and unsupported IR types. The identical message cannot distinguish them.

The QMD records contain launch state and shader addresses, not the instructions at those addresses. The log explicitly records both `dump_guest_shaders` and `gpu_log_shader_dumps` as false. No shader capture accompanies this log in the supplied directory. There is no evidence to choose one of the unsupported emitters or invent a captured instruction sequence.

## Why the original cause disappears

`PipelineCache::CreateComputePipeline` catches `Shader::Exception`, logs `what()`, and previously returned only a null pipeline. `CurrentComputePipeline` retains that null result under the shader hash, shared-memory size, and workgroup dimensions. On later dispatches it finds the existing key and does not compile again.

The rasterizer previously saw only that null pointer and threw `Native compute pipeline unavailable`. The compute engine therefore lost the compiler's reason even on the first dispatch. Repeated generic messages are not evidence of repeated shader compilation; negative caching was already present.

The native decoder is not the demonstrated failure point: its rejected-opcode errors include native program and instruction details, while this message originates in the SPIR-V emitter. This is also not a Vulkan driver shader-module rejection: the failing emission occurs before `BuildShader`.

Failed dispatch does not withhold QMD releases. `NativeQmdScheduler::Signal` in `src/video_core/native_qmd.h` records the dispatch failure, then marks the QMD complete, runs its release slots, and handles dependent QMDs. The five `ChannelSetTimeout` lines configure a timeout; they do not report one. The live trace has no four-second `WaitForFrame` timeout. Neither the QMD error nor these timeout-setting lines explain a 40 ms cadence.

## Upstream comparison

The requested fetched revisions were checked before changing code:

| Ref | Pinned head | Relevant result |
| --- | --- | --- |
| `upstream/pr301` | `5bfdb6836b4fe8cd75572e61d41d50686ee77f9d` | Includes `7f027b2` for LDC, F2I, and VOTEU forms, plus storage-cache capacity. The local tree already supports these forms and uses `Shader::Info::MAX_SSBOS`, currently 32. No SPIR-V emitter implementation changes are in this PR. |
| `upstream/pr306` | `2c5b90b7adde8c926c35cd800b1c4994feda2e5a` | Its head changes Ampere LDC, F2I, LEPC, IMAD.X, and VOTEU handling. Its additional ancestry includes `FPTanh` support, but no evidence connects that new operation to any of the identical error sites in this log. |
| `upstream/pr363` | `044bb10c4534118e6666ade5219dc8d4af6b0e2e` | Adds geometry, reset, and address-tracking changes already compared in `bananza-upstream-comparison.md`. Its SPIR-V change selects NVN descriptors by guest slot; that selective port is already in this build. It does not implement the unidentified failing emitter. |

No opcode implementation was ported in this follow-up. Broadly importing these patches would not establish which operation failed and could discard local validation or unrelated merge work.

## Diagnostic change

`EmitInst` now adds the IR opcode name to `Shader::Exception` before rethrowing the same exception. The original reason and exception subtype survive. The annotation occurs only on failure and does not change emitted SPIR-V.

The Vulkan compute cache now retains a `ComputePipelineCacheEntry` containing the pipeline and its compilation-failure text. Successful pipelines follow the existing path. A failed native lookup throws a message containing the current guest address, shader hash, full pipeline-key hash, and retained cause. The current address is supplied at lookup time, so reuse of a content-keyed failure at another guest address does not report the old address.

Legacy failed lookups still return null. Native failed dispatches still throw `Shader::RuntimeError` and take the compute engine's existing failure path. Cache insertion still uses `try_emplace`, and compilation still occurs only for a new key. Failed results remain cached; no per-frame retry was added. Disk preload still inserts only successful pipelines, and failure text is not serialized.

The first compilation error includes both shader and pipeline hashes. The existing compute-engine log will now retain details in this form:

```text
Native compute pipeline unavailable: address=<guest address> shader=<shader hash> pipeline=<key hash>: SPIR-V emission failed at IR opcode <name>: <original reason>
```

The opcode is an optimized IR opcode, not a guessed native instruction address. Keep `EDEN_DKB_FRAME_TRACE=1` to retain the existing timing/rejection trace. The original compute rejection log also reports each distinct detailed error without that switch. Enabling the existing `dump_guest_shaders` setting can additionally dump guest code before emission; `gpu_log_shader_dumps` alone cannot capture a module whose emission failed.

## Verification

The new emitter regression uses two synthetic unsupported operations, `UndefU8` and `UndefU16`. They are not claimed to be Bananza's failing instructions. Before the diagnostic change, both opcode-name assertions failed while the original exception text matched. Afterward, both names and the original reason survive in `NotImplementedException`.

The cache-entry regression repeats lookups of the same failed key at two guest addresses. It checks retained cause text, the current address and hashes, unchanged legacy-null behavior, native rejection, and isolation of a different workgroup key.

Clang 20 and Ninja built the integrated headless emulator and tests using all eight cloud cores. All **486 test cases passed**, both with tracing disabled and with `EDEN_DKB_FRAME_TRACE=1 EDEN_NX2_GPU_PROFILE=1`. The focused `[diagnostics]` filter passed 24 assertions across the two new failure-context cases. The existing build configuration keeps Werror disabled for unrelated baseline warnings.

The pre-integration runner linked real shader and Vulkan cache code; 14 adjacent cases passed with 7,255 assertions. The new opcode regression had two failing assertions before the diagnostic change. No Vulkan device or game was launched. The two modified test files were already registered, so this diagnostic change does not alter their CMake registration.
