// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdlib>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "shader_recompiler/frontend/ir/ir_emitter.h"
#include "shader_recompiler/frontend/ir/program.h"

namespace Shader::Optimization {

/// Ocarina of Time's outdoor fog (EDEN_NX2_OOT_FOG).
enum class OotFog {
    On,       ///< The game's fog as emulated and exposure 0.65
    Reduced,  ///< Fog slices 1/4 as thick and exposure 0.65 (the haze workaround)
    Off,      ///< No fog (zero-thickness slices) and exposure 0.65
    Original, ///< No shader edits: the game's fog and the emulator's brighter exposure
};

/// EDEN_NX2_OOT_FOG=on|reduced|off|original. Without it, EDEN_NX2_OOT_VISUAL_WORKAROUND=1 means
/// reduced. EDEN_NX2_OOT_VISUAL_WORKAROUND=0 (older builds' opt-out, which users may still have in
/// their own nx2_switches.txt) means original over a shipped EDEN_NX2_OOT_FOG=reduced default.
[[nodiscard]] inline OotFog ParseOotFog(const char* fog, const char* legacy) {
    const std::string_view legacy_value = legacy != nullptr ? legacy : "";
    std::optional<OotFog> mode;
    if (fog != nullptr) {
        const std::string_view value{fog};
        if (value == "on") {
            mode = OotFog::On;
        } else if (value == "reduced") {
            mode = OotFog::Reduced;
        } else if (value == "off") {
            mode = OotFog::Off;
        } else if (value == "original") {
            mode = OotFog::Original;
        }
    }
    if (!mode) {
        return legacy_value == "1" ? OotFog::Reduced : OotFog::Original;
    }
    if (*mode == OotFog::Reduced && legacy_value == "0") {
        return OotFog::Original;
    }
    return *mode;
}

[[nodiscard]] inline OotFog OotFogFromEnvironment() {
    return ParseOotFog(std::getenv("EDEN_NX2_OOT_FOG"),
                       std::getenv("EDEN_NX2_OOT_VISUAL_WORKAROUND"));
}

// TEMPORARY visual workaround for Ocarina of Time, not an emulator fix.
inline std::optional<size_t> ApplyOotVisualWorkaround(IR::Program& program, u64 title_id,
                                                     u64 shader_hash, OotFog mode) {
    if (mode == OotFog::Original || title_id != 0x0400486024DEC000ULL) {
        return std::nullopt;
    }
    const bool fog = mode != OotFog::On && shader_hash == 0x780c0536b89fc89eULL &&
                     program.stage == Stage::Compute;
    const bool exposure = shader_hash == 0xbded5ce917183214ULL && program.stage == Stage::Fragment;
    if (!fog && !exposure) {
        return std::nullopt;
    }
    const auto has_opcode = [](const IR::Value& value, IR::Opcode opcode) {
        return !value.IsImmediate() && value.InstRecursive()->GetOpcode() == opcode;
    };
    const auto negative_exp = [&](const IR::Value& value) {
        if (has_opcode(value, IR::Opcode::FPNeg32)) {
            return has_opcode(value.InstRecursive()->Arg(0), IR::Opcode::FPExp2);
        }
        if (!has_opcode(value, IR::Opcode::BitCastF32U32)) {
            return false;
        }
        const auto bits = value.InstRecursive()->Arg(0);
        if (!has_opcode(bits, IR::Opcode::BitwiseXor32)) {
            return false;
        }
        const auto* flip = bits.InstRecursive();
        const auto source = flip->Arg(0);
        return flip->Arg(1).IsImmediate() && flip->Arg(1).U32() == 0x80000000U &&
               has_opcode(source, IR::Opcode::BitCastU32F32) &&
               has_opcode(source.InstRecursive()->Arg(0), IR::Opcode::FPExp2);
    };
    std::vector<std::pair<IR::Block*, IR::Inst*>> matches;
    for (IR::Block* block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (fog && inst.GetOpcode() == IR::Opcode::FPAdd32 &&
                ((negative_exp(inst.Arg(0)) && has_opcode(inst.Arg(1), IR::Opcode::FPExp2)) ||
                 (negative_exp(inst.Arg(1)) && has_opcode(inst.Arg(0), IR::Opcode::FPExp2)))) {
                matches.emplace_back(block, &inst);
            }
            if (exposure && inst.GetOpcode() == IR::Opcode::GetCbufF32 &&
                inst.Arg(0).IsImmediate() && inst.Arg(0).U32() == 3 &&
                inst.Arg(1).IsImmediate() && inst.Arg(1).U32() == 0x38) {
                matches.emplace_back(block, &inst);
            }
        }
    }
    if ((fog && matches.size() != 1) || (exposure && (matches.empty() || matches.size() > 3))) {
        return 0;
    }
    for (const auto& [block, inst] : matches) {
        if (fog && mode == OotFog::Off) {
            // A zero slice distance, not distance * 0: no extinction or scattering accumulates,
            // and an infinite distance can't turn into NaN.
            inst->ReplaceUsesWith(IR::Value{0.0f});
            continue;
        }
        const auto insertion = block->Instructions().iterator_to(*inst);
        const auto original = block->PrependNewInst(insertion, *inst);
        IR::IREmitter ir{*block, insertion};
        const auto scaled = ir.FPMul(IR::F32{IR::Value{&*original}},
                                     ir.Imm32(fog ? 0.25f : 0.65f));
        inst->ReplaceUsesWith(scaled);
    }
    return matches.size();
}

} // namespace Shader::Optimization
