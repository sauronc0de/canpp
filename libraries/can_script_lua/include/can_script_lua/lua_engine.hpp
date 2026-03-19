#pragma once

#include <memory>

#include "can_script_api/script_engine.hpp"

struct lua_State;

namespace can_script_lua
{
class LuaEngine : public can_script_api::ScriptEngine
{
public:
  LuaEngine();
  ~LuaEngine() override;

  bool compile(const can_script_api::ScriptProgram &scriptProgram) override;
  [[nodiscard]] can_script_api::ScriptResult run(const can_script_api::ScriptEventView &scriptEventView) const override;
  [[nodiscard]] can_script_api::ScriptResult run(const can_script_api::ScriptDecodedView &scriptDecodedView) const override;
  void enable() override;
  void disable() override;
  [[nodiscard]] bool isEnabled() const override;

private:
  void resetState();
  [[nodiscard]] can_script_api::ScriptResult makeRunError(const char *message) const;

  lua_State *state_ = nullptr;
  bool isEnabled_ = false;
  can_script_api::ScriptProgram scriptProgram_;
};
} // namespace can_script_lua

