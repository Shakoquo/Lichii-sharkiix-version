# Star Fox community crash investigation

The reports use Lichii `2b139e9889`. The service fix is based on `nx2`
`546cb67825b886898c974f2773e9426b62e6fdcc`. It fixes an incorrect IPC
reply demonstrated in the RTX 4060 Ti panic. It is not a verified fix for
the separate RTX 5090 Vulkan device loss.

## RTX 4060 Ti panic

At 18.161087 seconds, `stpl:u::0` command 300 returns an event. At
18.161718 seconds, the game sets termination result `0xca01`, then panics.
That result is `Kernel::ResultInvalidSize`, module 1, description 101.

The log's SDK build ID matches the owner's read-only SDK module:
`41d4de4a3b78214ad20c896960df0e5ea0ed595f`. All three decompressed NSO
segment hashes were verified. With SDK base `0x92941000`, the relevant
return addresses resolve to these callers. Symbol containment was checked
at the preceding instruction, rather than assigning the nearest symbol.

| SDK return offset | Caller |
| --- | --- |
| `0x16f7b0` | `nn::os::detail::SharedMemoryImplByHorizon::Map` |
| `0x16bcdc` | `nn::os::MapSharedMemory` |
| `0x772a88` | `nn::streamplay::detail::HidApplicationResourceHolder` constructor |
| `0x77a398` | `nn::streamplay::detail::GetUserService` |
| `0x770dcc` | `nn::streamplay::GetSupportedHidFeatureSet` |

The command-300 wrapper at `0x777ea0` enters the IPC serializer at
`0x778f70`. Its successful reply path reads two 64-bit configuration words
at `0x77904c`, and a copied handle at `0x77905c`. The holder constructor
passes the first word as shared-memory size, maps with permission 1, then
adds the second word to the mapped base. These are size and offset, not
event parameters.

The old service replies success with a readable event but no configuration.
The short reply leaves the client's following TLS words unchanged. They
are not guaranteed to be zero. The regression pre-fills them with
`0xa5a5a5a5` and observes that value where the SDK reads the size.
The panic's map implementation reaches its abort after an unexpected SVC
result. `MapSharedMemory` checks size alignment and nonzero size before
looking up the handle. The log does not record the actual malformed size.

## Upstream port and SDK adaptation

The allocation, permissions, lazy reuse and size/offset reply come from
[upstream commit 9750ad666f809b158a907e81ce24d433e1e5524f](https://github.com/Djihads80/Eglantine/commit/9750ad666f809b158a907e81ce24d433e1e5524f).
The identical implementation appears in
[535b6769709eb22333af26269f1b16f37adce4db](https://github.com/Djihads80/Eglantine/commit/535b6769709eb22333af26269f1b16f37adce4db),
[PR #1](https://github.com/Djihads80/Eglantine/pull/1). This is experimental
upstream branch work, not a fix present in upstream `nx2`. The unrelated
work-memory query in that patch is not included.

Both the service-manager fallback and the registered NGC service now use
one reply implementation. Each scoped service owns a zero-initialized
`KSharedMemory`; clients receive a copied handle with read-only mapping
permission and a 16-byte `{u64 size, u64 offset}` configuration.

Upstream allocates `0x84000` bytes. This SDK adds mouse-input storage
beginning at exactly that offset. `GetGuestNmouseHandles` at `0x775550`
addresses six groups of three `0x1400`-byte slots. The explicit scan in
`GetGuestNmouseStates` at `0x775a30` reaches the last slot at `0x99400`.
The region ends at `0x9a800`, so this port rounds the allocation up to
`0x9b000` bytes and returns offset zero. This is a sufficient page-rounded
size for the observed SDK layout, not a claim about a hardware service's
exact allocation size. Zeroed rings and style sets describe no connected
guests; GameShare streaming itself is not implemented.

## Regression and retest

Run `build/bin/tests '[streamplay]'`. The regression dispatches command 300
through both production service routes, in session and domain mode. It
checks the reply in client TLS, resolves the copied handle as
`KSharedMemory`, maps it through the real SVC, reads the full zeroed region,
checks size and permission rejection, and verifies repeated requests reuse
the object. Copied handles remain valid after the service is destroyed.
Before the fix, both routes left the configuration as the TLS sentinel.

After review, package with `EDEN_NX2_IPC_TRACE=1` and filter `*:Info`.
Have the 4060 Ti reporter repeat the same launch and progress beyond the
previous panic. Save the entire new log. Expect
`GameShare guest HID shared memory: size=0x9b000, offset=0, no guests connected`
and no `CA01` panic. Confirm visible progress separately; passing the IPC
regression does not prove the game remains playable.

## Separate RTX 5090 failure

The first null guest access occurs at 12.024 seconds; Vulkan device loss
ends the log at 86.772 seconds. There is no logged streamplay request or
guest panic. Rendering continues through a logged upscale at 48.416 seconds.
The updated main module matches build
`a7730d0f74d168d90422354c8317f76ff78ad938`; its segment hashes were verified.
The first logged guest PC is module offset `0xbe2ce60`. The surrounding
instructions load a pointer from an object field at `+0x28`, then increment
the value at that pointer plus eight. A null field explains the read/write
at address eight, but the available log does not identify its owner or
where the field became null. No containing symbol is available. This is
not evidence that those accesses caused the later Vulkan device loss.

The release already includes the ULDC and Star Fox visual fixes. Current
nx2 additionally includes selected GPU fixes from upstream #475, #329 and
#472 via Lichii #62, but none has been verified against this failure.
Retest current nx2 separately with the same game update and settings.
Record visible behavior, reproduction steps, and the full log. If supported
by the package, collect Nsight Aftermath output in a separate diagnostic
run, wait at least 15 seconds after device loss, and preserve `log/gpucrash`
before another launch clears it. The missing evidence is the failing Vulkan
operation and GPU fault data, plus the guest object's initialization path.

OpenSSL initialization assertions also occur during GUI-only game-list
discovery. Their cause is not established by these logs. Missing metadata,
airplane mode and the driver version are observations, not proven causes.
The CLI-only log has no ROM argument, and the other GUI log never boots a
game. Neither is an in-game reproduction.
