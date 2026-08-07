#pragma once

#include "canpp/application/command.hpp"
#include "canpp/core/session.hpp"

#include <iosfwd>
#include <string>
#include <string_view>

namespace canpp::cli
{

enum class OutputFormat
{
  plain,
  table,
  json
};

struct TerminalCapabilities
{
  bool input_tty{false};
  bool output_tty{false};
  bool error_tty{false};
  bool color{false};

  [[nodiscard]] bool interactive() const noexcept { return input_tty && output_tty && error_tty; }
};

[[nodiscard]] TerminalCapabilities detect_terminal() noexcept;
[[nodiscard]] bool no_color_requested() noexcept;
[[nodiscard]] bool parse_output_format(std::string_view value, OutputFormat &format) noexcept;
[[nodiscard]] std::string output_format_name(OutputFormat format);

class Presentation
{
public:
  Presentation(OutputFormat format, bool interactive, TerminalCapabilities terminal) noexcept;

  [[nodiscard]] bool uses_json() const noexcept { return format_ == OutputFormat::json; }
  [[nodiscard]] bool uses_table() const noexcept { return format_ == OutputFormat::table; }
  [[nodiscard]] bool ansi() const noexcept { return terminal_.color && interactive_; }
  [[nodiscard]] bool interactive() const noexcept { return interactive_; }
  [[nodiscard]] bool terminal_interactive() const noexcept { return terminal_.interactive(); }

  [[nodiscard]] std::string style(std::string_view text, std::string_view code) const;
  [[nodiscard]] std::string prompt(const core::Session &session) const;
  [[nodiscard]] std::string table(std::string_view command, std::string_view text) const;

  void diagnostic(std::ostream &output, const application::Diagnostic &diagnostic) const;
  void result(std::ostream &output, std::ostream &diagnostics,
              std::string_view command, const application::CommandResult &result) const;
  void parse_error(std::ostream &diagnostics, const application::Diagnostic &diagnostic) const;

private:
  OutputFormat format_;
  bool interactive_;
  TerminalCapabilities terminal_;
};

} // namespace canpp::cli
