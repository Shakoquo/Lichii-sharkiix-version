# Bananza query diagnostics

`EDEN_DKB_FRAME_TRACE=1` enables `DKB_QUERY` and `DKB_CONDITION` lines in the normal
log. Keep `HW.GPU` and `Render.Vulkan` at Info or more verbose. The switch is read
once per process; restart after changing it. It does not enable the separate
`EDEN_NATIVE_DRAW_DIAGNOSTICS` CSV trace.

Each event class logs its first 64 events, then samples every 1024th event with a
limit of one sampled line per second. `n` is a
process-wide counter within that class, not a frame number or a query ID. Requests,
writes, discarded completions, host results, CPU decisions, and Vulkan decisions
have separate counters. Sampling can omit either side of a correlation.

`DKB_QUERY request` records the requested and effective query types, mapping,
payload, flags, and subreport. Unsupported types retain the existing payload-1
fallback; `payload` is the effective payload. `write` records the value actually
written by immediate, completion, semi-flush, or direct-stamp paths. `bytes` is the
result width, excluding any timestamp. Four-byte writes show the truncated value.
`discard` means the completion did not write because its query was invalidated or
unsynchronized.

`host-result` records occlusion sums and transform-feedback values when the existing
readback completes. These are before accumulation amendment; the completion write
shows the amended result. Match `device` to requests and writes, then match their
`gpu` address to conditions. `device` is an emulated device-memory address, not a
host pointer. `as` is the GPU address-space ID where available. Addresses and
stream slots can be reused; the trace does not assign unique query generations.

`DKB_CONDITION cpu` records the actual `execute` decision. Modes are 0=false,
1=true, 2=conditional, 3=equal, and 4=not-equal. Overrides are 0=use-mode,
1=always-render, and 2=never-render. `initial` and `current` contain the sequence
and mode words already read for that decision. When `operands_read=false`, their
zero values are placeholders, not memory observations.

Vulkan `decision=GPU-deferred predicate=unknown` means an existing GPU predicate
was configured, not that it passed. `inverted` is the configured Vulkan inversion
flag. `cpu-fallback` leaves the decision to the CPU path. `execute-bypass` identifies
the existing driver, accuracy, or query-cache branches that disable the predicate
and allow submission. None of these diagnoses a rendering bug by itself.

No extra guest-memory reads, query waits, GPU readbacks, or predicate changes are added.
Host results are logged only when the existing path makes them available; they do
not prove which value a particular GPU draw consumed. Logging still adds CPU and
I/O overhead. Windows and Bananza runtime validation must be done on the user's
machine; no game was run for this change.
