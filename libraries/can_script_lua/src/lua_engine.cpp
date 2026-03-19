#include "can_script_lua/lua_engine.hpp"

extern "C"
{
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace can_script_lua
{
LuaEngine::LuaEngine()
{
  resetState();
}

LuaEngine::~LuaEngine()
{
  if(state_ != nullptr)
  {
    lua_close(state_);
  }
}

bool LuaEngine::compile(const can_script_api::ScriptProgram &scriptProgram)
{
  scriptProgram_ = scriptProgram;
  resetState();
  if(state_ == nullptr)
  {
    return false;
  }

  if(luaL_dostring(state_, scriptProgram.sourceText.c_str()) != LUA_OK)
  {
    resetState();
    return false;
  }

  return true;
}

can_script_api::ScriptResult LuaEngine::run(const can_script_api::ScriptEventView &scriptEventView) const
{
  if(!isEnabled_)
  {
    return {};
  }

  if(state_ == nullptr || scriptEventView.canEvent == nullptr)
  {
    return makeRunError("Lua engine is not initialized");
  }

  lua_getglobal(state_, scriptProgram_.entryFunctionName.c_str());
  if(!lua_isfunction(state_, -1))
  {
    lua_pop(state_, 1);
    return {};
  }

  lua_pushinteger(state_, static_cast<lua_Integer>(scriptEventView.canEvent->timestampNs));
  lua_pushinteger(state_, static_cast<lua_Integer>(scriptEventView.canEvent->canId));
  lua_pushinteger(state_, static_cast<lua_Integer>(scriptEventView.canEvent->channel));
  lua_pushinteger(state_, static_cast<lua_Integer>(scriptEventView.canEvent->dlc));
  if(lua_pcall(state_, 4, 1, 0) != LUA_OK)
  {
    return makeRunError(lua_tostring(state_, -1));
  }

  can_script_api::ScriptResult scriptResult;
  if(lua_isboolean(state_, -1))
  {
    scriptResult.isAccepted = lua_toboolean(state_, -1) != 0;
  }
  lua_pop(state_, 1);
  return scriptResult;
}

can_script_api::ScriptResult LuaEngine::run(const can_script_api::ScriptDecodedView &scriptDecodedView) const
{
  if(!isEnabled_)
  {
    return {};
  }

  if(state_ == nullptr || scriptDecodedView.decodedMessage == nullptr)
  {
    return makeRunError("Lua engine is not initialized");
  }

  lua_getglobal(state_, "accept_decoded");
  if(!lua_isfunction(state_, -1))
  {
    lua_pop(state_, 1);
    return {};
  }

  lua_pushstring(state_, std::string(scriptDecodedView.decodedMessage->messageName).c_str());
  if(lua_pcall(state_, 1, 1, 0) != LUA_OK)
  {
    return makeRunError(lua_tostring(state_, -1));
  }

  can_script_api::ScriptResult scriptResult;
  if(lua_isboolean(state_, -1))
  {
    scriptResult.isAccepted = lua_toboolean(state_, -1) != 0;
  }
  lua_pop(state_, 1);
  return scriptResult;
}

void LuaEngine::enable()
{
  isEnabled_ = true;
}

void LuaEngine::disable()
{
  isEnabled_ = false;
}

bool LuaEngine::isEnabled() const
{
  return isEnabled_;
}

void LuaEngine::resetState()
{
  if(state_ != nullptr)
  {
    lua_close(state_);
  }

  state_ = luaL_newstate();
  if(state_ != nullptr)
  {
    luaL_openlibs(state_);
  }
}

can_script_api::ScriptResult LuaEngine::makeRunError(const char *message) const
{
  can_script_api::ScriptResult scriptResult;
  scriptResult.errorInfo.code = can_core::ErrorCode::IoFailure;
  scriptResult.errorInfo.message = message != nullptr ? message : "Lua execution error";
  if(state_ != nullptr)
  {
    lua_pop(state_, 1);
  }
  return scriptResult;
}
} // namespace can_script_lua

