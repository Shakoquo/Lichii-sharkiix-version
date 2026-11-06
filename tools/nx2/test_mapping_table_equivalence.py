#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Compare the historical full-table and bounded-window mapping implementations.

Run from any directory with Python 3 and a C++20 compiler:
    python3 tools/nx2/test_mapping_table_equivalence.py --cxx clang++
    CXX=clang++ python3 tools/nx2/test_mapping_table_equivalence.py --sanitize

Both revisions must exist locally. Production sources are never modified. Scratch
headers only rename the namespace and expose private members for state/lookup checks.
This audits the historical optimization, not the current Nx2GpuMemoryRegistry.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile


OLD_REVISION = "badc91ebce6b6cb44eabe67f7aa0dbbc6abd9e46"
NEW_REVISION = "c4137dc79500428483e5c680f6a4a668bbb3532b"
HEADER = "src/core/hle/service/nvdrv/devices/nx2_gpu_mapping_table.h"

CXX_SOURCE = r"""
#include <array>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <iterator>
#include <random>
#include <stdexcept>
#include <string>
#include "reference.h"
#include "optimized.h"

using Reference = MappingReference::Nx2GpuMappingTable;
using Optimized = MappingOptimized::Nx2GpuMappingTable;
constexpr u64 Max = std::numeric_limits<u64>::max();

enum class Kind { Map, Unmap, Acquire, Release, Reset };

struct Operation {
    Kind kind;
    u64 gpu{};
    u64 cpu{};
    u64 size{};
};

template <typename A, typename B>
bool SameRange(const A& a, const B& b) {
    return a.gpu == b.gpu && a.cpu == b.cpu && a.size == b.size;
}

void Require(bool condition, const char* what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

template <typename A, typename B>
void CheckRanges(const A& a, const B& b) {
    Require(a.size() == b.size(), "range count");
    for (size_t i = 0; i < a.size(); ++i) {
        Require(SameRange(a[i], b[i]), "range contents/order");
    }
}

template <typename T>
typename T::Update Apply(T& table, Operation op) {
    switch (op.kind) {
    case Kind::Map: return table.Map(op.gpu, op.cpu, op.size);
    case Kind::Unmap: return table.Unmap(op.gpu, op.size);
    case Kind::Acquire: return table.Acquire(op.gpu, op.size);
    case Kind::Release: return table.Release(op.gpu);
    case Kind::Reset: table.Reset(); return {};
    }
    std::abort();
}

template <typename T>
void DumpUpdate(const T& update) {
    std::cerr << "success=" << update.success << " lease=" << update.lease << '\n';
    for (const auto& change : update.changes) {
        std::cerr << "  gpu=" << change.range.gpu << " cpu=" << change.range.cpu
                  << " size=" << change.range.size << " map=" << change.map
                  << " native=" << change.native << '\n';
    }
}

struct Audit {
    Reference reference;
    Optimized optimized;
    std::deque<Operation> history;
    u64 steps{};
    u64 accepted{};
    u64 rejected{};
    u64 changes{};
    u64 native_changes{};
    u64 unmap_changes{};
    std::array<u64, 5> kinds{};

    void Query(u64 address, u64 size) const {
        Require(reference.Lookup(address) == optimized.Lookup(address), "lookup");
        Require(reference.UsesBacking(address, size) == optimized.UsesBacking(address, size),
                "backing overlap");
        Require(reference.HasNativeOverlap(address, size) ==
                    optimized.HasNativeOverlap(address, size), "native overlap");
        Require(Reference::Valid(address, size) == Optimized::Valid(address, size), "validity");
    }

    void Image(u64 address, u64 bound, u64 offset, u64 size) const {
        const auto a = Reference::ImageRange(address, bound, offset, size);
        const auto b = Optimized::ImageRange(address, bound, offset, size);
        Require(a.has_value() == b.has_value(), "image validity");
        if (a) {
            Require(SameRange(*a, *b), "image range");
        }
    }

    void State() const {
        CheckRanges(reference.Effective(), optimized.Effective());
        CheckRanges(reference.mappings, optimized.mappings);
        Require(reference.next_lease == optimized.next_lease, "next lease ID");
        Require(reference.leases.size() == optimized.leases.size(), "lease count");
        auto other = optimized.leases.begin();
        for (const auto& [id, range] : reference.leases) {
            Require(id == other->first && SameRange(range, other->second), "lease contents");
            ++other;
        }
        for (const auto& range : reference.Effective()) {
            Query(range.gpu - 1, 1);
            Query(range.gpu, range.size);
            Query(range.gpu + range.size - 1, 1);
            Query(range.gpu + range.size, 1);
        }
    }

    u64 Step(Operation op) {
        ++steps;
        ++kinds[static_cast<size_t>(op.kind)];
        history.push_back(op);
        if (history.size() > 16) {
            history.pop_front();
        }
        const auto a = Apply(reference, op);
        const auto b = Apply(optimized, op);
        try {
            Require(a.success == b.success, "update success");
            Require(a.lease == b.lease, "returned lease ID");
            Require(a.changes.size() == b.changes.size(), "change count");
            for (size_t i = 0; i < a.changes.size(); ++i) {
                const auto& x = a.changes[i];
                const auto& y = b.changes[i];
                Require(SameRange(x.range, y.range) && x.map == y.map && x.native == y.native,
                        "change contents/order");
                ++changes;
                native_changes += x.native;
                unmap_changes += !x.map;
            }
            State();
            Query(op.gpu, op.size);
            Query(op.cpu, op.size);
            Query(op.gpu + op.size, 0);
            Query(Max, Max);
        } catch (...) {
            std::cerr << "Failed at operation " << steps << ", last operations (kind gpu cpu size):\n";
            for (const auto& item : history) {
                std::cerr << static_cast<int>(item.kind) << ' ' << item.gpu << ' ' << item.cpu
                          << ' ' << item.size << '\n';
            }
            std::cerr << "Reference update:\n";
            DumpUpdate(a);
            std::cerr << "Optimized update:\n";
            DumpUpdate(b);
            throw;
        }
        if (op.kind != Kind::Reset) {
            accepted += a.success;
            rejected += !a.success;
        }
        return a.lease;
    }
};

void Edges(Audit& audit) {
    const std::array<u64, 11> values{0, 1, 2, 0xfff, 0x1000, 0x1001,
                                    Max / 2, Max - 0x1000, Max - 2, Max - 1, Max};
    for (const u64 gpu : values) {
        for (const u64 size : values) {
            audit.Step({Kind::Reset});
            audit.Step({Kind::Map, gpu, gpu, size});
            const u64 lease = audit.Step({Kind::Acquire, gpu, 0, size});
            audit.Step({Kind::Unmap, gpu, 0, size});
            audit.Step({Kind::Release, lease});
            audit.Step({Kind::Release, lease});
            for (const u64 cpu : values) {
                audit.Step({Kind::Map, gpu, cpu, size});
                audit.Step({Kind::Unmap, gpu, 0, size});
            }
            for (const u64 offset : values) {
                for (const u64 bound : values) {
                    audit.Image(gpu, bound, offset, size);
                }
            }
        }
    }
    audit.Step({Kind::Reset});
    audit.Step({Kind::Map, 10, 100, 100});
    audit.Step({Kind::Unmap, 30, 0, 20});
    audit.Step({Kind::Map, 20, 0, 70});
    audit.Step({Kind::Acquire, 20, 0, 1});
    audit.Step({Kind::Unmap, 0, 0, 200});
    const u64 outer = audit.Step({Kind::Acquire, 10, 0, 100});
    const u64 inner = audit.Step({Kind::Acquire, 30, 0, 20});
    const u64 duplicate = audit.Step({Kind::Acquire, 30, 0, 20});
    const u64 adjacent = audit.Step({Kind::Acquire, 110, 0, 20});
    audit.Step({Kind::Map, 0, 0, 140});
    audit.Step({Kind::Map, 30, 300, 1});
    audit.Step({Kind::Unmap, 20, 0, 100});
    audit.Step({Kind::Release, inner});
    audit.Step({Kind::Release, outer});
    audit.Step({Kind::Release, duplicate});
    audit.Step({Kind::Release, adjacent});
    const u64 stale = audit.Step({Kind::Acquire, 0, 0, Max});
    audit.Step({Kind::Reset});
    audit.Step({Kind::Acquire, Max - 1, 0, 1});
    audit.Step({Kind::Release, stale});
    audit.Step({Kind::Reset});
    for (u64 i = 0; i < 1024; ++i) {
        audit.Step({Kind::Map, i * 16, i * 32, 8});
    }
    audit.Step({Kind::Map, 8194, 0, 2});
    audit.Step({Kind::Unmap, 8193, 0, 4});
    const u64 distant = audit.Step({Kind::Acquire, 1ULL << 40, 0, 16});
    audit.Step({Kind::Unmap, 0, 0, 16384});
    audit.Step({Kind::Release, distant});
    audit.Step({Kind::Reset});
}

void Random(Audit& audit, u64 seed, u64 operations) {
    std::mt19937_64 random(seed);
    for (u64 i = 0; i < operations; ++i) {
        const u64 mode = (i / 256) % 3;
        const auto address = [&]() -> u64 {
            const u64 value = random() % 256;
            return mode == 0 ? value : mode == 1 ? Max - value : value * 0x1000;
        };
        u64 gpu = address();
        u64 cpu = random() % 2 ? gpu : address();
        u64 size = 1 + random() % 64;
        if (mode == 2) {
            size *= 0x1000;
        }
        switch (random() % 24) {
        case 0: size = 0; break;
        case 1: size = Max; break;
        case 2: size = gpu == 0 ? Max : Max - gpu + 1; break;
        case 3: gpu = random(); cpu = random(); break;
        default: break;
        }
        const u64 choice = random() % 12;
        if (i % 256 == 0) {
            audit.Step({Kind::Reset});
        } else if (choice < 4 || choice == 11) {
            audit.Step({Kind::Map, gpu, cpu, size});
        } else if (choice < 6) {
            audit.Step({Kind::Unmap, gpu, 0, size});
        } else if (choice < 9) {
            audit.Step({(i / 256) % 4 == 0 ? Kind::Map : Kind::Acquire, gpu, cpu, size});
        } else {
            u64 id = random() % (audit.reference.next_lease + 4);
            if (!audit.reference.leases.empty() && random() % 4 != 0) {
                auto it = audit.reference.leases.begin();
                std::advance(it, random() % audit.reference.leases.size());
                id = it->first;
            }
            audit.Step({Kind::Release, id});
        }
        audit.Query(random(), random());
        audit.Image(gpu, size, random() % 0x2000, random() % 0x2000);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    const u64 seed = std::stoull(argv[1]);
    const u64 operations = std::stoull(argv[2]);
    Audit audit;
    try {
        Edges(audit);
        Random(audit, seed, operations);
    } catch (const std::exception& error) {
        std::cerr << "FAIL seed=" << seed << " operations=" << operations << ": "
                  << error.what() << '\n';
        return 1;
    }
    std::cout << "PASS seed=" << seed << " randomized=" << operations
              << " total=" << audit.steps << " accepted=" << audit.accepted
              << " rejected=" << audit.rejected << " changes=" << audit.changes
              << " native_changes=" << audit.native_changes
              << " unmap_changes=" << audit.unmap_changes << '\n';
    std::cout << "map=" << audit.kinds[0] << " unmap=" << audit.kinds[1]
              << " acquire=" << audit.kinds[2] << " release=" << audit.kinds[3]
              << " reset=" << audit.kinds[4] << '\n';
}
"""


