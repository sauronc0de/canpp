#include "canpp/core/session.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
#endif
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef CANPP_TRACE_CLI_HAS_READLINE
#include <cstring>
#include <readline/history.h>
#include <readline/readline.h>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#endif
#endif

namespace {

std::vector<std::string> split(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> words;
    for (std::string word; input >> word;) {
        words.push_back(std::move(word));
    }
    return words;
}

bool parse_quoted_words(const std::string& line, std::vector<std::string>& words) {
    words.clear();
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) ++position;
        if (position == line.size()) break;
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
            } else if (std::isspace(static_cast<unsigned char>(current))) {
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
        if (quote != '\0') return false;
        words.push_back(std::move(word));
    }
    return true;
}

void print_matching_lines(std::ostream& output, const std::string& text, const std::string& pattern) {
    std::istringstream input(text);
    for (std::string line; std::getline(input, line);) {
        if (line.find(pattern) != std::string::npos) {
            output << line << '\n';
        }
    }
}

template <typename Integer>
bool parse_number(std::string text, Integer& output, int base) {
    if (base == 16 && text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

struct OutputSuffix {
    bool list = false;
    bool grep = false;
    std::string grep_pattern;
    std::size_t limit = 20;
    std::size_t offset = 0;
    bool named_limit = false;
    bool named_offset = false;
};

bool parse_output_suffix(const std::vector<std::string>& args,
                         std::size_t start,
                         std::vector<std::string>& base,
                         OutputSuffix& suffix,
                         std::string& error,
                         std::size_t default_limit = 20) {
    base.clear();
    suffix = OutputSuffix{};
    suffix.limit = default_limit;
    for (std::size_t index = start; index < args.size(); ++index) {
        const auto& argument = args[index];
        if (argument == "list") {
            if (suffix.list) {
                error = "Invalid arguments: duplicate list";
                return false;
            }
            suffix.list = true;
        } else if (argument == "grep") {
            if (suffix.grep || index + 1U == args.size() || args[index + 1U].empty()) {
                error = "Usage: ... grep <pattern>";
                return false;
            }
            suffix.grep = true;
            suffix.grep_pattern = args[++index];
        } else if (argument == "limit" || argument == "offset") {
            if (index + 1U == args.size()) {
                error = "Usage: ... " + argument + " N";
                return false;
            }
            std::size_t value{};
            if (!parse_number(args[index + 1U], value, 10)) {
                error = "Invalid " + argument + ": expected a non-negative integer";
                return false;
            }
            if (argument == "limit") {
                if (suffix.named_limit) {
                    error = "Invalid arguments: duplicate limit";
                    return false;
                }
                suffix.named_limit = true;
                suffix.limit = value;
            } else {
                if (suffix.named_offset) {
                    error = "Invalid arguments: duplicate offset";
                    return false;
                }
                suffix.named_offset = true;
                suffix.offset = value;
            }
            ++index;
        } else {
            base.push_back(argument);
        }
    }
    return true;
}

bool from_original(const std::vector<std::string>& args, std::size_t index) {
    return args.size() > index && args[index] == "original";
}

std::string raw_after_words(const std::string& line, std::size_t words) {
    std::size_t position = 0;
    std::size_t consumed = 0;
    bool quoted = false;
    char quote = '\0';
    while (position < line.size() && consumed < words) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) ++position;
        if (position == line.size()) return {};
        ++consumed;
        while (position < line.size()) {
            const char current = line[position++];
            if (current == '\\' && position < line.size()) {
                ++position;
            } else if (quoted && current == quote) {
                quoted = false;
            } else if (!quoted && (current == '\'' || current == '"')) {
                quoted = true;
                quote = current;
            } else if (!quoted && std::isspace(static_cast<unsigned char>(current))) {
                break;
            }
        }
    }
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) ++position;
    return line.substr(position);
}

