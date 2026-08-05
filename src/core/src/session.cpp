#include "canpp/core/session.hpp"

#include "canpp/protocol/can/can_record.hpp"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <sstream>
#include <utility>

namespace canpp::core {

bool Session::open(const std::filesystem::path& path, std::string& error) {
    if (!reader_.open(path, error)) {
        return false;
    }
    reset();
    return true;
}

bool Session::import_can_asc(const std::filesystem::path& input,
                             const std::filesystem::path& output,
                             protocol::can::ImportStats& stats,
                             std::string& error) {
    if (!protocol::can::import_asc(input, output, stats, error)) {
        return false;
    }
    return open(output, error);
}

bool Session::load_dbc(const std::filesystem::path& path, std::string& error) {
    return dbc_.load(path, error);
}

void Session::reset() {
    selection_.resize(static_cast<std::size_t>(reader_.size()));
    std::iota(selection_.begin(), selection_.end(), std::uint64_t{0});
}

template <typename Predicate>
bool Session::apply_filter(Predicate&& predicate, bool original, std::string& error) {
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    std::vector<std::uint64_t> result;
    const auto count = original ? reader_.size() : static_cast<std::uint64_t>(selection_.size());
    result.reserve(static_cast<std::size_t>(count / 4U + 1U));
    for (std::uint64_t row = 0; row < count; ++row) {
        const auto source_index = original ? row : selection_[static_cast<std::size_t>(row)];
        const auto record = reader_.read(source_index);
        if (!record) {
            error = "Cannot read record " + std::to_string(source_index);
            return false;
        }
        if (std::forward<Predicate>(predicate)(*record)) {
            result.push_back(source_index);
        }
    }
    selection_ = std::move(result);
    return true;
}

bool Session::filter_protocol(trace::ProtocolId protocol, bool original, std::string& error) {
    return apply_filter([protocol](const trace::Record& record) {
        return record.protocol == protocol;
    }, original, error);
}

bool Session::filter_direction(trace::Direction direction, bool original, std::string& error) {
    return apply_filter([direction](const trace::Record& record) {
        return record.direction == direction;
    }, original, error);
}

bool Session::filter_payload_byte(std::size_t offset,
                                  std::uint8_t value,
                                  bool original,
                                  std::string& error) {
    return apply_filter([offset, value](const trace::Record& record) {
        return offset < record.payload.size() && record.payload[offset] == value;
    }, original, error);
}

bool Session::filter_can_id(std::uint32_t id, bool original, std::string& error) {
    return apply_filter([id](const trace::Record& record) {
        const auto frame = protocol::can::decode(record);
        return frame && frame->can_id == id;
    }, original, error);
}

bool Session::filter_can_name(const std::string& name, bool original, std::string& error) {
    return apply_filter([this, &name](const trace::Record& record) {
        const auto frame = protocol::can::decode(record);
        if (!frame) {
            return false;
        }
        if (!dbc_.empty()) {
            const auto* message = dbc_.find_message(frame->can_id, frame->extended);
            return message != nullptr && message->name == name;
        }
        return frame->message_name == name;
    }, original, error);
}

bool Session::filter_can_signal(const std::string& signal_name, bool original, std::string& error) {
    if (dbc_.empty()) {
        error = "No DBC database is loaded";
        return false;
    }
    return apply_filter([this, &signal_name](const trace::Record& record) {
        const auto frame = protocol::can::decode(record);
        return frame && dbc_.has_signal(frame->can_id, frame->extended, signal_name, frame->data);
    }, original, error);
}

bool Session::filter_expression(const std::string& expression, bool original, std::string& error) {
    auto parsed = Query::parse(expression, error);
    if (!parsed) {
        return false;
    }
    auto query = std::move(*parsed);
    return apply_filter([this, query = std::move(query)](const trace::Record& record) {
        return query.matches(record, dbc_);
    }, original, error);
}

bool Session::filter_range(const std::string& expression, bool original, std::string& error) {
    auto parsed = Query::parse(expression, error);
    if (!parsed) {
        return false;
    }
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }

    const auto count = original ? reader_.size() : static_cast<std::uint64_t>(selection_.size());
    std::vector<std::uint64_t> source;
    source.reserve(static_cast<std::size_t>(count));
    if (original) {
        for (std::uint64_t index = 0; index < count; ++index) {
            source.push_back(index);
        }
    } else {
        source = selection_;
    }

    std::vector<std::uint64_t> result;
    auto query = std::move(*parsed);
    std::optional<std::uint64_t> begin;
    for (std::size_t position = 0; position < source.size(); ++position) {
        const auto source_index = source[position];
        const auto record = reader_.read(source_index);
        if (!record) {
            error = "Cannot read record " + std::to_string(source_index);
            return false;
        }
        if (!query.matches(*record, dbc_)) {
            continue;
        }
        if (begin) {
            for (std::size_t interior = static_cast<std::size_t>(*begin) + 1U;
                 interior < position; ++interior) {
                result.push_back(source[interior]);
            }
        }
        begin = static_cast<std::uint64_t>(position);
    }
    selection_ = std::move(result);
    return true;
}

bool Session::save(const std::filesystem::path& output, std::string& error) const {
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    return trace::write_selection(reader_, selection_, output, error);
}

