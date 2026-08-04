#pragma once

#include "canpp/protocol/can/asc_importer.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace canpp::core {

class Session {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool import_can_asc(const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        protocol::can::ImportStats& stats,
                        std::string& error);

    void reset();
    bool filter_protocol(trace::ProtocolId protocol, bool original, std::string& error);
    bool filter_direction(trace::Direction direction, bool original, std::string& error);
    bool filter_payload_byte(std::size_t offset,
                             std::uint8_t value,
                             bool original,
                             std::string& error);
    bool filter_can_id(std::uint32_t id, bool original, std::string& error);
    bool filter_can_name(const std::string& name, bool original, std::string& error);

    bool save(const std::filesystem::path& output, std::string& error) const;
    void print(std::ostream& output, std::size_t limit = 20, std::size_t offset = 0) const;
    void status(std::ostream& output) const;

    [[nodiscard]] bool has_trace() const;
    [[nodiscard]] std::size_t selection_size() const;

private:
    template <typename Predicate>
    bool apply_filter(Predicate&& predicate, bool original, std::string& error);

    trace::BinaryTraceReader reader_;
    std::vector<std::uint64_t> selection_;
};

} // namespace canpp::core