bool remove_original_suffix(std::string& expression) {
    while (!expression.empty() && std::isspace(static_cast<unsigned char>(expression.back()))) expression.pop_back();
    bool quoted = false;
    char quote = '\0';
    std::size_t suffix = std::string::npos;
    for (std::size_t index = 0; index < expression.size(); ++index) {
        const char current = expression[index];
        if (current == '\\' && index + 1U < expression.size()) {
            ++index;
        } else if (quoted && current == quote) {
            quoted = false;
        } else if (!quoted && (current == '\'' || current == '"')) {
            quoted = true;
            quote = current;
        } else if (!quoted && std::isspace(static_cast<unsigned char>(current))) {
            suffix = index;
        }
    }
    if (suffix == std::string::npos) return false;
    const auto tail = expression.substr(suffix);
    const auto first = tail.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || tail.substr(first) != "original") return false;
    expression.resize(suffix);
    while (!expression.empty() && std::isspace(static_cast<unsigned char>(expression.back()))) expression.pop_back();
    return true;
}

#ifdef CANPP_GUI_EXECUTABLE_NAME
std::filesystem::path gui_path(const std::filesystem::path& cli_path) {
    const auto directory = std::filesystem::absolute(cli_path).parent_path();
    auto executable = directory / CANPP_GUI_EXECUTABLE_NAME;
#ifdef _WIN32
    if (!executable.has_extension()) {
        executable += ".exe";
    }
#endif
    return executable;
}

#ifndef _WIN32
std::string shell_quote(const std::filesystem::path& path) {
    std::string quoted{"'"};
    for (const char character : path.string()) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
}
#endif
#endif

bool launch_gui(const canpp::core::Session& session,
                const std::filesystem::path& cli_path,
                std::string& error) {
    if (!session.has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    if (session.dbc_path().empty()) {
        error = "No DBC database is loaded";
        return false;
    }
#ifdef CANPP_GUI_EXECUTABLE_NAME
    const auto executable = gui_path(cli_path);
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(executable, filesystem_error)) {
        error = "GUI executable not found: " + executable.string();
        return false;
    }
#ifdef _WIN32
    const std::string executable_string = executable.string();
    const std::string trace_string = session.trace_path().string();
    const std::string dbc_string = session.dbc_path().string();
    const char* arguments[] = {executable_string.c_str(), trace_string.c_str(), dbc_string.c_str(), nullptr};
    if (_spawnv(_P_NOWAIT, executable_string.c_str(), arguments) == -1) {
        error = "Unable to launch GUI executable: " + executable.string();
        return false;
    }
#else
    const auto command = shell_quote(executable) + " " + shell_quote(session.trace_path()) + " " +
                         shell_quote(session.dbc_path());
    if (std::system(command.c_str()) != 0) {
        error = "Unable to launch GUI executable: " + executable.string();
        return false;
    }
#endif
    return true;
#else
    (void)cli_path;
    error = "GUI is unavailable; rebuild with ENABLE_RENDER=ON";
    return false;
#endif
}

void print_help() {
    std::cout
        << "import asc <input.asc> <output.commtrace>\n"
        << "open <file.commtrace>\n"
        << "load dbc <file.dbc>\n"
        << "dbc status [grep <pattern>]\n"
        << "dbc messages [list] [grep <pattern>] [limit N] [offset N]\n"
        << "dbc variables [list] [grep <pattern>] [limit N] [offset N]\n"
        << "dbc variable <signal-name> [grep <pattern>] [limit N] [offset N]\n"
        << "gui\n"
        << "status | reset\n"
        << "history\n"
        << "filter protocol can [original]\n"
        << "filter direction rx|tx [original]\n"
        << "filter payload <offset> <hex-byte> [original]\n"
        << "filter can id <hex-id> [original]\n"
        << "filter can name <message-name> [original]\n"
        << "filter can signal <signal-name> [original]\n"
        << "filter <expression> [original]\n"
        << "filter range <event-expression> [original]\n"
        << "  refs: signal.<Name>, message.name/id/extended, record.stream_id/protocol/direction, timestamp_ns, time\n"
        << "  ops: == != < <= > >= && || ! (precedence: !, comparison, &&, ||)\n"
        << "print [list] [limit] [offset] [grep <pattern>]\n"
        << "print message[s]|id|timestamp [list] [limit] [offset] [grep <pattern>]\n"
        << "print variable[s] [<SignalName> [& <SignalName> ...]] [list] [limit] [offset] [grep <pattern>]\n"
        << "  named suffixes (list, grep, limit, offset) may appear in any order; invalid combinations are errors\n"
        << "print index <index> [<last-index>]\n"
        << "print filter <expression> (current selection; non-mutating)\n"
        << "save <output.commtrace>\n"
        << "Ctrl+C: cancel the current line; press twice consecutively to exit\n"
        << "exit\n";
}

