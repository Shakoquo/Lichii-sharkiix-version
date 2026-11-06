// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <stdexcept>

#include "common/assert.h"
#include "common/logging.h"

void AssertFailSoftImpl() {
    throw std::runtime_error("Shader compiler assertion failed");
}

void AssertFatalImpl() {
    throw std::runtime_error("Shader compiler reached an unreachable path");
}

namespace Common::Log {
void FmtLogMessageImpl(Class, Level level, const char* filename, unsigned int line,
                       const char* function, fmt::string_view format,
                       const fmt::format_args& args) {
    if (level < Level::Warning)
        return;
    const auto message = fmt::vformat(format, args);
    fmt::print(stderr, "{}:{} {}: {}\n", filename, line, function, message);
    if (level == Level::Critical)
        throw std::runtime_error(message);
}
} // namespace Common::Log
