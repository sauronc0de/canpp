#include "canpp/application/command_executor.hpp"

#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <utility>

namespace canpp::application {
namespace {

CommandResult failure(DiagnosticCode code, std::string message) {
    CommandResult result;
    result.success = false;
    result.diagnostics.push_back(Diagnostic{code, std::move(message), {}});
    return result;
}

DiagnosticCode classify_error(const std::string& message) {
    if (message == "No communication trace is open") return DiagnosticCode::no_trace;
    if (message == "No DBC database is loaded") return DiagnosticCode::no_dbc;
    if (message.rfind("Unknown DBC", 0U) == 0U) return DiagnosticCode::not_found;
    if (message.rfind("Cannot ", 0U) == 0U) return DiagnosticCode::io_error;
    return DiagnosticCode::invalid_argument;
}

void append_status(core::Session& session, CommandResult& result) {
    std::ostringstream status;
    session.status(status);
    result.output += status.str();
}

template <typename Integer>
bool number(std::string value, Integer& output, int base) {
    if (base == 16 && value.size() > 2U && value[0] == '0' &&
        (value[1] == 'x' || value[1] == 'X')) {
        value.erase(0, 2);
    }
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), output, base);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
}

struct OutputSuffix {
    bool list{false};
    bool grep{false};
    std::string pattern;
    std::size_t limit{20};
    std::size_t offset{0};
    bool named_limit{false};
    bool named_offset{false};
};

bool suffixes(const std::vector<std::string>& args, std::size_t start,
              std::vector<std::string>& base, OutputSuffix& suffix,
              std::string& error, std::size_t default_limit = 20) {
    base.clear();
    suffix = OutputSuffix{};
    suffix.limit = default_limit;
    for (std::size_t index = start; index < args.size(); ++index) {
        const auto& argument = args[index];
        if (argument == "list") {
            if (suffix.list) { error = "Invalid arguments: duplicate list"; return false; }
            suffix.list = true;
        } else if (argument == "grep") {
            if (suffix.grep || index + 1U == args.size() || args[index + 1U].empty()) {
                error = "Usage: ... grep <pattern>"; return false;
            }
            suffix.grep = true;
            suffix.pattern = args[++index];
        } else if (argument == "limit" || argument == "offset") {
            if (index + 1U == args.size()) { error = "Usage: ... " + argument + " N"; return false; }
            std::size_t value{};
            if (!number(args[index + 1U], value, 10)) {
                error = "Invalid " + argument + ": expected a non-negative integer"; return false;
            }
            if (argument == "limit") {
                if (suffix.named_limit) { error = "Invalid arguments: duplicate limit"; return false; }
                suffix.named_limit = true; suffix.limit = value;
            } else {
                if (suffix.named_offset) { error = "Invalid arguments: duplicate offset"; return false; }
                suffix.named_offset = true; suffix.offset = value;
            }
            ++index;
        } else {
            base.push_back(argument);
        }
    }
    return true;
}

void grep_lines(std::string& output, const std::string& pattern) {
    std::istringstream input(output);
    std::ostringstream filtered;
    for (std::string line; std::getline(input, line);) {
        if (line.find(pattern) != std::string::npos) filtered << line << '\n';
    }
    output = filtered.str();
}

bool original_suffix(std::vector<std::string> const& args, std::size_t index) {
    return args.size() > index && args[index] == "original";
}

bool remove_original(std::string& expression) {
    while (!expression.empty() && std::isspace(static_cast<unsigned char>(expression.back())) != 0) expression.pop_back();
    bool quoted = false;
    char quote = '\0';
    std::size_t separator = std::string::npos;
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char current = expression[index];
        if (current == '\\' && index + 1U < expression.size()) ++index;
        else if (quoted && current == quote) quoted = false;
        else if (!quoted && (current == '\'' || current == '"')) { quoted = true; quote = current; }
        else if (!quoted && std::isspace(static_cast<unsigned char>(current)) != 0) separator = index;
    }
    if (separator == std::string::npos) return false;
    const auto tail = expression.substr(separator);
    const auto first = tail.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || tail.substr(first) != "original") return false;
    expression.resize(separator);
    while (!expression.empty() && std::isspace(static_cast<unsigned char>(expression.back())) != 0) expression.pop_back();
    return true;
}

