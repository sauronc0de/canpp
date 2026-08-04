#include "canpp/core/session.hpp"

#include <charconv>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

void print_help() {
    std::cout
        << "import asc <input.asc> <output.commtrace>\n"
        << "open <file.commtrace>\n"
        << "status | reset\n"
        << "filter protocol can [original]\n"
        << "filter direction rx|tx [original]\n"
        << "filter payload <offset> <hex-byte> [original]\n"
        << "filter can id <hex-id> [original]\n"
        << "filter can name <message-name> [original]\n"
        << "print [limit] [offset]\n"
        << "save <output.commtrace>\n"
        << "exit\n";
}

} // namespace

int main() {
    canpp::core::Session session;
    std::cout << "Canpp communication trace CLI - type 'help'\n";
    for (std::string line; std::cout << "canpp> " && std::getline(std::cin, line);) {
        const auto args = split(line);
        if (args.empty()) {
            continue;
        }
        std::string error;
        bool success = false;
        if (args[0] == "exit" || args[0] == "quit") {
            break;
        }
        if (args[0] == "help") {
            print_help();
            continue;
        }
        if (args[0] == "status") {
            session.status(std::cout);
            continue;
        }
        if (args[0] == "reset") {
            session.reset();
            session.status(std::cout);
            continue;
        }
        if (args[0] == "open" && args.size() == 2U) {
            success = session.open(args[1], error);
        } else if (args[0] == "import" && args.size() == 4U && args[1] == "asc") {
            canpp::protocol::can::ImportStats stats;
            success = session.import_can_asc(args[2], args[3], stats, error);
            if (success) {
                std::cout << "Imported " << stats.imported << ", skipped " << stats.skipped << '\n';
            }
        } else if (args[0] == "save" && args.size() == 2U) {
            success = session.save(args[1], error);
        } else if (args[0] == "print") {
            std::size_t limit = 20;
            std::size_t offset = 0;
            if ((args.size() > 1U && !parse_number(args[1], limit, 10)) ||
                (args.size() > 2U && !parse_number(args[2], offset, 10))) {
                std::cerr << "Invalid print range\n";
                continue;
            }
            session.print(std::cout, limit, offset);
            continue;
        } else if (args[0] == "filter" && args.size() >= 3U) {
            if (args[1] == "protocol" && args[2] == "can") {
                success = session.filter_protocol(canpp::trace::ProtocolId::can,
                                                  from_original(args, 3), error);
            } else if (args[1] == "direction") {
                const auto direction = args[2] == "rx" ? canpp::trace::Direction::rx
                                     : args[2] == "tx" ? canpp::trace::Direction::tx
                                                       : canpp::trace::Direction::unknown;
                if (direction == canpp::trace::Direction::unknown) {
                    std::cerr << "Direction must be rx or tx\n";
                    continue;
                }
                success = session.filter_direction(direction, from_original(args, 3), error);
            } else if (args[1] == "payload" && args.size() >= 4U) {
                std::size_t offset{};
                unsigned int value{};
                if (!parse_number(args[2], offset, 10) || !parse_number(args[3], value, 16) || value > 255U) {
                    std::cerr << "Usage: filter payload <offset> <hex-byte> [original]\n";
                    continue;
                }
                success = session.filter_payload_byte(offset, static_cast<std::uint8_t>(value),
                                                      from_original(args, 4), error);
            } else if (args[1] == "can" && args.size() >= 4U && args[2] == "id") {
                std::uint32_t id{};
                if (!parse_number(args[3], id, 16)) {
                    std::cerr << "Invalid CAN ID\n";
                    continue;
                }
                success = session.filter_can_id(id, from_original(args, 4), error);
            } else if (args[1] == "can" && args.size() >= 4U && args[2] == "name") {
                success = session.filter_can_name(args[3], from_original(args, 4), error);
            }
        } else {
            std::cerr << "Unknown command\n";
            continue;
        }

        if (!success) {
            std::cerr << "Error: " << (error.empty() ? "invalid command" : error) << '\n';
        } else {
            session.status(std::cout);
        }
    }
    return 0;
}
