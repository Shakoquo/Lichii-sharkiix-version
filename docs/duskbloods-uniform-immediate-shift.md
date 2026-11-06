# Duskbloods uniform shift with an immediate high word

NVIDIA nvdisasm 12.8.90 with `-b SM87` identifies opcode `0x499` as the
immediate-high-word form of `USHF`, not a surface atomic instruction:

| Captured words | Instruction |
| --- | --- |
| `3f057499 ffffffff 08010604 000fe200` | `USHF.L.U32.HI UR5, URZ, UR4, 0xffffffff` |
| `3f057499 00000001 08010e04 000fc800` | `USHF.L.W.U32.HI UR5, URZ, UR4, 0x1` |

The first regression is at instruction offset 2624 in
`GXRend_TileBasedLighting16x16noloop.cpo`. The wrapped regression is at offset 3552
in `GXRend_TileBasedLightingOptPointSdw16x16noloop.cpo`. Their instruction-allocation
SHA256s are, respectively:

```text
d5f294f7ea611154468938e2450e325d11e8d29f1f5bea770dda69d274249bbb
72e54e083d76dc5c37b83c13f916ebcdb3cdedae865f53c3e941f11f5d9cc58a
```

With a zero low word, the selected high word is the immediate shifted left by
the uniform register's amount. Without `.W`, amounts of 32 or more produce zero.
With `.W`, the amount wraps modulo 32. The lowering materializes the full 32-bit
immediate, uses the existing uniform-register allocation and uniform-predicate
handling, and emits the corresponding Maxwell `SHL` mode.

Only the unsigned left-shift high-word form with `URZ` as the low word is
accepted. Other directions, widths, selected halves, nonzero low registers,
invalid register numbers and reserved fields remain rejected. Tests retain the
two captured instructions and separately exercise synthetic amounts around the
32-bit boundary, uniform guards, and high uniform-register allocation.

## Corpus result

The supplied corpus contains 26 cases initially rejected at `0x499`, including
one fragment shader. With only this fix applied to nx2 `935c5bdb03`, the fragment
shader passes captured-extent lowering. The other 25 advance past their uniform
shift and reject register-handle surface instructions: 24 at `0x998`, one at
`0x99c`. Those instructions are a separate root cause and fix.

These results test lowering, not complete pipelines. No matching compute launch
or resource environment was supplied, and no in-game rendering claim follows
from the offline result.
