# `can_core` Detailed Design

## Purpose

Provide the canonical domain model and stable value types used throughout the
system.

## Responsibilities

- Define `CanEvent`
- Define query model primitives
- Define common IDs, enums, and result types
- Define non-allocating event and view-friendly data structures

## Main Public Types

- `struct CanEvent`
- `struct TraceMetadata`
- `struct ContextRequest`
- `struct QuerySpec`
- `struct MatchReference`
- `enum class FrameType`
- `enum class FilterOperator`
- `enum class LogicalOperator`
- `struct ErrorInfo`

## Data Types

### `struct CanEvent`

Fields:

- `std::uint64_t timestampNs`
- `std::uint32_t canId`
- `std::uint8_t dlc`
- `std::uint8_t channel`
- `FrameType frameType`
- `std::array<std::uint8_t, 64> payload`

### `struct TraceMetadata`

Fields:

- `std::string sourcePath`
- `std::string sourceFormat`
- `std::uint64_t eventCount`
- `std::uint64_t startTimestampNs`
- `std::uint64_t endTimestampNs`

### `struct QuerySpec`

Fields:

- `FilterExpr rawFilter`
- `FilterExpr decodedFilter`
- `ContextRequest contextRequest`
- `bool shouldDecode`
- `bool shouldReturnRaw`

## Public API

Primary API surface:

- `bool isValidCanId(std::uint32_t canId)`
- `bool hasPayload() const`
- `std::span<const std::uint8_t> payloadView() const`
- `bool requiresDecode(const QuerySpec& querySpec)`

## Internal Design

`CanEvent` is a fixed-layout value type holding:

- `timestampNs`
- `canId`
- `dlc`
- `channel`
- `frameType`
- fixed payload storage of 64 bytes

`QuerySpec` contains:

- raw predicate tree
- decoded predicate tree
- result projection settings
- context retrieval settings
- output mode flags

## Ownership Rules

- All core types use value semantics where possible
- `CanEvent` is passed by value or by `const&` depending on context
- chunk containers own event storage
- decoded or exported stages may reference `CanEvent` but do not own it

## Performance Notes

- No dynamic allocation per event
- core types must remain trivially serializable where practical
- avoid virtual dispatch in hot-path domain objects

## Verification

- Layout tests for `CanEvent`
- serialization compatibility tests for core value types
- query model construction tests
