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
    {
        canpp::trace::BinaryTraceWriter writer(trace_path);
        assert(writer.good());
        canpp::protocol::can::Frame frame;
        frame.can_id = 256;
        frame.message_name = "legacy-name";
        frame.data = {0x01};
        assert(writer.append(canpp::protocol::can::encode(frame)));
        assert(writer.finalize());
    }
    {
        std::ofstream input(input_path);
        input << "print unknown\nprint 1 invalid\nprint variable\nprint variable Speed\n"
              << "open " << trace_path.string() << "\n"
              << "print filter message.name == \"legacy-name\"\n"
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
    assert(text.find("0.000000 CAN 0 Tx 100 legacy-name") != std::string::npos);
    assert(text.find("1 open ") != std::string::npos);
    assert(text.find("2 reset") != std::string::npos);
    assert(text.find("Invalid index: expected a non-negative integer") != std::string::npos);
    assert(text.find("Index out of bounds") != std::string::npos);

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
    std::filesystem::remove(trace_path);
}