def extract(repo: Path, revision: str, namespace: str) -> str:
    source = subprocess.check_output(
        ["git", "show", f"{revision}:{HEADER}"], cwd=repo, text=True
    )
    if source.count("namespace Service::Nvidia::Devices {") != 1:
        raise ValueError(f"Unexpected namespace in {revision}")
    if source.count("\nprivate:\n") != 1:
        raise ValueError(f"Unexpected access sections in {revision}")
    return source.replace("Service::Nvidia::Devices", namespace).replace(
        "\nprivate:\n", "\npublic:\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "clang++"))
    parser.add_argument("--seed", type=lambda value: int(value, 0), default=0x4D415050494E47)
    parser.add_argument("--operations", type=int, default=2_000_000)
    parser.add_argument("--sanitize", action="store_true", help="Enable address/undefined sanitizers")
    parser.add_argument("--work-dir", type=Path, help="Parent directory for temporary compiler files")
    args = parser.parse_args()
    if args.operations < 0 or not 0 <= args.seed < 2**64:
        parser.error("operations must be nonnegative and seed must fit u64")
    repo = Path(__file__).resolve().parents[2]
    if args.work_dir:
        args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="mapping-equivalence-", dir=args.work_dir) as directory:
        work = Path(directory)
        for name, revision, namespace in (
            ("reference.h", OLD_REVISION, "MappingReference"),
            ("optimized.h", NEW_REVISION, "MappingOptimized"),
        ):
            (work / name).write_text(extract(repo, revision, namespace))
        source = work / "equivalence.cpp"
        source.write_text(CXX_SOURCE)
        binary = work / "equivalence"
        command = shlex.split(args.cxx) + [
            "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(repo / "src"), str(source), "-o", str(binary),
        ]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        print(f"Reference: {OLD_REVISION}\nOptimized: {NEW_REVISION}", flush=True)
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        subprocess.run([str(binary), str(args.seed), str(args.operations)], check=True)


if __name__ == "__main__":
    main()
