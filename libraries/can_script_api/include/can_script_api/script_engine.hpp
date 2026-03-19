#pragma once

#include <string>

#include "can_core/core_types.hpp"
#include "can_decode/decoder.hpp"

namespace can_script_api
{
struct ScriptProgram
{
  std::string sourceText;
  std::string entryFunctionName = "accept_event";
};

struct ScriptEventView
{
  const can_core::CanEvent *canEvent = nullptr;
};

struct ScriptDecodedView
{
  const can_decode::DecodedMessage *decodedMessage = nullptr;
};

struct ScriptResult
{
  bool isAccepted = true;
  bool hasTransformedOutput = false;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class ScriptEngine
{
public:
  virtual ~ScriptEngine() = default;

  virtual bool compile(const ScriptProgram &scriptProgram) = 0;
  [[nodiscard]] virtual ScriptResult run(const ScriptEventView &scriptEventView) const = 0;
  [[nodiscard]] virtual ScriptResult run(const ScriptDecodedView &scriptDecodedView) const = 0;
  virtual void enable() = 0;
  virtual void disable() = 0;
  [[nodiscard]] virtual bool isEnabled() const = 0;
};

class DisabledScriptEngine : public ScriptEngine
{
public:
  bool compile(const ScriptProgram &scriptProgram) override;
  [[nodiscard]] ScriptResult run(const ScriptEventView &scriptEventView) const override;
  [[nodiscard]] ScriptResult run(const ScriptDecodedView &scriptDecodedView) const override;
  void enable() override;
  void disable() override;
  [[nodiscard]] bool isEnabled() const override;

private:
  bool isEnabled_ = false;
  ScriptProgram scriptProgram_;
};
} // namespace can_script_api

