// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

namespace VideoCommon {

enum class DlssEvaluationResult { Succeeded, OptionsFailed, TokenFailed, ConstantsFailed, EvaluateFailed };

template <typename Options, typename Token, typename Begin, typename Constants, typename Evaluate,
          typename Restore>
DlssEvaluationResult RecordDlssEvaluation(Options options, Token token, Begin begin,
                                         Constants constants, Evaluate evaluate, Restore restore) {
    if (!options()) {
        return DlssEvaluationResult::OptionsFailed;
    }
    if (!token()) {
        return DlssEvaluationResult::TokenFailed;
    }
    begin();
    const bool constants_ok = constants();
    const bool evaluated = constants_ok && evaluate();
    restore();
    return !constants_ok ? DlssEvaluationResult::ConstantsFailed
           : !evaluated ? DlssEvaluationResult::EvaluateFailed
                        : DlssEvaluationResult::Succeeded;
}

} // namespace VideoCommon
