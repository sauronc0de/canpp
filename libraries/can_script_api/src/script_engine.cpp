#include "can_script_api/script_engine.hpp"

namespace can_script_api
{
bool DisabledScriptEngine::compile(const ScriptProgram &scriptProgram)
{
  scriptProgram_ = scriptProgram;
  return true;
}

ScriptResult DisabledScriptEngine::run(const ScriptEventView &scriptEventView) const
{
  (void)scriptEventView;
  ScriptResult scriptResult;
  scriptResult.isAccepted = isEnabled_;
  return scriptResult;
}

ScriptResult DisabledScriptEngine::run(const ScriptDecodedView &scriptDecodedView) const
{
  (void)scriptDecodedView;
  ScriptResult scriptResult;
  scriptResult.isAccepted = isEnabled_;
  return scriptResult;
}

void DisabledScriptEngine::enable()
{
  isEnabled_ = true;
}

void DisabledScriptEngine::disable()
{
  isEnabled_ = false;
}

bool DisabledScriptEngine::isEnabled() const
{
  return isEnabled_;
}
} // namespace can_script_api