bool execute_line(canpp::core::Session& session,
                  const std::string& line,
                  const std::filesystem::path& cli_path) {
    const auto args = split(line);
    if (args.empty()) {
        return true;
    }
    std::string error;
    bool success = false;
    if (args[0] == "exit" || args[0] == "quit") {
        return false;
    }
    if (args[0] == "help") {
        print_help();
        return true;
    }
    if (args[0] == "status") {
        session.status(std::cout);
        return true;
    }
    if (args[0] == "dbc") {
        std::vector<std::string> parsed_args;
        if (!parse_quoted_words(line, parsed_args) || parsed_args.size() < 2U) {
            std::cerr << "Usage: dbc status | dbc messages [list] | dbc variables [list] | dbc variable <signal-name>\n";
            return true;
        }
        std::vector<std::string> command_args;
        OutputSuffix suffix;
        if (!parse_output_suffix(parsed_args, 2U, command_args, suffix, error,
                                 static_cast<std::size_t>(-1))) {
            std::cerr << error << '\n';
            return true;
        }
        const auto command = parsed_args[1];
        std::ostringstream rendered;
        std::ostream& output = suffix.grep ? static_cast<std::ostream&>(rendered) : std::cout;
        if (command == "status") {
            if (!command_args.empty() || suffix.list || suffix.named_limit || suffix.named_offset) {
                std::cerr << "Invalid dbc status arguments\n";
                return true;
            }
            success = session.print_dbc_status(output, error);
        } else if (command == "messages" || command == "variables") {
            if (!command_args.empty()) {
                std::cerr << "Invalid dbc catalog arguments\n";
                return true;
            }
            if (command == "messages") {
                success = session.print_dbc_messages(output, error, suffix.list, suffix.limit, suffix.offset);
            } else {
                success = session.print_dbc_variables(output, error, suffix.list, suffix.limit, suffix.offset);
            }
        } else if (command == "variable") {
            if (suffix.list && command_args.empty()) {
                success = session.print_dbc_variables(output, error, true, suffix.limit, suffix.offset);
            } else {
                if (suffix.list) {
                    std::cerr << "Invalid dbc variable arguments: list cannot be combined with a signal name\n";
                    return true;
                }
                if (command_args.size() != 1U) {
                    std::cerr << "Usage: dbc variable <signal-name> [grep <pattern>] [limit N] [offset N]\n";
                    return true;
                }
                success = session.print_dbc_variable(output, command_args.front(), error,
                                                     suffix.limit, suffix.offset);
            }
        } else {
            std::cerr << "Usage: dbc status | dbc messages [list] | dbc variables [list] | dbc variable <signal-name>\n";
            return true;
        }
        if (success && suffix.grep) {
            print_matching_lines(std::cout, rendered.str(), suffix.grep_pattern);
        }
        if (!success) {
            std::cerr << "Error: " << error << '\n';
        }
        return true;
    }
    if (args[0] == "history") {
        session.print_history(std::cout);
        return true;
    }
    if (args[0] == "reset") {
        session.reset();
        session.record_history(line);
        session.status(std::cout);
        return true;
    }
    if (args[0] == "gui" && args.size() == 1U) {
        success = launch_gui(session, cli_path, error);
    } else if (args[0] == "open" && args.size() == 2U) {
        success = session.open(args[1], error);
    } else if (args[0] == "load" && args.size() == 3U && args[1] == "dbc") {
        success = session.load_dbc(args[2], error);
    } else if (args[0] == "import" && args.size() == 4U && args[1] == "asc") {
        canpp::protocol::can::ImportStats stats;
        success = session.import_can_asc(args[2], args[3], stats, error);
        if (success) {
            std::cout << "Imported " << stats.imported << ", skipped " << stats.skipped << '\n';
        }
    } else if (args[0] == "save" && args.size() == 2U) {
        success = session.save(args[1], error);
    } else if (args[0] == "print") {
        if (args.size() >= 2U && args[1] == "index") {
            if (args.size() != 3U && args.size() != 4U) {
                std::cerr << "Usage: print index <index> [<last-index>]\n";
                return true;
            }
            std::size_t first{};
            std::size_t last{};
            if (!parse_number(args[2], first, 10) ||
                (args.size() == 4U && !parse_number(args[3], last, 10))) {
                std::cerr << "Invalid index: expected a non-negative integer\n";
                return true;
            }
            if (args.size() == 3U) {
                last = first;
            }
            if (!session.print_index(std::cout, first, last, error)) {
                std::cerr << "Error: " << error << '\n';
            }
            return true;
        }
        if (args.size() >= 2U && args[1] == "filter") {
            const auto expression = raw_after_words(line, 2U);
            if (expression.empty()) {
                std::cerr << "Usage: print filter <expression>\n";
                return true;
            }
            if (!session.print_filter(std::cout, expression, error)) {
                std::cerr << "Error: " << error << '\n';
            }
            return true;
        }
        std::vector<std::string> parsed_args;
        if (!parse_quoted_words(line, parsed_args)) {
            std::cerr << "Usage: print ... grep <pattern>\n";
            return true;
        }
        std::vector<std::string> command_args;
        OutputSuffix suffix;
        if (!parse_output_suffix(parsed_args, 1U, command_args, suffix, error)) {
            std::cerr << (error == "Usage: ... grep <pattern>" ? "Usage: print ... grep <pattern>" : error) << '\n';
            return true;
        }
        command_args.insert(command_args.begin(), "print");
        std::size_t limit = suffix.limit;
        std::size_t offset = suffix.offset;
        auto mode = canpp::core::PrintMode::full;
        bool explicit_mode = false;
        std::vector<std::string> variable_names;
        std::size_t variable_range_start = 0;
        bool variable_signal = false;
        if (command_args.size() > 1U) {
            if (command_args[1] == "message" || command_args[1] == "messages") {
                mode = canpp::core::PrintMode::message;
            } else if (command_args[1] == "id") {
                mode = canpp::core::PrintMode::id;
            } else if (command_args[1] == "timestamp") {
                mode = canpp::core::PrintMode::timestamp;
            } else if (command_args[1] == "variable" || command_args[1] == "variables") {
                mode = canpp::core::PrintMode::variable;
                variable_signal = true;
                std::size_t argument = 2U;
                if (argument < command_args.size() && !parse_number(command_args[argument], limit, 10)) {
                    bool expect_signal = true;
                    while (argument < command_args.size() && expect_signal) {
                        if (command_args[argument] == "&") {
                            std::cerr << "Malformed variable list: expected a signal name after '&'\n";
                            return true;
                        }
                        variable_names.push_back(command_args[argument++]);
                        expect_signal = false;
                        if (argument < command_args.size() && command_args[argument] == "&") {
                            ++argument;
                            expect_signal = true;
                        }
                    }
                    if (expect_signal) {
                        std::cerr << "Malformed variable list: expected a signal name after '&'\n";
                        return true;
                    }
                } else if (!suffix.list) {
                    std::cerr << "Usage: print variable <SignalName> [& <SignalName> ...]\n";
                    return true;
                }
                variable_range_start = argument;
            } else if (!parse_number(command_args[1], limit, 10)) {
                std::cerr << "Invalid print mode\n";
                return true;
            }
            explicit_mode = mode != canpp::core::PrintMode::full;
        }
        const auto range_start = variable_signal ? variable_range_start : explicit_mode ? 2U : 1U;
        if (variable_signal && suffix.list && !variable_names.empty()) {
            std::cerr << "Invalid print arguments: list cannot be combined with signal names\n";
            return true;
        }
        if (command_args.size() > range_start + 2U ||
            (mode != canpp::core::PrintMode::variable && command_args.size() > 4U)) {
            std::cerr << "Invalid print arguments\n";
            return true;
        }
        if ((suffix.named_limit || suffix.named_offset) && command_args.size() > range_start) {
            std::cerr << "Invalid print arguments: use either positional or named limit/offset\n";
            return true;
        }
        if (command_args.size() > range_start && !parse_number(command_args[range_start], limit, 10)) {
            std::cerr << "Invalid print range\n";
            return true;
        }
        if (command_args.size() > range_start + 1U && !parse_number(command_args[range_start + 1U], offset, 10)) {
            std::cerr << "Invalid print range\n";
            return true;
        }
        const bool preserve_no_dbc_variable_error =
            variable_signal && !suffix.list && session.dbc_path().empty();
        if (!session.has_trace() && !preserve_no_dbc_variable_error) {
            std::cerr << "Error: No communication trace is open\n";
            return true;
        }
        std::ostringstream rendered;
        bool print_success = true;
        if (variable_signal) {
            if (suffix.list && variable_names.empty()) {
                session.print(rendered, mode, limit, offset, true);
            } else if (variable_names.size() == 1U) {
                print_success = session.print_variable(rendered, variable_names.front(), limit, offset, error);
            } else {
                print_success = session.print_variables(rendered, variable_names, limit, offset, error);
            }
        } else {
            session.print(rendered, mode, limit, offset, suffix.list);
        }
        if (!print_success) {
            std::cerr << "Error: " << error << '\n';
        } else if (suffix.grep) {
            print_matching_lines(std::cout, rendered.str(), suffix.grep_pattern);
        } else {
            std::cout << rendered.str();
        }
        return true;
    } else if (args[0] == "filter" && args.size() >= 2U) {
        if (args[1] == "range") {
            auto expression = raw_after_words(line, 2U);
            const bool original = remove_original_suffix(expression);
            if (expression.empty()) {
                std::cerr << "Usage: filter range <event-expression> [original]\n";
                return true;
            }
            success = session.filter_range(expression, original, error);
        } else if (args[1] == "protocol" && args.size() >= 3U && args[2] == "can") {
            success = session.filter_protocol(canpp::trace::ProtocolId::can,
                                              from_original(args, 3), error);
        } else if (args[1] == "direction" && args.size() >= 3U) {
            const auto direction = args[2] == "rx" ? canpp::trace::Direction::rx
                                 : args[2] == "tx" ? canpp::trace::Direction::tx
                                                   : canpp::trace::Direction::unknown;
            if (direction == canpp::trace::Direction::unknown) {
                std::cerr << "Direction must be rx or tx\n";
                return true;
            }
            success = session.filter_direction(direction, from_original(args, 3), error);
        } else if (args[1] == "payload" && args.size() >= 4U) {
            std::size_t offset{};
            unsigned int value{};
            if (!parse_number(args[2], offset, 10) || !parse_number(args[3], value, 16) || value > 255U) {
                std::cerr << "Usage: filter payload <offset> <hex-byte> [original]\n";
                return true;
            }
            success = session.filter_payload_byte(offset, static_cast<std::uint8_t>(value),
                                                  from_original(args, 4), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "id") {
            std::uint32_t id{};
            if (!parse_number(args[3], id, 16)) {
                std::cerr << "Invalid CAN ID\n";
                return true;
            }
            success = session.filter_can_id(id, from_original(args, 4), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "name") {
            success = session.filter_can_name(args[3], from_original(args, 4), error);
        } else if (args[1] == "can" && args.size() >= 4U && args[2] == "signal") {
            success = session.filter_can_signal(args[3], from_original(args, 4), error);
        } else {
            auto expression = raw_after_words(line, 1U);
            const bool original = remove_original_suffix(expression);
            if (expression.empty()) {
                std::cerr << "Usage: filter <expression> [original]\n";
                return true;
            }
            success = session.filter_expression(expression, original, error);
        }
    } else {
        std::cerr << "Unknown command\n";
        return true;
    }

    if (!success) {
        std::cerr << "Error: " << (error.empty() ? "invalid command" : error) << '\n';
    } else {
        if (args[0] == "filter" || args[0] == "open" || args[0] == "load" || args[0] == "import") {
            session.record_history(line);
        }
        session.status(std::cout);
    }
    return true;
}

#ifdef CANPP_TRACE_CLI_HAS_READLINE

const std::vector<std::string> top_level_commands{
    "import", "open", "load", "dbc", "gui", "status", "reset", "history", "filter", "print", "save", "exit", "quit", "help"};

canpp::core::Session* completion_session = nullptr;
std::vector<std::string> completion_values;
std::size_t completion_index = 0;

std::vector<std::string> signal_completion_candidates() {
    std::vector<std::string> candidates;
    if (completion_session == nullptr) {
        return candidates;
    }
    for (const auto& name : completion_session->dbc_signal_names()) {
        candidates.push_back("signal." + name);
    }
    return candidates;
}

std::vector<std::string> completion_candidates(const std::string& line,
                                               const std::string& text,
                                               int start) {
    const auto prefix = line.substr(0, static_cast<std::size_t>(start));
    auto completed = split(prefix);
    const bool current_word_is_partial =
        start == 0 || (start > 0 && !std::isspace(static_cast<unsigned char>(line[static_cast<std::size_t>(start - 1)])));
    if (current_word_is_partial && !completed.empty()) {
        completed.pop_back();
    }
    if (completed.empty()) {
        return top_level_commands;
    }
    if (completed.size() == 1U && completed[0] == "load") {
        return {"dbc"};
    }
    if (completed.size() == 1U && completed[0] == "dbc") {
        return {"status", "messages", "variables", "variable"};
    }
    if (completed.size() == 2U && completed[0] == "dbc" &&
        (completed[1] == "messages" || completed[1] == "variables" || completed[1] == "variable")) {
        if (completed[1] == "variable" && completion_session != nullptr) {
            auto candidates = completion_session->dbc_signal_names();
            candidates.insert(candidates.end(), {"list", "grep", "limit", "offset"});
            return candidates;
        }
        return {"list", "grep", "limit", "offset"};
    }
    if (completed.size() == 2U && completed[0] == "dbc" && completed[1] == "variable" &&
        completion_session != nullptr) {
        return completion_session->dbc_signal_names();
    }
    if (completed.size() == 1U && completed[0] == "import") {
        return {"asc"};
    }
    if (completed.size() == 1U && completed[0] == "print") {
        return {"message", "messages", "id", "timestamp", "variable", "variables", "index", "filter", "list", "grep", "limit", "offset"};
    }
    if (completed.size() == 1U && completed[0] == "filter") {
        // Readline's `start` points at the current token, so use `text`
        // directly instead of relying on the completed prefix. This keeps
        // direct expression completion working for `filter signal.<prefix>`.
        if (text.rfind("signal.", 0U) == 0U) {
            return signal_completion_candidates();
        }
        return {"protocol", "direction", "payload", "can", "range", "signal.", "message.", "record.", "timestamp_ns", "time"};
    }
    if (completed.size() == 2U && completed[0] == "print" && completed[1] == "filter") {
        return {"signal.", "message.", "record.", "timestamp_ns", "time"};
    }
    if (completed.size() == 2U && completed[0] == "print" &&
        (completed[1] == "message" || completed[1] == "messages" || completed[1] == "id" ||
         completed[1] == "timestamp")) {
        return {"list", "grep", "limit", "offset"};
    }
    if (completed.size() >= 2U && completed[0] == "print" &&
        (completed[1] == "variable" || completed[1] == "variables") && completion_session != nullptr) {
        if (completed.size() == 2U) {
            auto candidates = completion_session->dbc_signal_names();
            candidates.insert(candidates.end(), {"list", "grep", "limit", "offset"});
            return candidates;
        }
        if (completed.back() == "&") {
            return completion_session->dbc_signal_names();
        }
        if (completed.size() >= 3U && !current_word_is_partial && completed.back() != "&") {
            return {"&"};
        }
    }
    if (completed.size() == 2U && completed[0] == "filter") {
        if (completed[1] == "range") {
            return {"signal.", "message.", "record.", "timestamp_ns", "time", "original"};
        }
        if (completed[1].rfind("signal.", 0U) == 0U) {
            return signal_completion_candidates();
        }
        if (completed[1] == "protocol") {
            return {"can"};
        }
        if (completed[1] == "direction") {
            return {"rx", "tx"};
        }
        if (completed[1] == "can") {
            return {"id", "name", "signal"};
        }
    }
    if (completed.size() == 3U && completed[0] == "filter" && completed[1] == "can") {
        if (completed[2] == "name" && completion_session != nullptr) {
            return completion_session->dbc_message_names();
        }
        if (completed[2] == "signal" && completion_session != nullptr) {
            return completion_session->dbc_signal_names();
        }
    }
    return {};
}

char* completion_generator(const char* text, int state) {
    if (state == 0) {
        completion_index = 0;
    }
    const std::string_view prefix(text);
    while (completion_index < completion_values.size()) {
        const auto& candidate = completion_values[completion_index++];
        if (candidate.size() < prefix.size() || candidate.compare(0, prefix.size(), prefix) != 0) {
            continue;
        }
        auto* result = static_cast<char*>(std::malloc(candidate.size() + 1U));
        if (result == nullptr) {
            return nullptr;
        }
        std::memcpy(result, candidate.c_str(), candidate.size() + 1U);
        return result;
    }
    return nullptr;
}

char** complete_line(const char* text, int start, int /*end*/) {
    completion_values = completion_candidates(rl_line_buffer, text, start);
    rl_attempted_completion_over = 1;
    if (completion_values.empty()) {
        return nullptr;
    }
    return rl_completion_matches(text, completion_generator);
}

std::filesystem::path history_path() {
    if (const auto* state_home = std::getenv("XDG_STATE_HOME"); state_home != nullptr && *state_home != '\0') {
        return std::filesystem::path(state_home) / "canpp_history";
    }
    if (const auto* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".canpp_history";
    }
    return {};
}

void initialize_history() {
    const auto path = history_path();
    if (path.empty()) {
        return;
    }
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
    }
    (void)read_history(path.c_str());
}

void save_history() {
    const auto path = history_path();
    if (!path.empty()) {
        (void)write_history(path.c_str());
    }
}

void configure_completion() {
    // Keep the dot in direct signal references as part of the completion word;
    // otherwise readline passes only the suffix after `signal.` to us.
    static constexpr char default_word_break_characters[] = " \t\n\\\"'`@$><=;|&{";
    static std::string word_break_characters = rl_completer_word_break_characters != nullptr
                                                    ? rl_completer_word_break_characters
                                                    : default_word_break_characters;
    for (auto position = word_break_characters.find('.');
         position != std::string::npos;
         position = word_break_characters.find('.')) {
        word_break_characters.erase(position, 1U);
    }
    rl_completer_word_break_characters = word_break_characters.c_str();
}

#ifndef _WIN32

std::string readline_callback_line;
bool readline_callback_line_ready = false;
bool readline_callback_eof = false;
volatile sig_atomic_t sigint_pipe_write_fd = -1;

void readline_line_handler(char* raw_line) {
    if (raw_line == nullptr) {
        readline_callback_eof = true;
        return;
    }
    readline_callback_line.assign(raw_line);
    std::free(raw_line);
    readline_callback_line_ready = true;
}

void sigint_handler(int /*signal*/) {
    const char marker = 1;
    if (sigint_pipe_write_fd != -1) {
        const auto result = write(sigint_pipe_write_fd, &marker, sizeof(marker));
        (void)result;
    }
}

bool install_sigint_handler(int& read_fd, int& write_fd, struct sigaction& previous_action) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        return false;
    }
    const int flags = fcntl(pipe_fds[1], F_GETFL, 0);
    if (flags == -1 || fcntl(pipe_fds[1], F_SETFL, flags | O_NONBLOCK) == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return false;
    }
    // Publish the descriptor before installing the handler so an interrupt
    // arriving during setup can never observe an invalid write descriptor.
    read_fd = pipe_fds[0];
    write_fd = pipe_fds[1];
    sigint_pipe_write_fd = write_fd;
    struct sigaction action{};
    action.sa_handler = sigint_handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, &previous_action) != 0) {
        sigint_pipe_write_fd = -1;
        close(read_fd);
        close(write_fd);
        read_fd = -1;
        write_fd = -1;
        return false;
    }
    return true;
}

