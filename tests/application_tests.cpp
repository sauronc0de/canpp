#include "canpp/application/command_executor.hpp"
#include "canpp/application/command_parser.hpp"
#include "canpp/protocol/can/can_record.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const auto trace_path = directory / "canpp_application_test.commtrace";
    const auto dbc_path = directory / "canpp_application_test.dbc";
    {
        std::ofstream dbc(dbc_path);
        dbc << "BO_ 256 Example: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (1,0) [0|255] \"\" ECU\n";
    }
    {
        canpp::trace::BinaryTraceWriter writer(trace_path);
        assert(writer.good());
        canpp::protocol::can::Frame frame;
        frame.can_id = 256;
        frame.message_name = "Example";
        frame.data = {100};
        assert(writer.append(canpp::protocol::can::encode(frame)));
        assert(writer.finalize());
    }

    canpp::core::Session session;
    canpp::application::CommandParser parser;
    canpp::application::CommandExecutor executor(session);
    auto run = [&](const std::string& line) {
        const auto parsed = parser.parse_line(line);
        assert(parsed);
        return executor.execute(*parsed, {.include_status = false});
    };

    auto result = run("open '" + trace_path.string() + "'");
    assert(result.success && result.state_changed);
    result = run("load dbc '" + dbc_path.string() + "'");
    assert(result.success && result.state_changed);
    result = run("filter protocol can original");
    assert(result.success && result.state_changed);
    result = run("print messages list");
    assert(result.success);
    assert(result.output.find("Example") != std::string::npos);

    const auto quoted = parser.parse_line("print message grep \"Example bus\"");
    assert(quoted && quoted->tokens.back() == "Example bus");
    const auto malformed = parser.parse_line("open 'unfinished");
    assert(!malformed);

    std::filesystem::remove(trace_path);
    std::filesystem::remove(dbc_path);
}
