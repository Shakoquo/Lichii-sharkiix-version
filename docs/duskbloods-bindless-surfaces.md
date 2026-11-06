# Duskbloods register-handle image instructions

The static CGSB shader corpus contains formatted image instructions whose handle is
in a general register. The existing `0xb97` and `0xb9b` lowerings only cover handles
in a descriptor constant buffer. `0x998` and `0x99c` are separate register-handle
encodings, not aliases for the direct-descriptor encodings.

NVIDIA nvdisasm 12.8.90, invoked with `-b SM87`, decodes these captured words:

| Words | Instruction |
| --- | --- |
| `0200799c 60000004 00100f00 004fe200` | `SUST.P.2D.IGN [R2], R4, R0` |
| `10007998 60000000 001e0f1d 010f6600` | `SULD.P.2D.IGN R0, [R16], R29` |
| `00037998 a0000000 001e0119 010f6200` | `SULD.P.3D.R.IGN R3, [R0], R25` |
| `0000799c a0000004 00100f03 0101e200` | `SUST.P.3D.IGN [R0], R4, R3` |
| `0000799c 8000000c 00100f07 0041e200` | `SUST.P.2D_ARRAY.IGN [R0], R12, R7` |

`ampere_bindless_surfaces.cpp` retains those instruction words, not complete game
binaries. The corresponding emitted Maxwell instructions were also decoded with
nvdisasm `-b SM52`; their handles, coordinates, channels and data operands match.
The lowering uses the existing Maxwell bindless image path and does not select or
replace the program's descriptor constant buffer.

The first two regressions come from `CopyDepthCS.cpo` at instruction offset 256
and `GXDenoiseReproject_8x8.cpo` at offset 976. Their instruction-allocation SHA256s
are, respectively:

```text
253d8c360ba84fdbcde32a2339c70c994cee7858e5003fad508640ca45c0cb32
9a8292a8495dced116ef0833243a9ddb04b9dba1cfac76ae10c7d9f7f27d25d8
```

The accepted forms use `.P`, `.IGN`, dimensions 2D/2D array/3D, and the captured
memory modes. Loads preserve the channel mask and require the destination alignment
that the Maxwell frontend supports. Stores accept R, RG and RGBA masks. Other
memory modes, reserved fields, unsupported dimensions, zero masks, and register
spans that overlap lowering scratch registers fail closed.

## Validation limits

On nx2 `935c5bdb03`, all 106 supplied cases failed native lowering. This change makes
all 47 cases initially rejected at `0x99c` and all 33 initially rejected at `0x998`
pass complete captured-extent lowering. The other 26 still reject `0x499`, which
nvdisasm identifies as `USHF`, not an image atomic instruction.

This is lowering-only evidence. Compute workgroup sizes and resource descriptors
were not captured, so this result does not establish full pipeline compilation or
in-game rendering. No launch dimensions or resources were invented for this replay.
