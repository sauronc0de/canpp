#include <doctest/doctest.h>

#include "can_script_lua/lua_engine.hpp"

TEST_CASE("can_script_lua executes configured Lua entry points for event and decoded views")
{
  can_script_lua::LuaEngine engine;
  const can_script_api::ScriptProgram scriptProgram{
    "function accept_event(timestamp_ns, can_id, channel, dlc) "
    "  return can_id == 0x123 and channel == 2 and dlc == 3 and timestamp_ns == 1000 "
    "end "
    "function accept_decoded(message_name) return message_name == 'VehicleStatus' end",
    "accept_event"};

  REQUIRE(engine.compile(scriptProgram));
  engine.enable();

  can_core::CanEvent canEvent;
  canEvent.timestampNs = 1000U;
  canEvent.canId = 0x123U;
  canEvent.channel = 2U;
  canEvent.dlc = 3U;

  const auto eventResult = engine.run(can_script_api::ScriptEventView{&canEvent});
  REQUIRE_FALSE(eventResult.hasError());
  CHECK(eventResult.isAccepted);

  can_decode::DecodedMessage decodedMessage;
  decodedMessage.messageName = "VehicleStatus";
  const auto decodedResult = engine.run(can_script_api::ScriptDecodedView{&decodedMessage});
  REQUIRE_FALSE(decodedResult.hasError());
  CHECK(decodedResult.isAccepted);
}

TEST_CASE("can_script_lua maps runtime failures into script errors")
{
  can_script_lua::LuaEngine engine;
  REQUIRE(engine.compile({"function accept_event(timestamp_ns, can_id, channel, dlc) error('boom') end", "accept_event"}));
  engine.enable();

  can_core::CanEvent canEvent;
  const auto result = engine.run(can_script_api::ScriptEventView{&canEvent});

  REQUIRE(result.hasError());
  CHECK(result.errorInfo.code == can_core::ErrorCode::IoFailure);
  CHECK(result.errorInfo.message.find("boom") != std::string::npos);
}
