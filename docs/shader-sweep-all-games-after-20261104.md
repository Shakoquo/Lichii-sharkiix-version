# After: static library shader diagnostics

Counts are code/control allocations, not distinct bytecode or runtime draws.
Context gaps are not compiler successes or demonstrated compiler bugs.
See [the coverage report](shader-sweep-all-games-20261104.md).

| Game | Stage | Phase | Opcode | Cause | Count |
|---|---|---|---|---|---:|
| Donkey Kong Bananza | fragment | context |  | Uncaptured storage pointer classification at 0x... | 3645 |
| Nintendo Switch Sports Resort | fragment | context |  | Uncaptured constant buffer value at 0x... | 3255 |
| Donkey Kong Bananza | fragment | context |  | Uncaptured constant buffer value at 0x... | 2901 |
| Raiders | fragment | context |  | Uncaptured storage pointer classification at 0x... | 2387 |
| Mario Kart World | fragment | context |  | Uncaptured storage pointer classification at 0x... | 2036 |
| Fire Emblem: Fortune's Weave | fragment | context |  | Uncaptured constant buffer value at 0x... | 1535 |
| The Duskbloods network test | fragment | context |  | Uncaptured storage pointer classification at 0x... | 540 |
| The Duskbloods network test | compute | context |  | compute launch and resource context not established | 538 |
| The Duskbloods network test | fragment | context |  | Uncaptured constant buffer value at 0x... | 431 |
| Raiders | vertex | context |  | Uncaptured constant buffer value at 0x... | 401 |
| Mario Kart World | vertex | context |  | Uncaptured constant buffer value at 0x... | 357 |
| Mario Kart World | vertex | context |  | Uncaptured storage pointer classification at 0x... | 338 |
| Nintendo Switch Sports Resort | vertex | context |  | Uncaptured storage pointer classification at 0x... | 330 |
| Fast Fusion | fragment | context |  | Uncaptured constant buffer value at 0x... | 302 |
| Nintendo Switch Sports Resort | vertex | context |  | Uncaptured constant buffer value at 0x... | 198 |
| Raiders | fragment | context |  | Uncaptured constant buffer value at 0x... | 167 |
| Donkey Kong Bananza | vertex | context |  | Uncaptured constant buffer value at 0x... | 143 |
| Fire Emblem: Fortune's Weave | vertex | context |  | Uncaptured storage pointer classification at 0x... | 127 |
| Mario Kart World | fragment | context |  | Uncaptured constant buffer value at 0x... | 109 |
| The Duskbloods network test | mesh | context |  | unverified mesh bytecode envelope and launch metadata | 88 |
| Fire Emblem: Fortune's Weave | vertex | context |  | Uncaptured constant buffer value at 0x... | 78 |
| The Duskbloods network test | vertex | context |  | Uncaptured storage pointer classification at 0x... | 72 |
| Nintendo Switch Sports Resort | fragment | context |  | Uncaptured storage pointer classification at 0x... | 70 |
| Fast Fusion | compute | context |  | compute launch and resource context not established | 66 |
| The Duskbloods network test | tess-eval | context |  | Uncaptured storage pointer classification at 0x... | 57 |
| Fire Emblem: Fortune's Weave | fragment | context |  | Uncaptured storage pointer classification at 0x... | 52 |
| Drag x Drive Global Jam | vertex | context |  | Uncaptured constant buffer value at 0x... | 50 |
| Raiders | fragment | context |  | Native allocation lacks a complete runtime capture | 44 |
| Fast Fusion | vertex | context |  | Uncaptured constant buffer value at 0x... | 36 |
| Nintendo Switch Sports Resort | vertex | lower | 0x4a4 | unsupported native opcode | 36 |
| The Duskbloods network test | compute | lower | 0x98e | native global reduction mode | 36 |
| Donkey Kong Bananza | fragment | context |  | Native allocation lacks a complete runtime capture | 33 |
| Raiders | fragment | lower | 0x624 | native multiply wide addend mode | 24 |
| The Duskbloods network test | tess-control | context |  | Uncaptured storage pointer classification at 0x... | 24 |
| Fast Fusion | fragment | context |  | Native allocation lacks a complete runtime capture | 20 |
| Fire Emblem: Fortune's Weave | compute | context |  | compute launch and resource context not established | 16 |
| The Duskbloods network test | geometry | lower | 0xb66 | native instruction before exit setup state is dead | 16 |
| Raiders | vertex | context |  | Native allocation lacks a complete runtime capture | 15 |
| Drag x Drive Global Jam | fragment | lower | 0x89a | unsupported native opcode | 12 |
| The Duskbloods network test | geometry | lower | 0x321 | native instruction before exit setup state is dead | 11 |
| Fire Emblem: Fortune's Weave | fragment | context |  | Native allocation lacks a complete runtime capture | 10 |
| Fire Emblem: Fortune's Weave | geometry | lower | 0x321 | native instruction before exit setup state is dead | 10 |
| Nintendo Switch Sports Resort | fragment | lower | 0x326 | native register shadowing mode | 10 |
| Raiders | vertex | lower | 0x88c | native uniform comparison mode | 10 |
| The Duskbloods network test | geometry | lower | 0x890 | native instruction before exit setup state is dead | 9 |
| Donkey Kong Bananza | geometry | context |  | Uncaptured storage pointer classification at 0x... | 6 |
| Raiders | geometry | lower | 0x947 | native instruction before exit setup state is dead | 6 |
| The Duskbloods network test | task | context |  | unverified task bytecode envelope and launch metadata | 6 |
| Drag x Drive Global Jam | fragment | context |  | Uncaptured constant buffer value at 0x... | 5 |
| Fire Emblem: Fortune's Weave | compute | context |  | Native allocation lacks a complete runtime capture | 5 |
| Fast Fusion | compute | lower | 0x981 | native global load uniform addressing mode | 4 |
| Fast Fusion | fragment | context |  | Uncaptured storage pointer classification at 0x... | 4 |
| Nintendo Switch Sports Resort | fragment | lower | 0x202 | native register shadowing mode | 4 |
| Nintendo Switch Sports Resort | fragment | lower | 0x4a4 | unsupported native opcode | 4 |
| The Duskbloods network test | fragment | lower | 0x624 | native multiply wide addend mode | 4 |
| The Duskbloods network test | fragment | lower | 0xf63 | native texture gather mode | 4 |
| Fast Fusion | compute | context |  | Native allocation lacks a complete runtime capture | 3 |
| Fire Emblem: Fortune's Weave | compute | context |  | Uncaptured storage pointer classification at 0x... | 3 |
| Raiders | vertex | lower | 0x28c | native uniform comparison mode | 3 |
| The Duskbloods network test | fragment | lower | 0xb66 | native texel fetch mode or descriptor binding | 3 |
| Fast Fusion | vertex | context |  | Native allocation lacks a complete runtime capture | 2 |
| Mario Kart World | vertex | context |  | Native allocation lacks a complete runtime capture | 2 |
| Mario Kart World | vertex | lower | 0x634 | unsupported native opcode | 2 |
| Nintendo Switch Sports Resort | fragment | lower | 0x220 | native program uses lowering scratch registers | 2 |
| Nintendo Switch Sports Resort | fragment | lower | 0x220 | native register shadowing mode | 2 |
| Nintendo Switch Sports Resort | fragment | lower | 0x423 | native register shadowing mode | 2 |
| Raiders | geometry | lower | 0x81c | native instruction before exit setup state is dead | 2 |
| Raiders | geometry | lower | 0x94d | native instruction before exit setup state is dead | 2 |
| The Duskbloods network test | compute | lower | 0x364 | unsupported native opcode | 2 |
| The Duskbloods network test | fragment | context |  | Native allocation lacks a complete runtime capture | 2 |
| The Duskbloods network test | fragment | lower | 0x817 | native integer min/max mode | 2 |
| The Duskbloods network test | fragment | lower | 0x890 | native integer carry mode | 2 |
| The Duskbloods network test | fragment | lower | 0xc08 | unsupported native opcode | 2 |
| The Duskbloods network test | geometry | lower | 0x20b | native instruction before exit setup state is dead | 2 |
| Fast Fusion | compute | lower | 0x386 | native global store register addressing mode | 1 |
| Fast Fusion | compute | lower | 0x38d | unsupported native opcode | 1 |
| Fast Fusion | compute | lower | 0x3c4 | native warp sum mode | 1 |
| Fast Fusion | compute | lower | 0x986 | native global store uniform addressing mode | 1 |
| Fast Fusion | vertex | context |  | Uncaptured storage pointer classification at 0x... | 1 |
| Fire Emblem: Fortune's Weave | compute | lower | 0x38d | unsupported native opcode | 1 |
| Fire Emblem: Fortune's Weave | compute | lower | 0x98e | native global reduction mode | 1 |
| The Duskbloods network test | fragment | lower | 0xb6f | native texture descriptor buffer conflict | 1 |
| The Duskbloods network test | geometry | context |  | Uncaptured storage pointer classification at 0x... | 1 |
| The Duskbloods network test | vertex | context |  | Uncaptured constant buffer value at 0x... | 1 |
