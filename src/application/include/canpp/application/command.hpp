#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace canpp::application {

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};
};

enum class DiagnosticCode { invalid_syntax, invalid_argument, no_trace, no_dbc, not_found, io_error, unsupported, internal_error };

struct Diagnostic {
    DiagnosticCode code{DiagnosticCode::invalid_argument};
    std::string message;
    SourceLocation location{};
};

// A parsed command. The token boundaries are shared by all text adapters; the
// original line is retained for expression arguments whose whitespace matters.
struct CommandRequest {
    std::string line;
    std::vector<std::string> tokens;
    std::vector<std::size_t> token_end_offsets;

    [[nodiscard]] std::string remainder_after(std::size_t word_count) const;
};

struct CommandResult {
    bool success{true};
    bool exit_requested{false};
    bool state_changed{false};
    std::string output;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] std::string error_text() const;
};

} // namespace canpp::application