void Session::print(std::ostream& output, std::size_t limit, std::size_t offset) const {
    const auto end = std::min(selection_.size(), offset + limit);
    for (std::size_t row = offset; row < end; ++row) {
        const auto record = reader_.read(selection_[row]);
        if (!record) {
            continue;
        }
        if (const auto frame = protocol::can::decode(*record)) {
            output << std::fixed << std::setprecision(6)
                   << static_cast<double>(frame->timestamp_ns) / 1'000'000'000.0 << " CAN"
                   << (frame->fd ? "FD " : " ") << frame->stream_id << ' '
                   << (frame->direction == trace::Direction::rx ? "Rx " : "Tx ")
                   << std::hex << std::uppercase << frame->can_id
                   << (frame->extended ? "x " : " ") << std::dec
                   << frame->message_name << " [" << frame->data.size() << ']';
            for (const auto byte : frame->data) {
                output << ' ' << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<unsigned>(byte);
            }
            if (!dbc_.empty()) {
                const auto values = dbc_.decode(frame->can_id, frame->extended, frame->data);
                for (const auto& value : values) {
                    output << ' ' << value.name << '=';
                    if (value.description) {
                        output << *value.description << " (" << std::fixed << std::setprecision(6)
                               << value.value;
                        if (!value.unit.empty()) {
                            output << ' ' << value.unit;
                        }
                        output << ')';
                    } else {
                        output << std::fixed << std::setprecision(6) << value.value;
                        if (!value.unit.empty()) {
                            output << ' ' << value.unit;
                        }
                    }
                }
            }
            output << std::dec << std::setfill(' ') << '\n';
        } else {
            output << std::fixed << std::setprecision(6)
                   << static_cast<double>(record->timestamp_ns) / 1'000'000'000.0
                   << " protocol=" << static_cast<unsigned>(record->protocol)
                   << " stream=" << record->stream_id
                   << " bytes=" << record->payload.size() << '\n';
        }
    }
}

void Session::status(std::ostream& output) const {
    if (!has_trace()) {
        output << "No communication trace open\n";
        return;
    }
    output << "File: " << reader_.path() << '\n'
           << "Records: " << reader_.size() << '\n'
           << "Current selection: " << selection_.size() << '\n';
    if (!dbc_.empty()) {
        output << "DBC: " << dbc_.path() << '\n';
    }
}

bool Session::has_trace() const { return reader_.is_open(); }
const std::filesystem::path& Session::trace_path() const noexcept { return reader_.path(); }
const std::filesystem::path& Session::dbc_path() const noexcept { return dbc_.path(); }
std::size_t Session::selection_size() const { return selection_.size(); }

std::vector<std::string> Session::dbc_message_names() const { return dbc_.message_names(); }
std::vector<std::string> Session::dbc_signal_names() const { return dbc_.signal_names(); }

std::vector<protocol::can::SignalDescriptor> Session::plot_variables() const {
    return dbc_.signal_catalog();
}

bool Session::extract_plot_data(const PlotRequest& request,
                                std::vector<PlotSeries>& output,
                                std::string& error) const {
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    if (dbc_.empty()) {
        error = "No DBC database is loaded";
        return false;
    }
    if (request.variables.empty()) {
        error = "At least one graph variable is required";
        return false;
    }

    std::vector<PlotSeries> result;
    result.reserve(request.variables.size());
    for (const auto& variable : request.variables) {
        const auto* message = dbc_.find_message(variable.can_id, variable.extended);
        if (message == nullptr || message->name != variable.message_name ||
            dbc_.find_signal(variable.can_id, variable.extended, variable.signal_name) == nullptr) {
            error = "Unknown graph variable: " + variable.message_name + "." + variable.signal_name +
                    " (CAN ID 0x" + [&variable] {
                        std::ostringstream id;
                        id << std::hex << std::uppercase << variable.can_id;
                        return id.str();
                    }() + (variable.extended ? ", extended)" : ", standard)");
            return false;
        }
        const auto* signal = dbc_.find_signal(variable.can_id, variable.extended, variable.signal_name);
        PlotSeries series;
        series.variable = variable;
        series.unit = signal->unit;
        result.push_back(std::move(series));
    }

    std::vector<std::uint64_t> source;
    if (request.source == PlotSource::full_trace) {
        source.resize(static_cast<std::size_t>(reader_.size()));
        std::iota(source.begin(), source.end(), std::uint64_t{0});
    } else {
        source = selection_;
    }
    for (auto& series : result) {
        series.samples.reserve(source.size());
    }

    for (const auto source_index : source) {
        const auto record = reader_.read(source_index);
        if (!record) {
            error = "Cannot read record " + std::to_string(source_index);
            return false;
        }
        std::vector<protocol::can::DbcSignalValue> decoded;
        const auto frame = protocol::can::decode(*record);
        if (record->protocol == trace::ProtocolId::can && !frame) {
            error = "Cannot decode CAN record " + std::to_string(source_index);
            return false;
        }
        if (frame) {
            decoded = dbc_.decode(frame->can_id, frame->extended, frame->data);
        }
        for (auto& series : result) {
            std::optional<double> value;
            if (frame) {
                const auto& key = series.variable;
                if (frame->can_id == key.can_id && frame->extended == key.extended) {
                    const auto decoded_value = std::find_if(
                        decoded.begin(), decoded.end(), [&key](const protocol::can::DbcSignalValue& candidate) {
                            return candidate.name == key.signal_name;
                        });
                    if (decoded_value != decoded.end()) {
                        value = decoded_value->value;
                    }
                }
            }
            series.samples.push_back(PlotSample{record->timestamp_ns, value});
        }
    }
    output = std::move(result);
    return true;
}

} // namespace canpp::core
