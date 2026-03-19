# `can_script_api` Detailed Design

## Purpose

Define a stable scripting abstraction for optional custom processing.

## Responsibilities

- define script engine contract
- define script-facing data views
- define execution results and sandbox constraints

## Main Public Types

- `class ScriptEngine`
- `struct ScriptProgram`
- `struct ScriptContext`
- `struct ScriptResult`
- `struct ScriptEventView`
- `struct ScriptDecodedView`

## Data Types

### `struct ScriptProgram`

Fields:

- `std::string sourceText`
- `std::string entryFunctionName`

### `struct ScriptResult`

Fields:

- `bool isAccepted`
- `bool hasTransformedOutput`
- `ErrorInfo errorInfo`

### `struct ScriptEventView`

Fields:

- `const can_core::CanEvent* canEvent`

## Public API

### `class ScriptEngine`

- `bool compile(const ScriptProgram& scriptProgram)`
- `ScriptResult run(const ScriptEventView& scriptEventView) const`
- `ScriptResult run(const ScriptDecodedView& scriptDecodedView) const`
- `void enable()`
- `void disable()`
- `bool isEnabled() const`

## Internal Design

The API separates:

- script compilation
- script execution
- runtime enable or disable state
- data marshalling boundary

Core modules interact only with this API, never with Lua directly.

## Verification

- contract tests using mock engines
- enable and disable behavior tests
- data view mapping tests
