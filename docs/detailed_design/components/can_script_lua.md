# `can_script_lua` Detailed Design

## Purpose

Implement the scripting API using Lua.

## Responsibilities

- compile Lua scripts
- enforce sandbox restrictions
- map C++ views to Lua values
- return results through `can_script_api`

## Main Public Types

- `class LuaEngine`
- `class LuaProgram`

## Data Types

### `class LuaProgram`

State:

- compiled chunk handle
- script metadata
- exported function bindings

## Public API

### `class LuaEngine`

- `bool compile(const ScriptProgram& scriptProgram) override`
- `ScriptResult run(const ScriptEventView& scriptEventView) const override`
- `ScriptResult run(const ScriptDecodedView& scriptDecodedView) const override`
- `void enable() override`
- `void disable() override`

## Main Internal Types

- `class LuaSandbox`
- `class LuaValueBridge`
- `class LuaErrorMapper`

## Internal Design

The Lua adapter manages:

- Lua state creation
- approved library exposure
- script compilation cache
- input value marshaling
- error capture and translation

## Performance Notes

- remain fully optional
- do not sit in the baseline hot path unless enabled
- prefer reusable Lua states where safe

## Verification

- sandbox restriction tests
- script execution tests
- failure isolation tests
