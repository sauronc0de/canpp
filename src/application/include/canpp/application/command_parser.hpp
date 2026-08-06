#pragma once

#include "canpp/application/command.hpp"

#include <expected>
#include <string_view>

namespace canpp::application {

class CommandParser {
public:
    [[nodiscard]] std::expected<CommandRequest, Diagnostic>
    parse_line(std::string_view line, SourceLocation location = {}) const;
};

[[nodiscard]] std::expected<CommandRequest, Diagnostic>
parse_line(std::string_view line, SourceLocation location = {});

} // namespace canpp::application
