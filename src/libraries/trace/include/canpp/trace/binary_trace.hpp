#pragma once

#include "canpp/trace/types.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace canpp::trace {

class BinaryTraceWriter {
public:
    explicit BinaryTraceWriter(const std::filesystem::path& path);
    ~BinaryTraceWriter();

    BinaryTraceWriter(const BinaryTraceWriter&) = delete;
    BinaryTraceWriter& operator=(const BinaryTraceWriter&) = delete;

    [[nodiscard]] bool good() const;
    [[nodiscard]] const std::string& error() const;
    bool append(const Record& record);
    bool finalize();

private:
    std::ofstream stream_;
    std::uint64_t record_count_{};
    std::string error_;
    bool finalized_{};
};

class BinaryTraceReader {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    void close();

    [[nodiscard]] bool is_open() const;
    [[nodiscard]] std::uint64_t size() const;
    [[nodiscard]] const std::filesystem::path& path() const;
    [[nodiscard]] std::optional<Record> read(std::uint64_t index) const;

private:
    std::filesystem::path path_;
    mutable std::ifstream stream_;
    std::vector<std::uint64_t> record_offsets_;
};

bool write_selection(const BinaryTraceReader& source,
                     const std::vector<std::uint64_t>& indices,
                     const std::filesystem::path& output,
                     std::string& error);

} // namespace canpp::trace
