#include "canpp/core/session.hpp"
#include "canpp/application/command_executor.hpp"
#include "canpp/application/command_parser.hpp"
#include "presentation.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
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

[[maybe_unused]] std::vector<std::string> split(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> words;
    for (std::string word; input >> word;) {
        words.push_back(std::move(word));
    }
    return words;
}

#ifdef CANPP_GUI_EXECUTABLE_NAME
#ifndef _WIN32
std::string shell_quote(const std::filesystem::path& path) {
    std::string quoted{"'"};
    for (const char character : path.string()) {
        if (character == '\'') quoted += "'\\''";
        else quoted += character;
    }
    quoted += '\'';
    return quoted;
}
#endif

bool launch_gui(const canpp::core::Session& session,
                const std::filesystem::path& cli_path,
                std::string& error) {
    if (!session.has_trace()) { error = "No communication trace is open"; return false; }
    if (session.dbc_path().empty()) { error = "No DBC database is loaded"; return false; }
    auto executable = std::filesystem::absolute(cli_path).parent_path() / CANPP_GUI_EXECUTABLE_NAME;
#ifdef _WIN32
    if (!executable.has_extension()) executable += ".exe";

    const std::string executable_string = executable.string();
    const std::string trace_string = session.trace_path().string();
    const std::string dbc_string = session.dbc_path().string();
    const char* arguments[] = {executable_string.c_str(), trace_string.c_str(), dbc_string.c_str(), nullptr};
    if (_spawnv(_P_NOWAIT, executable_string.c_str(), arguments) == -1) {
        error = "Unable to launch GUI executable: " + executable.string(); return false;
    }
#else
    std::error_code filesystem_error;
    if (!std::filesystem::is_regular_file(executable, filesystem_error)) {
        error = "GUI executable not found: " + executable.string(); return false;
    }
    const auto command = shell_quote(executable) + " " + shell_quote(session.trace_path()) + " " + shell_quote(session.dbc_path());
    if (std::system(command.c_str()) != 0) {
        error = "Unable to launch GUI executable: " + executable.string(); return false;
    }
#endif
    return true;
}
#else
bool launch_gui(const canpp::core::Session&, const std::filesystem::path&, std::string& error) {
    error = "GUI is unavailable; rebuild with ENABLE_RENDER=ON";
    return false;
}
#endif

bool is_asc_import(const std::vector<std::string>& tokens) noexcept {
    return tokens.size() == 4U && tokens[0] == "import" && tokens[1] == "asc";
}

class LoadingSpinner {
public:
    LoadingSpinner(bool enabled, bool ansi_enabled, std::ostream& output)
        : enabled_(enabled), ansi_enabled_(ansi_enabled), output_(output) {}

    LoadingSpinner(const LoadingSpinner&) = delete;
    LoadingSpinner& operator=(const LoadingSpinner&) = delete;

    ~LoadingSpinner() { stop(); }

    void start() {
        if (!enabled_) return;
#ifdef _WIN32
        terminal_mode_ = std::make_unique<TerminalMode>();
        ansi_enabled_ = ansi_enabled_ && terminal_mode_->enabled();
#endif
        if (ansi_enabled_) {
            output_ << "\r\033[2K";
        } else {
            output_ << '\r' << std::string(32U, ' ') << '\r';
        }
        output_ << std::flush;
        running_.store(true);
        render(0U);
        try {
            worker_ = std::thread([this] {
                std::size_t frame = 1U;
                while (running_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (running_.load()) render(frame++ % frames.size());
                }
            });
        } catch (...) {
            running_.store(false);
            clear();
            return;
        }
        started_ = true;
    }

    void stop() noexcept {
        if (!enabled_ || (!started_ && !worker_.joinable())) return;
        running_.store(false);
        if (worker_.joinable()) worker_.join();
        clear();
        started_ = false;
    }

    [[nodiscard]] bool started() const noexcept { return started_; }

private:
#ifdef _WIN32
    class TerminalMode {
    public:
        TerminalMode() {
            handle_ = GetStdHandle(STD_ERROR_HANDLE);
            if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE ||
                GetConsoleMode(handle_, &previous_mode_) == 0) {
                return;
            }
            enabled_ = SetConsoleMode(handle_, previous_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
        }

        ~TerminalMode() {
            if (enabled_) (void)SetConsoleMode(handle_, previous_mode_);
        }

        [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    private:
        HANDLE handle_ = INVALID_HANDLE_VALUE;
        DWORD previous_mode_ = 0;
        bool enabled_ = false;
    };
#endif

    static constexpr std::array<std::string_view, 12> frames{
        "⠁", "⠃", "⠇", "⠧", "⠷", "⠿", "⠷", "⠯", "⠮", "⠟", "⠻", "⠽"};

    void render(std::size_t frame) {
        if (ansi_enabled_) {
            output_ << "\033[?25l" << '\r' << frames[frame];
        } else {
            constexpr std::string_view fallback_frames{"|/-\\"};
            output_ << '\r' << fallback_frames[frame % fallback_frames.size()];
        }
        output_ << " Importing ASC trace..." << std::flush;
    }

