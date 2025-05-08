#pragma once

#include <string>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"

namespace mousedb {
namespace logging {
enum class sink_type { null, file, console };

enum class level {
    trace = quill::LogLevel::TraceL1,
    debug = quill::LogLevel::Debug,
    info = quill::LogLevel::Info,
    error = quill::LogLevel::Error,
};

static auto get(std::string_view name, level lvl, sink_type t) -> void;

static auto write(level lvl, std::string_view message) -> void;

}  // namespace logging
}  // namespace mousedb
