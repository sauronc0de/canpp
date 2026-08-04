#include "canpp/core/session.hpp"
#include "canpp/protocol/can/can_record.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const auto dbc_path = directory / "canpp_query_test.dbc";
    const auto trace_path = directory / "canpp_query_test.commtrace";
    {
        std::ofstream dbc(dbc_path);
        dbc << "BO_ 256 Example: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (1,0) [0|255] \"km/h\" ECU\n";
    }
    canpp::trace::BinaryTraceWriter writer(trace_path);
    assert(writer.good());
    for (std::uint64_t index = 0; index < 6U; ++index) {
        canpp::protocol::can::Frame frame;
        frame.timestamp_ns = index * 1'000'000'000U;
        frame.stream_id = index % 2U;
        frame.can_id = 256U;
        frame.direction = index % 2U == 0U ? canpp::trace::Direction::rx : canpp::trace::Direction::tx;
        frame.dlc = 8U;
        frame.data.assign(8U, 0U);
        frame.data[0] = static_cast<std::uint8_t>(index * 10U);
        assert(writer.append(canpp::protocol::can::encode(frame)));
    }
    assert(writer.finalize());

    std::string error;
    canpp::core::Session session;
    assert(session.open(trace_path, error));
    assert(session.load_dbc(dbc_path, error));
    assert(session.filter_expression("signal.Speed >= 20 && record.direction == \"rx\"", true, error));
    assert(session.selection_size() == 2U);
    session.reset();
    assert(session.filter_expression("time >= 2 && time < 4", true, error));
    assert(session.selection_size() == 2U);
    session.reset();
    assert(session.filter_range("signal.Speed == 0 || signal.Speed == 50", true, error));
    assert(session.selection_size() == 4U);
    session.reset();
    assert(session.filter_expression("record.stream_id == 0 || signal.Speed == 50", true, error));
    assert(session.filter_range("signal.Speed == 0 || signal.Speed == 50", false, error));
    assert(session.selection_size() == 2U);
    session.reset();
    // Missing DBC signals are unknown: a decisive logical operand still
    // determines the result, while an unknown-only expression does not match.
    assert(session.filter_expression("signal.Missing == 1 || true", true, error));
    assert(session.selection_size() == 6U);
    session.reset();
    assert(session.filter_expression("signal.Missing == 1 && false", true, error));
    assert(session.selection_size() == 0U);
    session.reset();
    assert(session.filter_expression("true || signal.Missing == 1", true, error));
    assert(session.selection_size() == 6U);
    session.reset();
    assert(session.filter_expression("false && signal.Missing == 1", true, error));
    assert(session.selection_size() == 0U);
    session.reset();
    assert(session.filter_expression("message.id == 0x100 && !message.extended", true, error));
    assert(session.selection_size() == 6U);
    session.reset();
    assert(!session.filter_expression("record.protocol =", true, error));
    assert(error.find("position") != std::string::npos);
    assert(session.selection_size() == 6U);

    canpp::trace::Record high_timestamp;
    high_timestamp.timestamp_ns = (std::uint64_t{1} << 53U) + 1U;
    canpp::protocol::can::DbcDatabase empty_dbc;
    error.clear();
    auto exact_timestamp = canpp::core::Query::parse("timestamp_ns == 9007199254740993", error);
    assert(exact_timestamp);
    assert(exact_timestamp->matches(high_timestamp, empty_dbc));
    auto neighboring_timestamp = canpp::core::Query::parse("timestamp_ns == 9007199254740992", error);
    assert(neighboring_timestamp);
    assert(!neighboring_timestamp->matches(high_timestamp, empty_dbc));
    auto exact_hex_timestamp = canpp::core::Query::parse("timestamp_ns == 0x20000000000001", error);
    assert(exact_hex_timestamp);
    assert(exact_hex_timestamp->matches(high_timestamp, empty_dbc));

    std::filesystem::remove(dbc_path);
    std::filesystem::remove(trace_path);
}