    void clear() noexcept {
        try {
            if (ansi_enabled_) {
                output_ << "\r\033[2K\033[?25h";
            } else {
                output_ << '\r' << std::string(32U, ' ') << '\r';
            }
            output_ << std::flush;
        } catch (...) {
            // Diagnostics are best-effort during cleanup.
        }
    }

    const bool enabled_;
    bool ansi_enabled_;
    std::ostream& output_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    bool started_ = false;
#ifdef _WIN32
    std::unique_ptr<TerminalMode> terminal_mode_;
#endif
};

bool execute_line(canpp::core::Session& session,
                  const std::string& line,
                  const std::filesystem::path& cli_path,
                  const canpp::cli::Presentation& presentation,
                  bool include_status = true,
                  std::ostream& output = std::cout,
                  std::ostream& diagnostics = std::cerr,
                  bool* failed = nullptr,
                  bool allow_animation = true,
                  bool* animated = nullptr) {
    if (failed != nullptr) *failed = false;
    if (animated != nullptr) *animated = false;
    canpp::application::CommandParser parser;
    const auto parsed = parser.parse_line(line);
    if (!parsed) {
        if (presentation.uses_json()) {
            canpp::application::CommandResult result;
            result.success = false;
            result.diagnostics.push_back(parsed.error());
            presentation.result(output, diagnostics, "", result);
        } else {
            presentation.parse_error(diagnostics, parsed.error());
        }
        if (failed != nullptr) *failed = true;
        return true;
    }
    if (parsed->tokens.size() == 1U && parsed->tokens.front() == "gui") {
        std::string error;
        if (!launch_gui(session, cli_path, error)) {
            const canpp::application::Diagnostic diagnostic{
                canpp::application::DiagnosticCode::io_error, error, {1U, 1U}};
            if (presentation.uses_json()) {
                canpp::application::CommandResult result;
                result.success = false;
                result.diagnostics.push_back(diagnostic);
                presentation.result(output, diagnostics, "gui", result);
            } else {
                presentation.diagnostic(diagnostics, diagnostic);
            }
            if (failed != nullptr) *failed = true;
        } else if (include_status) {
            canpp::application::CommandResult result;
            std::ostringstream status;
            session.status(status);
            result.output = status.str();
            presentation.result(output, diagnostics, "gui", result);
        }
        return true;
    }
    canpp::application::CommandExecutor executor(session);
    LoadingSpinner spinner(allow_animation && presentation.interactive() && is_asc_import(parsed->tokens),
                           presentation.ansi(), diagnostics);
    spinner.start();
    if (animated != nullptr) *animated = spinner.started();
    auto result = executor.execute(*parsed, {.include_status = include_status});
    spinner.stop();
    if (presentation.interactive() && parsed->tokens.front() == "help" && result.success) {
        result.output =
            "Resources\n"
            "  import asc <input.asc> <output.commtrace>\n"
            "  open <file.commtrace>\n"
            "  load dbc <file.dbc>\n"
            "  save <output.commtrace>\n\n"
            "Inspection\n"
            "  status | reset | history\n"
            "  dbc status | dbc messages [list] [grep <pattern>] [limit N] [offset N]\n"
            "  dbc variables [list] [grep <pattern>] [limit N] [offset N]\n"
            "  dbc variable <signal-name> [grep <pattern>] [limit N] [offset N]\n"
            "  print [list] [limit] [offset] [grep <pattern>]\n"
            "  print message[s]|id|timestamp [list] [limit] [offset] [grep <pattern>]\n"
            "  print variable[s] [<SignalName> [& <SignalName> ...]] [list] [limit] [offset] [grep <pattern>]\n"
            "  print index <index> [<last-index>] | print filter <expression>\n\n"
            "Filtering\n"
            "  filter protocol can [original] | filter direction rx|tx [original]\n"
            "  filter payload <offset> <hex-byte> [original]\n"
            "  filter can id|name|signal <value> [original]\n"
            "  filter <expression> [original]\n\n"
            "Other\n"
            "  gui | help | exit | quit\n";
    }
    if (presentation.interactive() && result.state_changed && !presentation.uses_json()) {
        const std::string marker = presentation.ansi() ? "✓ " : "+ ";
        std::string summary = marker + parsed->tokens.front() + " complete";
        if (parsed->tokens.front() == "open" && parsed->tokens.size() > 1U) {
            summary = marker + "Opened trace: " + parsed->tokens[1];
        } else if (parsed->tokens.front() == "load" && parsed->tokens.size() > 2U) {
            summary = marker + "Loaded DBC: " + parsed->tokens[2];
        } else if (parsed->tokens.front() == "import") {
            summary = marker + "Imported ASC trace";
        }
        output << presentation.style(summary + '\n', "\033[32m");
    }
    presentation.result(output, diagnostics, parsed->tokens.front(), result);
    if (failed != nullptr) *failed = !result.success;
    return !result.exit_requested;
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

void run_readline(canpp::core::Session& session, const std::filesystem::path& cli_path,
                  const canpp::cli::Presentation& presentation) {
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
    std::string prompt = presentation.prompt(session);
    rl_callback_handler_install(prompt.c_str(), readline_line_handler);

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
            bool animated = false;
            if (!execute_line(session, line, cli_path, presentation, true, std::cout, std::cerr, nullptr, true, &animated)) {
                stop = true;
            }
            if (animated) {
                rl_on_new_line_with_prompt();
                rl_forced_update_display();
            }
            if (!stop) {
                rl_callback_handler_remove();
                prompt = presentation.prompt(session);
                rl_callback_handler_install(prompt.c_str(), readline_line_handler);
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

    std::vector<std::string> commands;
    std::filesystem::path script;
    bool stdin_mode = false;
    bool batch = false;
    canpp::cli::OutputFormat format = canpp::cli::OutputFormat::plain;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "--format") {
            if (++index >= argc || argv[index] == nullptr ||
                !canpp::cli::parse_output_format(argv[index], format)) {
                std::cerr << "--format requires plain, table, or json\n";
                return 2;
            }
        } else if (argument.rfind("--format=", 0U) == 0U) {
            if (!canpp::cli::parse_output_format(argument.substr(9), format)) {
                std::cerr << "--format requires plain, table, or json\n";
                return 2;
            }
        } else if (argument == "--command") {
            if (++index >= argc || argv[index] == nullptr) {
                std::cerr << "--command requires a command line\n";
                return 2;
            }
            commands.emplace_back(argv[index]);
            batch = true;
        } else if (argument == "--script") {
            if (++index >= argc || argv[index] == nullptr) {
                std::cerr << "--script requires a file\n";
                return 2;
            }
            script = argv[index];
            batch = true;
        } else if (argument == "--stdin") {
            stdin_mode = true;
            batch = true;
        } else {
            std::cerr << "Unknown option: " << argument << '\n';
            return 2;
        }
    }

    const auto terminal = canpp::cli::detect_terminal();
    const canpp::cli::Presentation presentation(format, !batch, terminal);

    if (batch) {
        if ((!commands.empty() && (!script.empty() || stdin_mode)) ||
            (!script.empty() && stdin_mode)) {
            std::cerr << "Choose one of --command, --script, or --stdin\n";
            return 2;
        }
        std::vector<std::string> lines;
        if (!commands.empty()) {
            lines = std::move(commands);
        } else if (!script.empty()) {
            std::ifstream input(script);
            if (!input) {
                std::cerr << "Unable to open script: " << script.string() << '\n';
                return 2;
            }
            for (std::string line; std::getline(input, line);) lines.push_back(std::move(line));
        } else {
            for (std::string line; std::getline(std::cin, line);) lines.push_back(std::move(line));
        }
        for (const auto& line : lines) {
            bool failed = false;
            if (!execute_line(session, line, cli_path, presentation, false, std::cout, std::cerr, &failed, false)) return 0;
            if (failed) return 1;
        }
        return 0;
    }

    if (!presentation.uses_json()) {
        std::cout << presentation.style("Canpp communication trace CLI - type 'help'\n", "\033[1;36m");
    }
#ifdef CANPP_TRACE_CLI_HAS_READLINE
    completion_session = &session;
    rl_attempted_completion_function = complete_line;
    configure_completion();
    initialize_history();
#ifndef _WIN32
    if (presentation.terminal_interactive()) {
        run_readline(session, cli_path, presentation);
    } else {
        for (std::string line; (presentation.uses_json() || (std::cout << presentation.prompt(session))) &&
                               std::getline(std::cin, line);) {
            if (!execute_line(session, line, cli_path, presentation)) break;
        }
    }
#else
    if (presentation.terminal_interactive()) {
        for (;;) {
            std::cout.flush();
            const std::string prompt = presentation.prompt(session);
            char* raw_line = readline(prompt.c_str());
            if (raw_line == nullptr) break;
            std::string line(raw_line);
            std::free(raw_line);
            if (!line.empty()) add_history(line.c_str());
            bool animated = false;
            if (!execute_line(session, line, cli_path, presentation, true, std::cout, std::cerr, nullptr, true, &animated)) break;
            if (animated) {
                rl_on_new_line_with_prompt();
                rl_forced_update_display();
            }
        }
    } else {
        for (std::string line; (presentation.uses_json() || (std::cout << presentation.prompt(session))) &&
                               std::getline(std::cin, line);) {
            if (!execute_line(session, line, cli_path, presentation)) break;
        }
    }
#endif
    save_history();
#else
    for (std::string line; std::cout << presentation.prompt(session) && std::getline(std::cin, line);) {
        if (!execute_line(session, line, cli_path, presentation)) break;
    }
#endif
    return 0;
}
