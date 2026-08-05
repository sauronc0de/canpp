#include "canpp/core/session.hpp"
#include "canpp/protocol/can/can_record.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const auto dbc_path = directory / "canpp_plot_test.dbc";
    const auto trace_path = directory / "canpp_plot_test.commtrace";
    {
        std::ofstream dbc(dbc_path);
        dbc << "BO_ 256 Engine: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (2,5) [0|515] \"km/h\" ECU\n"
               "BO_ 512 Dashboard: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (1,0) [0|255] \"km/h\" ECU\n";
    }
    canpp::trace::BinaryTraceWriter writer(trace_path);
    assert(writer.good());
    for (std::uint64_t index = 0; index < 3U; ++index) {
        canpp::protocol::can::Frame frame;
        frame.timestamp_ns = index * 1'000'000U;
        frame.can_id = index == 1U ? 512U : 256U;
        frame.message_name = index == 1U ? "Dashboard" : "Engine";
        frame.dlc = 8U;
        frame.data.assign(8U, 0U);
        frame.data[0] = index == 1U ? 7U : 10U;
        assert(writer.append(canpp::protocol::can::encode(frame)));
    }
    assert(writer.finalize());

    std::string error;
    canpp::core::Session session;
    assert(session.open(trace_path, error));
    assert(session.load_dbc(dbc_path, error));
    const auto variables = session.plot_variables();
    assert(variables.size() == 2U);
    assert(variables[0].key.message_name == "Engine");
    assert(variables[0].key.signal_name == "Speed");
    assert(variables[1].key.message_name == "Dashboard");
    assert(variables[1].key.signal_name == "Speed");

    canpp::core::PlotRequest request;
    request.variables.push_back(variables[0].key);
    request.variables.push_back(variables[1].key);
    std::vector<canpp::core::PlotSeries> series;
    assert(session.extract_plot_data(request, series, error));
    assert(series.size() == 2U);
    assert(series[0].unit == "km/h");
    assert(series[0].samples.size() == 3U);
    assert(series[0].samples[0].value && *series[0].samples[0].value == 25.0);
    assert(!series[0].samples[1].value);
    assert(series[0].samples[2].value && *series[0].samples[2].value == 25.0);
    assert(!series[1].samples[0].value);
    assert(series[1].samples[1].value && *series[1].samples[1].value == 7.0);
    assert(!series[1].samples[2].value);

    assert(session.filter_can_id(256U, false, error));
    request.variables = {variables[0].key};
    request.source = canpp::core::PlotSource::current_selection;
    assert(session.extract_plot_data(request, series, error));
    assert(series[0].samples.size() == 2U);
    assert(series[0].samples[0].value && series[0].samples[1].value);
    request.variables = {variables[1].key};
    request.source = canpp::core::PlotSource::full_trace;
    assert(session.extract_plot_data(request, series, error));
    assert(series[0].samples.size() == 3U);
    assert(series[0].samples[1].value);

    const auto previous = series;
    request.variables.clear();
    assert(!session.extract_plot_data(request, series, error));
    assert(series.size() == previous.size());

    request.variables = {variables[0].key};
    request.variables[0].can_id = 0x123U;
    error.clear();
    assert(!session.extract_plot_data(request, series, error));
    assert(error.find("CAN ID 0x123") != std::string::npos);
    assert(error.find("standard") != std::string::npos);
    assert(series.size() == previous.size());

    const auto malformed_trace_path = directory / "canpp_plot_malformed.commtrace";
    canpp::trace::BinaryTraceWriter malformed_writer(malformed_trace_path);
    assert(malformed_writer.good());
    canpp::protocol::can::Frame valid_frame;
    valid_frame.timestamp_ns = 10U;
    valid_frame.can_id = 256U;
    valid_frame.message_name = "Engine";
    valid_frame.dlc = 8U;
    valid_frame.data.assign(8U, 1U);
    assert(malformed_writer.append(canpp::protocol::can::encode(valid_frame)));
    canpp::trace::Record malformed;
    malformed.timestamp_ns = 20U;
    malformed.protocol = canpp::trace::ProtocolId::can;
    malformed.payload.assign(8U, 0U);
    assert(malformed_writer.append(malformed));
    assert(malformed_writer.finalize());

    canpp::core::Session malformed_session;
    assert(malformed_session.open(malformed_trace_path, error));
    assert(malformed_session.load_dbc(dbc_path, error));
    request.variables = {variables[0].key};
    request.source = canpp::core::PlotSource::full_trace;
    std::vector<canpp::core::PlotSeries> malformed_output;
    assert(!malformed_session.extract_plot_data(request, malformed_output, error));
    assert(error.find("CAN record 1") != std::string::npos);
    assert(malformed_output.empty());

    std::filesystem::remove(dbc_path);
    std::filesystem::remove(trace_path);
    std::filesystem::remove(malformed_trace_path);
}
