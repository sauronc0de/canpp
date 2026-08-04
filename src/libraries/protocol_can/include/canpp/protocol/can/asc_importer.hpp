#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace canpp::protocol::can {

struct ImportStats {
    std::uint64_t imported{};
    std::uint64_t skipped{};
};

bool import_asc(const std::filesystem::path& input,
                const std::filesystem::path& output,
                ImportStats& stats,
                std::string& error);

} // namespace canpp::protocol::can
