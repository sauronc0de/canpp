#include "canpp/core/session.hpp"

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

template <typename Integer>
bool parse_number(std::string text, Integer& output, int base) {
    if (base == 16 && text.size() > 2U && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.erase(0, 2);
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), output, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
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
        << "dbc status\n"
        << "dbc messages\n"
        << "dbc variable <signal-name>\n"
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
        << "print [limit] [offset]\n"
        << "print message|id|timestamp [limit] [offset]\n"
        << "print variable[s] <SignalName> [& <SignalName> ...] [limit] [offset]\n"
        << "print index <index> [<last-index>]\n"
        << "print filter <expression> (current selection; non-mutating)\n"
        << "save <output.commtrace>\n"
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
        if (args.size() < 2U || args.size() > 3U ||
            (args[1] != "status" && args[1] != "messages" && args[1] != "variable") ||
            (args[1] != "variable" && args.size() != 2U) ||
            (args[1] == "variable" && args.size() != 3U)) {
            std::cerr << "Usage: dbc status | dbc messages | dbc variable <signal-name>\n";
            return true;
        }
        if (args[1] == "status") {
            success = session.print_dbc_status(std::cout, error);
        } else if (args[1] == "messages") {
            success = session.print_dbc_messages(std::cout, error);
        } else {
            success = session.print_dbc_variable(std::cout, args[2], error);
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
        std::size_t limit = 20;
        std::size_t offset = 0;
        auto mode = canpp::core::PrintMode::full;
        bool explicit_mode = false;
        std::vector<std::string> variable_names;
        std::size_t variable_range_start = 0;
        bool variable_signal = false;
        if (args.size() > 1U) {
            if (args[1] == "message") {
                mode = canpp::core::PrintMode::message;
            } else if (args[1] == "id") {
                mode = canpp::core::PrintMode::id;
            } else if (args[1] == "timestamp") {
                mode = canpp::core::PrintMode::timestamp;
            } else if (args[1] == "variable" || args[1] == "variables") {
                mode = canpp::core::PrintMode::variable;
                variable_signal = true;
                if (args.size() < 3U || parse_number(args[2], limit, 10)) {
                    std::cerr << "Usage: print variable <SignalName> [& <SignalName> ...] [limit] [offset]\n";
                    return true;
                }
                std::size_t argument = 2U;
                bool expect_signal = true;
                while (argument < args.size() && expect_signal) {
                    if (args[argument] == "&") {
                        std::cerr << "Malformed variable list: expected a signal name after '&'\n";
                        return true;
                    }
                    variable_names.push_back(args[argument++]);
                    expect_signal = false;
                    if (argument < args.size() && args[argument] == "&") {
                        ++argument;
                        expect_signal = true;
                    }
                }
                if (expect_signal) {
                    std::cerr << "Malformed variable list: expected a signal name after '&'\n";
                    return true;
                }
                variable_range_start = argument;
            } else if (!parse_number(args[1], limit, 10)) {
                std::cerr << "Invalid print mode\n";
                return true;
            }
            explicit_mode = mode != canpp::core::PrintMode::full;
        }
        const auto range_start = variable_signal ? variable_range_start : explicit_mode ? 2U : 1U;
        if (args.size() > range_start + 2U ||
            (mode != canpp::core::PrintMode::variable && args.size() > 4U)) {
            std::cerr << "Invalid print arguments\n";
            return true;
        }
        if (args.size() > range_start && !parse_number(args[range_start], limit, 10)) {
            std::cerr << "Invalid print range\n";
            return true;
        }
        if (args.size() > range_start + 1U && !parse_number(args[range_start + 1U], offset, 10)) {
            std::cerr << "Invalid print range\n";
            return true;
        }
        if (variable_signal) {
            if (variable_names.size() == 1U) {
                if (!session.print_variable(std::cout, variable_names.front(), limit, offset, error)) {
                    std::cerr << "Error: " << error << '\n';
                }
            } else if (!session.print_variables(std::cout, variable_names, limit, offset, error)) {
                std::cerr << "Error: " << error << '\n';
            }
        } else {
            session.print(std::cout, mode, limit, offset);
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
        return {"status", "messages", "variable"};
    }
    if (completed.size() == 2U && completed[0] == "dbc" && completed[1] == "variable" &&
        completion_session != nullptr) {
        return completion_session->dbc_signal_names();
    }
    if (completed.size() == 1U && completed[0] == "import") {
        return {"asc"};
    }
    if (completed.size() == 1U && completed[0] == "print") {
        return {"message", "id", "timestamp", "variable", "variables", "index", "filter"};
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
    if (completed.size() >= 2U && completed[0] == "print" &&
        (completed[1] == "variable" || completed[1] == "variables") && completion_session != nullptr) {
        if (completed.size() == 2U || completed.back() == "&") {
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
