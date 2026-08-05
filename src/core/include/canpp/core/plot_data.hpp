#pragma once

#include "canpp/protocol/can/dbc.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace canpp::core {

enum class PlotSource { current_selection, full_trace };

struct PlotRequest {
    std::vector<protocol::can::SignalKey> variables;
    PlotSource source{PlotSource::current_selection};
};

struct PlotSample {
    std::uint64_t timestamp_ns{};
    std::optional<double> value;
};

struct PlotSeries {
    protocol::can::SignalKey variable;
    std::string unit;
    std::vector<PlotSample> samples;
};

} // namespace canpp::core
