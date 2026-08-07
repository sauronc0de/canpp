#include "presentation.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace canpp::cli
{
namespace
{

bool tty(int descriptor) noexcept
{
#ifdef _WIN32
  return _isatty(descriptor) != 0;
#else
  return isatty(descriptor) != 0;
#endif
}

std::string json_escape(std::string_view value)
{
  std::ostringstream escaped;
  escaped << '"';
  for(const unsigned char character : value)
  {
    switch(character)
    {
    case '"':
      escaped << "\\\"";
      break;
    case '\\':
      escaped << "\\\\";
      break;
    case '\b':
      escaped << "\\b";
      break;
    case '\f':
      escaped << "\\f";
      break;
    case '\n':
      escaped << "\\n";
      break;
    case '\r':
      escaped << "\\r";
      break;
    case '\t':
      escaped << "\\t";
      break;
    default:
      if(character < 0x20U)
      {
        escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                << static_cast<unsigned>(character) << std::dec << std::setfill(' ');
      }
      else
      {
        escaped << static_cast<char>(character);
      }
      break;
    }
  }
  escaped << '"';
  return escaped.str();
}

std::string diagnostic_code(application::DiagnosticCode code)
{
  switch(code)
  {
  case application::DiagnosticCode::invalid_syntax:
    return "invalid_syntax";
  case application::DiagnosticCode::invalid_argument:
    return "invalid_argument";
  case application::DiagnosticCode::no_trace:
    return "no_trace";
  case application::DiagnosticCode::no_dbc:
    return "no_dbc";
  case application::DiagnosticCode::not_found:
    return "not_found";
  case application::DiagnosticCode::io_error:
    return "io_error";
  case application::DiagnosticCode::unsupported:
    return "unsupported";
  case application::DiagnosticCode::internal_error:
    return "internal_error";
  }
  return "internal_error";
}

std::string basename(const std::filesystem::path &path)
{
  if(path.empty()) return "-";
  const auto name = path.filename().string();
  return name.empty() ? path.string() : name;
}

} // namespace

TerminalCapabilities detect_terminal() noexcept
{
  TerminalCapabilities capabilities;
  capabilities.input_tty = tty(
#ifdef _WIN32
      _fileno(stdin)
#else
      STDIN_FILENO
#endif
  );
  capabilities.output_tty = tty(
#ifdef _WIN32
      _fileno(stdout)
#else
      STDOUT_FILENO
#endif
  );
  capabilities.error_tty = tty(
#ifdef _WIN32
      _fileno(stderr)
#else
      STDERR_FILENO
#endif
  );
  capabilities.color = capabilities.output_tty && capabilities.error_tty && !no_color_requested();
  return capabilities;
}

bool no_color_requested() noexcept { return std::getenv("NO_COLOR") != nullptr; }

bool parse_output_format(std::string_view value, OutputFormat &format) noexcept
{
  if(value == "plain")
    format = OutputFormat::plain;
  else if(value == "table")
    format = OutputFormat::table;
  else if(value == "json")
    format = OutputFormat::json;
  else
    return false;
  return true;
}

std::string output_format_name(OutputFormat format)
{
  switch(format)
  {
  case OutputFormat::plain:
    return "plain";
  case OutputFormat::table:
    return "table";
  case OutputFormat::json:
    return "json";
  }
  return "plain";
}

Presentation::Presentation(OutputFormat format, bool interactive, TerminalCapabilities terminal) noexcept
    : format_(format), interactive_(interactive), terminal_(terminal) {}

std::string Presentation::style(std::string_view text, std::string_view code) const
{
  if(!ansi()) return std::string{text};
  return std::string{code} + std::string{text} + "\033[0m";
}

std::string Presentation::prompt(const core::Session &session) const
{
  if(uses_json()) return {};
  std::ostringstream prompt_text;
  prompt_text << "canpp";
  if(interactive_)
  {
    prompt_text << " [" << (session.has_trace() ? basename(session.trace_path()) : "no-trace")
                << " sel:" << session.selection_size()
                << " dbc:" << (session.dbc_path().empty() ? "-" : basename(session.dbc_path())) << ']';
  }
  prompt_text << "> ";
  return style(prompt_text.str(), "\033[1;36m");
}

std::string Presentation::table(std::string_view /*command*/, std::string_view text) const
{
  if(!uses_table() || text.empty()) return std::string{text};
  std::vector<std::string> rows;
  std::istringstream input{std::string{text}};
  std::size_t width = 0;
  for(std::string row; std::getline(input, row);)
  {
    if(!row.empty() && row.back() == '\r') row.pop_back();
    rows.push_back(row);
    width = std::max(width, row.size());
  }
  if(rows.empty()) return {};
  std::ostringstream output;
  output << "+-" << std::string(width, '-') << "-+\n";
  for(const auto &row : rows)
  {
    output << "| " << row << std::string(width - row.size(), ' ') << " |\n";
  }
  output << "+-" << std::string(width, '-') << "-+\n";
  return output.str();
}

void Presentation::diagnostic(std::ostream &output, const application::Diagnostic &value) const
{
  output << "Error";
  if(terminal_.interactive() && value.location.line != 0U)
  {
    output << " (" << value.location.line << ':' << value.location.column << ')';
  }
  output << ": " << value.message << '\n';
}

void Presentation::parse_error(std::ostream &diagnostics, const application::Diagnostic &value) const
{
  if(uses_json())
  {
    diagnostics << "{\"success\":false,\"exit_requested\":false,\"output\":\"\",\"diagnostics\":[{";
    diagnostics << "\"code\":" << json_escape(diagnostic_code(value.code))
                << ",\"message\":" << json_escape(value.message)
                << ",\"line\":" << value.location.line << ",\"column\":" << value.location.column
                << "}]}\n";
    return;
  }
  diagnostic(diagnostics, value);
}

void Presentation::result(std::ostream &output, std::ostream &diagnostics,
                          std::string_view command, const application::CommandResult &value) const
{
  if(uses_json())
  {
    output << "{\"success\":" << (value.success ? "true" : "false")
           << ",\"exit_requested\":" << (value.exit_requested ? "true" : "false")
           << ",\"output\":" << json_escape(value.output) << ",\"diagnostics\":[";
    for(std::size_t index = 0; index < value.diagnostics.size(); ++index)
    {
      if(index != 0U) output << ',';
      const auto &item = value.diagnostics[index];
      output << "{\"code\":" << json_escape(diagnostic_code(item.code))
             << ",\"message\":" << json_escape(item.message)
             << ",\"line\":" << item.location.line << ",\"column\":" << item.location.column << '}';
    }
    output << "]}\n";
    return;
  }
  if(!value.output.empty()) output << table(command, value.output);
  for(const auto &item : value.diagnostics) diagnostic(diagnostics, item);
}

} // namespace canpp::cli
