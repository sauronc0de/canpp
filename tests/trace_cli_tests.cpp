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
    {
        std::ofstream input(input_path);
        input << "print unknown\nprint 1 invalid\nquit\n";
    }
    const auto command = shell_quote(argv[1]) + " < " + shell_quote(input_path) + " > " +
                         shell_quote(output_path) + " 2>&1";
    assert(std::system(command.c_str()) == 0);

    std::ifstream output(output_path);
    const std::string text{std::istreambuf_iterator<char>(output), std::istreambuf_iterator<char>()};
    assert(text.find("Invalid print mode") != std::string::npos);
    assert(text.find("Invalid print range") != std::string::npos);

    std::filesystem::remove(input_path);
    std::filesystem::remove(output_path);
}
