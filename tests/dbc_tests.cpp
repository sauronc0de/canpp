#include "canpp/core/session.hpp"
#include "canpp/protocol/can/can_record.hpp"
#include "canpp/protocol/can/dbc.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

int main() {
    const auto directory = std::filesystem::temp_directory_path();
    const auto dbc_path = directory / "canpp_dbc_test.dbc";
    const auto trace_path = directory / "canpp_dbc_test.commtrace";
    {
        std::ofstream dbc(dbc_path);
        dbc << "BO_ 256 Example: 8 ECU\n"
               " SG_ Speed : 0|8@1+ (0.5,-1) [0|127] \"km/h\" ECU\n"
               " SG_ Mode : 8|2@1+ (1,0) [0|3] \"\" ECU\n"
               " SG_ Wide : 16|65@1+ (1,0) [0|0] \"\" ECU\n"
               " SG_ Truncated : 63|8@1+ (1,0) [0|0] \"\" ECU\n"
               " SG_ Selector M : 24|2@1+ (1,0) [0|3] \"\" ECU\n"
               " SG_ BranchOff m0 : 32|8@1+ (1,0) [0|255] \"\" ECU\n"
               " SG_ BranchOn m1 : 32|8@1+ (1,0) [0|255] \"\" ECU\n"
               " SG_ Motorola : 47|8@0+ (1,0) [0|255] \"\" ECU\n"
               "VAL_ 256 Mode 0 \"Off\" 1 \"On\" ;\n"
               "BO_ 257 UnsignedMessage: 8 ECU\n"
               " SG_ Unsigned64 : 0|64@1+ (1,0) [0|0] \"\" ECU\n"
               "VAL_ 257 Unsigned64 18446744073709551615 \"Max\" ;\n"
               "BO_ 2147483904 Extended: 8 ECU\n"
               " SG_ Value : 0|8@1+ (1,0) [0|255] \"\" ECU\n"
               "BO_ 258 Short: 4 ECU\n"
               " SG_ Outside : 32|1@1+ (1,0) [0|1] \"\" ECU\n";
    }

    canpp::protocol::can::DbcDatabase database;
    std::string error;
    assert(database.load(dbc_path, error));
    const std::uint8_t payload[] = {42, 1, 0, 1, 7, 0xA5, 0, 0xFF};
    const auto values = database.decode(256, false, payload);
    assert(values.size() == 5U);
    assert(values[0].name == "Speed" && values[0].value == 20.0);
    assert(values[1].description && *values[1].description == "On");
    assert(values[2].name == "Selector" && values[2].raw_value == 1U);
    assert(values[3].name == "BranchOn" && values[3].raw_value == 7U);
    assert(std::none_of(values.begin(), values.end(), [](const auto& value) {
        return value.name == "BranchOff";
    }));
    assert(values[4].name == "Motorola" && values[4].raw_value == 0xA5U);
    const std::uint8_t unsigned_payload[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const auto unsigned_values = database.decode(257, false, unsigned_payload);
    assert(unsigned_values.size() == 1U);
    assert(unsigned_values[0].raw_value == 0xFFFFFFFFFFFFFFFFULL);
    assert(unsigned_values[0].description && *unsigned_values[0].description == "Max");
    assert(database.has_signal(256, false, "BranchOn", payload));
    assert(!database.has_signal(256, false, "BranchOff", payload));
    assert(!database.has_signal(256, false, "Wide", payload));
    assert(!database.has_signal(256, false, "Truncated", payload));
    assert(!database.has_signal(258, false, "Outside", payload));
    const auto message_names = database.message_names();
    assert(message_names.size() == 4U && message_names[0] == "Example");
    const auto signal_names = database.signal_names();
    assert(std::find(signal_names.begin(), signal_names.end(), "Speed") != signal_names.end());
    const std::uint8_t short_payload[] = {42, 1, 0, 1};
    assert(database.decode(256, false, short_payload).empty());
    assert(!database.has_signal(256, false, "Mode", short_payload));
    const auto* extended = database.find_message(0x100U, true);
    assert(extended != nullptr && extended->name == "Extended");
    assert(database.decode(0x100U, true, payload).size() == 1U);

    canpp::trace::BinaryTraceWriter writer(trace_path);
    assert(writer.good());
    canpp::protocol::can::Frame frame;
    frame.can_id = 256;
    frame.dlc = 8;
    frame.message_name = "legacy-name";
    frame.data.assign(std::begin(payload), std::end(payload));
    assert(writer.append(canpp::protocol::can::encode(frame)));
    canpp::trace::Record non_can;
    non_can.timestamp_ns = 1'000'000'000;
    non_can.protocol = canpp::trace::ProtocolId::uart;
    non_can.payload = {0x01, 0x02};
    assert(writer.append(non_can));
    assert(writer.finalize());

    canpp::core::Session legacy_session;
    assert(legacy_session.open(trace_path, error));
    assert(legacy_session.filter_can_name("legacy-name", true, error));
    assert(legacy_session.selection_size() == 1U);
    std::ostringstream unavailable_variables;
    legacy_session.print(unavailable_variables, canpp::core::PrintMode::variable);
    assert(unavailable_variables.str() == "N/A (DBC values unavailable)\n");

    canpp::core::Session session;
    assert(session.open(trace_path, error));
    assert(session.load_dbc(dbc_path, error));
    assert(session.dbc_message_names().size() == 4U);
    assert(!session.dbc_signal_names().empty());
    assert(session.filter_can_name("Example", true, error));
    assert(session.selection_size() == 1U);
    session.reset();
    assert(session.filter_can_signal("Mode", true, error));
    assert(session.selection_size() == 1U);
    session.reset();
    assert(session.filter_can_signal("Wide", true, error));
    assert(session.selection_size() == 0U);
    session.reset();
    std::ostringstream printed;
    session.print(printed);
    assert(printed.str().find("Speed=20.000000 km/h") != std::string::npos);
    assert(printed.str().find("Mode=On") != std::string::npos);

    std::ostringstream messages;
    session.print(messages, canpp::core::PrintMode::message);
    assert(messages.str() == "Index Timestamp Message\n0 0.000000 Example\n");
    std::ostringstream ids;
    session.print(ids, canpp::core::PrintMode::id);
    assert(ids.str() == "Index Timestamp ID\n0 0.000000 100\n");
    std::ostringstream timestamps;
    session.print(timestamps, canpp::core::PrintMode::timestamp);
    assert(timestamps.str() == "Index Timestamp\n0 0.000000\n1 1.000000\n");
    std::ostringstream variables;
    session.print(variables, canpp::core::PrintMode::variable);
    assert(variables.str().find("Speed=20.000000 km/h Mode=On") != std::string::npos);
    assert(variables.str().find("N/A (not a CAN record)") != std::string::npos);

    std::ostringstream selected_variable;
    error.clear();
    assert(session.print_variable(selected_variable, "Speed", 20, 0, error));
    assert(selected_variable.str() == "Index Timestamp Value\n0 0.000000 20.000000\n");

    std::ostringstream indexed;
    error.clear();
    assert(session.print_index(indexed, 0, 0, error));
    assert(indexed.str().find("legacy-name") != std::string::npos);
    assert(indexed.str().find("protocol=") == std::string::npos);
    std::ostringstream indexed_range;
    error.clear();
    assert(session.print_index(indexed_range, 0, 1, error));
    const auto indexed_range_text = indexed_range.str();
    assert(std::count(indexed_range_text.begin(), indexed_range_text.end(), '\n') == 2);
    error.clear();
    assert(!session.print_index(indexed_range, 0, 2, error));
    assert(error.find("Index out of bounds") != std::string::npos);
    const auto selection_before_print_filter = session.selection_size();
    std::ostringstream filtered_print;
    error.clear();
    assert(session.print_filter(filtered_print, "message.name == \"Example\"", error));
    assert(filtered_print.str().find("legacy-name") != std::string::npos);
    assert(session.selection_size() == selection_before_print_filter);

    session.clear_history();
    session.record_history("reset");
    session.record_history("filter message.name == \"Example\"");
    std::ostringstream history;
    session.print_history(history);
    assert(history.str() == "1 reset\n2 filter message.name == \"Example\"\n");
    std::ostringstream unavailable_signal;
    error.clear();
    assert(session.print_variable(unavailable_signal, "Wide", 20, 0, error));
    assert(unavailable_signal.str() == "Index Timestamp Value\n");
    std::ostringstream unknown_signal;
    error.clear();
    assert(!session.print_variable(unknown_signal, "Unknown", 20, 0, error));
    assert(error == "Unknown DBC signal: Unknown");
    error.clear();
    assert(!legacy_session.print_variable(unknown_signal, "Speed", 20, 0, error));
    assert(error == "No DBC database is loaded");

    std::ostringstream limited;
    session.print(limited, canpp::core::PrintMode::id, 1, 1);
    assert(limited.str() == "Index Timestamp ID\n");

    std::filesystem::remove(dbc_path);
    std::filesystem::remove(trace_path);
}
