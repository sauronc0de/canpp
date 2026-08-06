#include "canpp/application/command_parser.hpp"

#include <cctype>

namespace canpp::application {

std::expected<CommandRequest, Diagnostic>
CommandParser::parse_line(std::string_view line, SourceLocation location) const {
    CommandRequest request;
    request.line = std::string(line);
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
            ++position;
        }
        if (position == line.size()) {
            break;
        }
        std::string word;
        char quote = '\0';
        while (position < line.size()) {
            const char current = line[position];
            if (quote != '\0') {
                if (current == quote) {
                    quote = '\0';
                    ++position;
                } else if (current == '\\' && position + 1U < line.size()) {
                    word += line[position + 1U];
                    position += 2U;
                } else {
                    word += current;
                    ++position;
                }
            } else if (std::isspace(static_cast<unsigned char>(current)) != 0) {
                break;
            } else if (current == '\\' && position + 1U < line.size()) {
                word += line[position + 1U];
                position += 2U;
            } else if (current == '\'' || current == '"') {
                quote = current;
                ++position;
            } else {
                word += current;
                ++position;
            }
        }
        if (quote != '\0') {
            return std::unexpected(Diagnostic{DiagnosticCode::invalid_syntax,
                                              "Unterminated quote", location});
        }
        request.tokens.push_back(std::move(word));
        request.token_end_offsets.push_back(position);
    }
    return request;
}

std::expected<CommandRequest, Diagnostic>
parse_line(std::string_view line, SourceLocation location) {
    return CommandParser{}.parse_line(line, location);
}

std::string CommandRequest::remainder_after(std::size_t word_count) const {
    if (word_count == 0U) {
        return line;
    }
    if (word_count >= token_end_offsets.size()) {
        return {};
    }
    std::size_t position = token_end_offsets[word_count - 1U];
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])) != 0) {
        ++position;
    }
    return line.substr(position);
}

std::string CommandResult::error_text() const {
    if (diagnostics.empty()) {
        return {};
    }
    return diagnostics.front().message;
}

} // namespace canpp::application
