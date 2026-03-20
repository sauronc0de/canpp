#include <doctest/doctest.h>

#include "can_script_api/script_engine.hpp"

TEST_CASE("can_script_api disabled engine exposes enable-disable contract for event and decoded views")
{
  can_script_api::DisabledScriptEngine engine;
  const can_script_api::ScriptProgram scriptProgram{"return true", "accept_event"};

  REQUIRE(engine.compile(scriptProgram));
  CHECK_FALSE(engine.isEnabled());

  can_core::CanEvent canEvent;
  can_decode::DecodedMessage decodedMessage;

  const auto disabledEventResult = engine.run(can_script_api::ScriptEventView{&canEvent});
  const auto disabledDecodedResult = engine.run(can_script_api::ScriptDecodedView{&decodedMessage});
  CHECK_FALSE(disabledEventResult.isAccepted);
  CHECK_FALSE(disabledDecodedResult.isAccepted);
  CHECK_FALSE(disabledEventResult.hasError());

  engine.enable();
  CHECK(engine.isEnabled());
  CHECK(engine.run(can_script_api::ScriptEventView{&canEvent}).isAccepted);
  CHECK(engine.run(can_script_api::ScriptDecodedView{&decodedMessage}).isAccepted);

  engine.disable();
  CHECK_FALSE(engine.isEnabled());
  CHECK_FALSE(engine.run(can_script_api::ScriptEventView{&canEvent}).isAccepted);
}