void restore_sigint_handler(int read_fd, int write_fd, const struct sigaction& previous_action) {
    sigint_pipe_write_fd = -1;
    (void)sigaction(SIGINT, &previous_action, nullptr);
    close(read_fd);
    close(write_fd);
}

void cancel_readline_input() {
    // A bell is readline's supported abort command and also leaves
    // incremental reverse search mode before replacing the editable line.
    rl_pending_input = '\a';
    rl_callback_read_char();
    rl_replace_line("", 0);
    rl_clear_message();
    rl_on_new_line_with_prompt();
    rl_redisplay();
}

void run_readline(canpp::core::Session& session, const std::filesystem::path& cli_path) {
    int signal_read_fd = -1;
    int signal_write_fd = -1;
    struct sigaction previous_action{};
    if (!install_sigint_handler(signal_read_fd, signal_write_fd, previous_action)) {
        std::cerr << "Unable to install Ctrl+C handler\n";
        return;
    }

    rl_catch_signals = 0;
    readline_callback_line.clear();
    readline_callback_line_ready = false;
    readline_callback_eof = false;
    rl_callback_handler_install("canpp> ", readline_line_handler);

    bool stop = false;
    bool interrupted = false;
    while (!stop && !readline_callback_eof) {
        struct pollfd descriptors[2]{{STDIN_FILENO, POLLIN, 0}, {signal_read_fd, POLLIN, 0}};
        int poll_result;
        do {
            poll_result = poll(descriptors, 2, -1);
        } while (poll_result < 0 && errno == EINTR);
        if (poll_result < 0) {
            std::cerr << "Unable to read console input\n";
            break;
        }

        if ((descriptors[1].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            char markers[32];
            const auto marker_count = read(signal_read_fd, markers, sizeof(markers));
            if (marker_count > 0) {
                for (ssize_t marker = 0; marker < marker_count && !stop; ++marker) {
                    if (interrupted) {
                        stop = true;
                    } else {
                        interrupted = true;
                        cancel_readline_input();
                    }
                }
            }
        }
        if (!stop && (descriptors[0].revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
            // Any ordinary input between interrupts makes the next Ctrl+C a
            // fresh cancellation rather than an exit request.
            interrupted = false;
            rl_callback_read_char();
        }
        if (readline_callback_line_ready) {
            const std::string line = std::move(readline_callback_line);
            readline_callback_line_ready = false;
            if (!line.empty()) {
                add_history(line.c_str());
            }
            if (!execute_line(session, line, cli_path)) {
                stop = true;
            }
        }
    }

    rl_callback_handler_remove();
    restore_sigint_handler(signal_read_fd, signal_write_fd, previous_action);
}

#endif

#endif

} // namespace

int main(int argc, char** argv) {
    canpp::core::Session session;
    const std::filesystem::path cli_path = argc > 0 && argv[0] != nullptr ? argv[0] : "Canpp";
    std::cout << "Canpp communication trace CLI - type 'help'\n";
#ifdef CANPP_TRACE_CLI_HAS_READLINE
    completion_session = &session;
    rl_attempted_completion_function = complete_line;
    configure_completion();
    initialize_history();
#ifndef _WIN32
    run_readline(session, cli_path);
#else
    // Readline is normally unavailable on Windows; retain a simple fallback
    // for ports that provide a compatible implementation.
    for (;;) {
        std::cout.flush();
        char* raw_line = readline("canpp> ");
        if (raw_line == nullptr) {
            break;
        }
        std::string line(raw_line);
        std::free(raw_line);
        if (!line.empty()) {
            add_history(line.c_str());
        }
        if (!execute_line(session, line, cli_path)) {
            break;
        }
    }
#endif
    save_history();
#else
    for (std::string line; std::cout << "canpp> " && std::getline(std::cin, line);) {
        if (!execute_line(session, line, cli_path)) {
            break;
        }
    }
#endif
    return 0;
}
