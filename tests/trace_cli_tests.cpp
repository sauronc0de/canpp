#include "canpp/protocol/can/can_record.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

std::string shell_quote(const std::filesystem::path& path) {
#ifdef _WIN32
    return "\"" + path.string() + "\"";
#else
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
#endif
}

} // namespace

int main(int argc, char** argv) {
    assert(argc == 2 && argv[1] != nullptr);
    const auto directory = std::filesystem::temp_directory_path();
    const auto input_path = directory / "canpp_trace_cli_test_input.txt";
    const auto output_path = directory / "canpp_trace_cli_test_output.txt";
    const auto trace_path = directory / "canpp_trace_cli_test.commtrace";
    const auto dbc_path = directory / "canpp_trace_cli_test.dbc";
    {
        std::ofstream dbc(dbc_path);
        dbc << "BO_ 256 Example: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (1,0) [0|255] \"\" ECU\n"
               " SG_ Mode : 8|2@1+ (1,0) [0|3] \"\" ECU\n"
               "BO_ 512 LS_Message: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (1,0) [0|255] \"\" ECU\n";
    }
    {
        canpp::trace::BinaryTraceWriter writer(trace_path);
        assert(writer.good());
        canpp::protocol::can::Frame frame;
        frame.can_id = 256;
        frame.message_name = "legacy-name";
        frame.data = {0x01};
        assert(writer.append(canpp::protocol::can::encode(frame)));
        frame.timestamp_ns = 1'000'000'000;
        frame.can_id = 512;
        frame.message_name = "LS Message";
        frame.data = {0x02};
        assert(writer.append(canpp::protocol::can::encode(frame)));
        assert(writer.finalize());
    }
    {
        std::ofstream input(input_path);
        input << "print unknown\nprint 1 invalid\nprint variable\nprint variable Speed\n"
              << "print variable Speed &\nprint variable & Speed\nprint variable Speed & & Mode\n"
              << "open " << trace_path.string() << "\n"
              << "print filter message.name == \"legacy-name\"\n"
              << "print messages grep \"LS Message\"\n"
              << "print grep \"legacy-name\"\n"
              << "print 1 1 grep \"LS Message\"\n"
              << "print id grep \"200\"\n"
              << "print message grep \"ls\"\n"
              << "print message grep\nprint message grep \"\"\n"
              << "print message grep \"legacy-name\" extra\n"
              << "load dbc " << dbc_path.string() << "\n"
              << "dbc messages list offset 1 limit 1\n"
              << "dbc variables list grep Speed limit 1 offset 0\n"
              << "dbc variable list limit 1 offset 1\n"
              << "print messages list grep \"LS_Message\" limit 10 offset 0\n"
              << "history\nprint index 0\nprint index invalid\nprint index 9\nreset\nhistory\nquit\n";
    }
    const auto command = shell_quote(argv[1]) + " < " + shell_quote(input_path) + " > " +
                         shell_quote(output_path) + " 2>&1";
    assert(std::system(command.c_str()) == 0);

    std::ifstream output(output_path);
    const std::string text{std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
    assert(text.find("Invalid print mode") != std::string::npos);
    assert(text.find("Invalid print range") != std::string::npos);
    assert(text.find("Error: No DBC database is loaded") != std::string::npos);
    assert(text.find("Malformed variable list: expected a signal name after '&'") != std::string::npos);
    assert(text.find("0.000000 CAN 0 Tx 100 legacy-name") != std::string::npos);
    assert(text.find("1 1.000000 LS Message") != std::string::npos);
    assert(text.find("LS_Message\n") != std::string::npos);
    assert(text.find("Speed\n") != std::string::npos);
    assert(text.find("Mode\n") != std::string::npos);
    assert(text.find("0.000000 CAN 0 Tx 100 legacy-name") != std::string::npos);
    assert(text.find("1 1.000000 200") != std::string::npos);
    assert(text.find("Usage: print ... grep <pattern>") != std::string::npos);
    assert(text.find("1 open ") != std::string::npos);
    assert(text.find("3 reset") != std::string::npos);
    assert(text.find("Invalid index: expected a non-negative integer") != std::string::npos);
    assert(text.find("Index out of bounds") != std::string::npos);

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
    std::filesystem::remove(trace_path);
    std::filesystem::remove(dbc_path);
}
