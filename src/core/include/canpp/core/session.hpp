#pragma once

#include "canpp/protocol/can/asc_importer.hpp"
#include "canpp/core/query.hpp"
#include "canpp/core/plot_data.hpp"
#include "canpp/protocol/can/dbc.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

namespace canpp::core {

enum class PrintMode { full, message, id, timestamp, variable };

class Session {
public:
    bool open(const std::filesystem::path& path, std::string& error);
    bool import_can_asc(const std::filesystem::path& input,
                        const std::filesystem::path& output,
                        protocol::can::ImportStats& stats,
                        std::string& error);
    bool load_dbc(const std::filesystem::path& path, std::string& error);

    void reset();
    bool filter_protocol(trace::ProtocolId protocol, bool original, std::string& error);
    bool filter_direction(trace::Direction direction, bool original, std::string& error);
    bool filter_payload_byte(std::size_t offset,
                             std::uint8_t value,
                             bool original,
                             std::string& error);
    bool filter_can_id(std::uint32_t id, bool original, std::string& error);
    bool filter_can_name(const std::string& name, bool original, std::string& error);
    bool filter_can_signal(const std::string& signal_name, bool original, std::string& error);
    bool filter_expression(const std::string& expression, bool original, std::string& error);
    bool filter_range(const std::string& expression, bool original, std::string& error);

    bool save(const std::filesystem::path& output, std::string& error) const;
    void print(std::ostream& output, std::size_t limit = 20, std::size_t offset = 0) const;
    void print(std::ostream& output,
               PrintMode mode,
               std::size_t limit = 20,
               std::size_t offset = 0,
               bool list = false) const;
    bool print_variable(std::ostream& output,
                        const std::string& signal_name,
                        std::size_t limit,
                        std::size_t offset,
                        std::string& error) const;
    bool print_variables(std::ostream& output,
                         const std::vector<std::string>& signal_names,
                         std::size_t limit,
                         std::size_t offset,
                         std::string& error) const;
    bool print_index(std::ostream& output,
                     std::size_t first,
                     std::size_t last,
                     std::string& error) const;
    bool print_filter(std::ostream& output,
                      const std::string& expression,
                      std::string& error) const;
    void print_history(std::ostream& output) const;
    void record_history(std::string command);
    void clear_history();
    void status(std::ostream& output) const;
    bool print_dbc_status(std::ostream& output, std::string& error) const;
    bool print_dbc_messages(std::ostream& output,
                             std::string& error,
                             bool list = false,
                             std::size_t limit = std::numeric_limits<std::size_t>::max(),
                             std::size_t offset = 0) const;
    bool print_dbc_variables(std::ostream& output,
                             std::string& error,
                             bool list = false,
                             std::size_t limit = std::numeric_limits<std::size_t>::max(),
                             std::size_t offset = 0) const;
    bool print_dbc_variable(std::ostream& output,
                            const std::string& signal_name,
                            std::string& error,
                            std::size_t limit = std::numeric_limits<std::size_t>::max(),
                            std::size_t offset = 0) const;

    [[nodiscard]] bool has_trace() const;
    [[nodiscard]] const std::filesystem::path& trace_path() const noexcept;
    [[nodiscard]] const std::filesystem::path& dbc_path() const noexcept;
    [[nodiscard]] std::size_t selection_size() const;
    [[nodiscard]] std::vector<std::string> dbc_message_names() const;
    [[nodiscard]] std::vector<std::string> dbc_signal_names() const;
    [[nodiscard]] std::vector<protocol::can::SignalDescriptor> plot_variables() const;
    bool extract_plot_data(const PlotRequest& request,
                           std::vector<PlotSeries>& output,
                           std::string& error) const;

private:
    template <typename Predicate>
    bool apply_filter(Predicate&& predicate, bool original, std::string& error);
    void print_full_record(std::ostream& output, const trace::Record& record) const;

    trace::BinaryTraceReader reader_;
    std::vector<std::uint64_t> selection_;
    protocol::can::DbcDatabase dbc_;
    std::vector<std::string> history_;
};

} // namespace canpp::core
