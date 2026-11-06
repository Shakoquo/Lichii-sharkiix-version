# Before: shader sweep failures

Generated from the full replay described in [the sweep report](shader-sweep-20261104.md).
Successful compilations are omitted. Context failures are incomplete captures, not compiler defects.

| Game | Stage | Phase | Opcode | Cause | Count |
|---|---|---|---|---|---:|
| Mario Kart World | fragment | context |  | Uncaptured storage pointer classification at 0x... | 809 |
| Mario Kart World | vertex | context |  | Uncaptured storage pointer classification at 0x... | 352 |
| Donkey Kong Bananza | vertex | context |  | Uncaptured storage pointer classification at 0x... | 253 |
| Donkey Kong Bananza | fragment | context |  | Uncaptured storage pointer classification at 0x... | 251 |
| Yoshi demo | vertex | context |  | Uncaptured storage pointer classification at 0x... | 125 |
| Donkey Kong Bananza | vertex | process |  | compiler exited -6:  | 124 |
| Fire Emblem: Fortune's Weave | vertex | context |  | Uncaptured storage pointer classification at 0x... | 120 |
| Mario Tennis Fever | geometry | validate |  | error: line 83: Operand 3 of Decorate requires one of these capabilities: Tessellation    OpDecorate %gl_TessCoord BuiltIn TessCoord | 108 |
| Fire Emblem: Fortune's Weave | fragment | context |  | Uncaptured storage pointer classification at 0x... | 98 |
| Mario Kart World | compute | context |  | Uncaptured storage pointer classification at 0x... | 89 |
| Yoshi demo | fragment | context |  | Uncaptured storage pointer classification at 0x... | 83 |
| Nintendo Switch Sports Resort | fragment | context |  | Uncaptured storage pointer classification at 0x... | 66 |
| Nintendo Switch Sports Resort | vertex | context |  | Uncaptured storage pointer classification at 0x... | 59 |
| Donkey Kong Bananza | compute | context |  | Uncaptured storage pointer classification at 0x... | 27 |
| Fire Emblem: Fortune's Weave | compute | context |  | Uncaptured storage pointer classification at 0x... | 25 |
| Star Fox | compute | context |  | Uncaptured storage pointer classification at 0x... | 17 |
| Yoshi demo | compute | context |  | Uncaptured storage pointer classification at 0x... | 17 |
| Nintendo Switch Sports Resort | compute | context |  | Uncaptured storage pointer classification at 0x... | 15 |
| Kirby Air Riders | vertex | context |  | Uncaptured storage pointer classification at 0x... | 5 |
| 040057c02159a000 | vertex | context |  | Uncaptured storage pointer classification at 0x... | 4 |
| Kirby Air Riders | compute | context |  | Uncaptured storage pointer classification at 0x... | 4 |
| Kirby Air Riders | fragment | context |  | Uncaptured storage pointer classification at 0x... | 4 |
| 040057c02159a000 | fragment | context |  | Uncaptured storage pointer classification at 0x... | 2 |
| Nintendo Switch Sports Resort | vertex | process |  | compiler exited -6:  | 2 |
| Donkey Kong Bananza | geometry | context |  | Uncaptured storage pointer classification at 0x... | 2 |
| Donkey Kong Bananza | geometry | process |  | compiler exited -6:  | 2 |
| Mario Tennis Fever | tess-control | lower | 0x923 | native stage buffer access mode | 2 |
| Mario Tennis Fever | tess-eval | lower | 0x321 | native indexed/patch attribute access | 1 |
| Mario Tennis Fever | vertex | context |  | Graphics compilation requires a native header or captured environment | 1 |
| Yoshi demo | compute | context |  | Uncaptured texture pixel format at 0x... | 1 |