void help(std::string& output) {
    output = "import asc <input.asc> <output.commtrace>\nopen <file.commtrace>\nload dbc <file.dbc>\n"
             "dbc status | dbc messages [list] [grep <pattern>] [limit N] [offset N]\n"
             "dbc variables [list] [grep <pattern>] [limit N] [offset N]\n"
             "dbc variable <signal-name> [grep <pattern>] [limit N] [offset N]\n"
             "gui\nstatus | reset\nhistory\nfilter protocol can [original]\nfilter direction rx|tx [original]\nfilter payload <offset> <hex-byte> [original]\nfilter can id|name|signal <value> [original]\nfilter <expression> [original]\n"
             "print [list] [limit] [offset] [grep <pattern>]\n"
             "print message[s]|id|timestamp [list] [limit] [offset] [grep <pattern>]\n"
             "print variable[s] [<SignalName> [& <SignalName> ...]] [list] [limit] [offset] [grep <pattern>]\n"
             "print index <index> [<last-index>]\nprint filter <expression>\nsave <output.commtrace>\nexit\n";
}

} // namespace

CommandResult CommandExecutor::execute(const CommandRequest& request, ExecuteOptions options) {
    const auto& args = request.tokens;
    if (args.empty()) return {};
    const auto& command = args.front();
    if (command == "exit" || command == "quit") {
        if (args.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: exit");
        CommandResult result;
        result.exit_requested = true;
        return result;
    }
    if (command == "help") {
        if (args.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: help");
        CommandResult result; help(result.output); return result;
    }
    if (command == "status") {
        if (args.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: status");
        CommandResult result; append_status(session_, result); return result;
    }
    if (command == "history") {
        if (args.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: history");
        CommandResult result;
        std::ostringstream rendered;
        session_.print_history(rendered);
        result.output = rendered.str();
        return result;
    }

    if (command == "reset") {
        if (args.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: reset");
        session_.reset();
        session_.record_history(request.line);
        CommandResult result; result.state_changed = true;
        if (options.include_status) append_status(session_, result);
        return result;
    }

    std::string error;
    bool success = false;
    CommandResult result;
    if (command == "open" && args.size() == 2U) {
        success = session_.open(args[1], error); result.state_changed = success;
    } else if (command == "load" && args.size() == 3U && args[1] == "dbc") {
        success = session_.load_dbc(args[2], error); result.state_changed = success;
    } else if (command == "import" && args.size() == 4U && args[1] == "asc") {
        protocol::can::ImportStats stats;
        success = session_.import_can_asc(args[2], args[3], stats, error);
        result.state_changed = success;
        if (success) result.output = "Imported " + std::to_string(stats.imported) + ", skipped " + std::to_string(stats.skipped) + "\n";
    } else if (command == "save" && args.size() == 2U) {
        success = session_.save(args[1], error);
        if (success && options.include_status) append_status(session_, result);
    } else if (command == "filter") {
        if (args.size() < 2U) return failure(DiagnosticCode::invalid_argument, "Usage: filter <expression> [original]");
        if (args[1] == "range") {
            auto expression = request.remainder_after(2U);
            const bool original = remove_original(expression);
            if (expression.empty()) return failure(DiagnosticCode::invalid_argument, "Usage: filter range <event-expression> [original]");
            success = session_.filter_range(expression, original, error);
        } else if (args[1] == "protocol" && args.size() >= 3U && args[2] == "can") {
            success = session_.filter_protocol(trace::ProtocolId::can, original_suffix(args, 3U), error);
        } else if (args[1] == "direction" && args.size() >= 3U) {
            const auto direction = args[2] == "rx" ? trace::Direction::rx : args[2] == "tx" ? trace::Direction::tx : trace::Direction::unknown;
            if (direction == trace::Direction::unknown) return failure(DiagnosticCode::invalid_argument, "Direction must be rx or tx");
            success = session_.filter_direction(direction, original_suffix(args, 3U), error);
        } else if (args[1] == "payload" && args.size() >= 4U) {
            std::size_t offset{}; unsigned int value{};
            if (!number(args[2], offset, 10) || !number(args[3], value, 16) || value > 255U) return failure(DiagnosticCode::invalid_argument, "Usage: filter payload <offset> <hex-byte> [original]");
            success = session_.filter_payload_byte(offset, static_cast<std::uint8_t>(value), original_suffix(args, 4U), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "id") {
            std::uint32_t id{};
            if (!number(args[3], id, 16)) return failure(DiagnosticCode::invalid_argument, "Invalid CAN ID");
            success = session_.filter_can_id(id, original_suffix(args, 4U), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "name") {
            success = session_.filter_can_name(args[3], original_suffix(args, 4U), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "signal") {
            success = session_.filter_can_signal(args[3], original_suffix(args, 4U), error);
        } else {
            auto expression = request.remainder_after(1U);
            const bool original = remove_original(expression);
            if (expression.empty()) return failure(DiagnosticCode::invalid_argument, "Usage: filter <expression> [original]");
            success = session_.filter_expression(expression, original, error);
        }
        result.state_changed = success;
    } else if (command == "dbc") {
        if (args.size() < 2U) return failure(DiagnosticCode::invalid_argument, "Usage: dbc status | dbc messages | dbc variables | dbc variable <signal-name>");
        std::vector<std::string> base; OutputSuffix suffix;
        if (!suffixes(args, 2U, base, suffix, error, std::numeric_limits<std::size_t>::max())) return failure(DiagnosticCode::invalid_argument, error);
        std::ostringstream rendered;
        if (args[1] == "status") {
            if (!base.empty() || suffix.list || suffix.named_limit || suffix.named_offset) return failure(DiagnosticCode::invalid_argument, "Invalid dbc status arguments");
            success = session_.print_dbc_status(rendered, error);
        } else if (args[1] == "messages" || args[1] == "variables") {
            if (!base.empty()) return failure(DiagnosticCode::invalid_argument, "Invalid dbc catalog arguments");
            success = args[1] == "messages" ? session_.print_dbc_messages(rendered, error, suffix.list, suffix.limit, suffix.offset)
                                              : session_.print_dbc_variables(rendered, error, suffix.list, suffix.limit, suffix.offset);
        } else if (args[1] == "variable") {
            if (suffix.list && base.empty()) success = session_.print_dbc_variables(rendered, error, true, suffix.limit, suffix.offset);
            else if (suffix.list) return failure(DiagnosticCode::invalid_argument, "Invalid dbc variable arguments: list cannot be combined with a signal name");
            else if (base.size() != 1U) return failure(DiagnosticCode::invalid_argument, "Usage: dbc variable <signal-name> [grep <pattern>] [limit N] [offset N]");
            else success = session_.print_dbc_variable(rendered, base.front(), error, suffix.limit, suffix.offset);
        } else return failure(DiagnosticCode::invalid_argument, "Usage: dbc status | dbc messages | dbc variables | dbc variable <signal-name>");
        if (success) { result.output = rendered.str(); if (suffix.grep) grep_lines(result.output, suffix.pattern); }
    } else if (command == "print") {
        if (args.size() >= 2U && args[1] == "index") {
            if (args.size() != 3U && args.size() != 4U) return failure(DiagnosticCode::invalid_argument, "Usage: print index <index> [<last-index>]");
            std::size_t first{}, last{};
            if (!number(args[2], first, 10) || (args.size() == 4U && !number(args[3], last, 10))) return failure(DiagnosticCode::invalid_argument, "Invalid index: expected a non-negative integer");
            if (args.size() == 3U) last = first;
            std::ostringstream rendered;
            success = session_.print_index(rendered, first, last, error);
            if (success) result.output = rendered.str();
        } else if (args.size() >= 2U && args[1] == "filter") {
            const auto expression = request.remainder_after(2U);
            if (expression.empty()) return failure(DiagnosticCode::invalid_argument, "Usage: print filter <expression>");
            std::ostringstream rendered;
            success = session_.print_filter(rendered, expression, error);
            if (success) result.output = rendered.str();
        } else {
            std::vector<std::string> base; OutputSuffix suffix;
            if (!suffixes(args, 1U, base, suffix, error)) return failure(DiagnosticCode::invalid_argument, error == "Usage: ... grep <pattern>" ? "Usage: print ... grep <pattern>" : error);
            auto mode = core::PrintMode::full;
            bool variable = false;
            std::vector<std::string> names;
            if (!base.empty() && (base[0] == "message" || base[0] == "messages" || base[0] == "id" || base[0] == "timestamp")) {
                mode = base[0] == "id" ? core::PrintMode::id : base[0] == "timestamp" ? core::PrintMode::timestamp : core::PrintMode::message;
                base.erase(base.begin());
            } else if (!base.empty() && (base[0] == "variable" || base[0] == "variables")) {
                mode = core::PrintMode::variable; variable = true; base.erase(base.begin());
                if (!base.empty() && !suffix.list && !number(base[0], suffix.limit, 10)) {
                    bool expect = true;
                    for (const auto& name : base) {
                        if (expect) { if (name == "&") return failure(DiagnosticCode::invalid_argument, "Malformed variable list: expected a signal name after '&'"); names.push_back(name); expect = false; }
                        else { if (name != "&") return failure(DiagnosticCode::invalid_argument, "Malformed variable list: expected '&' between signal names"); expect = true; }
                    }
                    if (expect) return failure(DiagnosticCode::invalid_argument, "Malformed variable list: expected a signal name after '&'");
                } else if (!suffix.list && base.empty()) return failure(DiagnosticCode::invalid_argument, "Usage: print variable <SignalName> [& <SignalName> ...]");
            }
            if (variable && suffix.list && !names.empty()) return failure(DiagnosticCode::invalid_argument, "Invalid print arguments: list cannot be combined with signal names");
            if (!base.empty() && !variable) {
                if (base.size() > 2U || (suffix.named_limit || suffix.named_offset)) return failure(DiagnosticCode::invalid_argument, "Invalid print arguments");
                if (!number(base[0], suffix.limit, 10)) return failure(DiagnosticCode::invalid_argument, base.size() == 1U ? "Invalid print mode" : "Invalid print range");
                if (base.size() == 2U && !number(base[1], suffix.offset, 10)) return failure(DiagnosticCode::invalid_argument, "Invalid print range");
            }
            std::ostringstream rendered;
            if (!session_.has_trace() && !(variable && !suffix.list && session_.dbc_path().empty())) return failure(DiagnosticCode::no_trace, "No communication trace is open");
            if (variable) {
                if (suffix.list && names.empty()) session_.print(rendered, mode, suffix.limit, suffix.offset, true);
                else if (names.size() == 1U) success = session_.print_variable(rendered, names.front(), suffix.limit, suffix.offset, error);
                else success = session_.print_variables(rendered, names, suffix.limit, suffix.offset, error);
            } else { session_.print(rendered, mode, suffix.limit, suffix.offset, suffix.list); success = true; }
            if (success) { result.output = rendered.str(); if (suffix.grep) grep_lines(result.output, suffix.pattern); }
        }
    } else {
        return failure(DiagnosticCode::invalid_argument, "Unknown command");
    }

    if (!success) {
        const auto message = error.empty() ? std::string{"invalid command"} : error;
        return failure(classify_error(message), message);
    }
    if (result.state_changed) {
        if (command == "open" || command == "load" || command == "import" || command == "filter") session_.record_history(request.line);
        if (options.include_status) append_status(session_, result);
    }
    return result;
}

} // namespace canpp::application
