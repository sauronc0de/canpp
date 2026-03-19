# `can_decode` Detailed Design

## Purpose

Decode `CanEvent` instances using an active database.

## Responsibilities

- signal bit extraction
- signed and unsigned conversion
- floating-point decoding
- scale and offset application
- multiplex selection

## Main Public Types

- `class Decoder`
- `struct DecodedMessageView`
- `struct DecodedSignalView`
- `struct DecodeResult`

## Data Types

### `struct DecodedSignalView`

Fields:

- `std::string_view name`
- `std::variant<std::int64_t, std::uint64_t, float, double> value`
- `std::string_view unit`

### `struct DecodedMessageView`

Fields:

- `std::string_view messageName`
- `std::uint32_t canId`
- `std::span<const DecodedSignalView> decodedSignals`

### `struct DecodeResult`

Fields:

- `bool canDecode`
- `DecodedMessageView decodedMessageView`
- `ErrorInfo errorInfo`

## Public API

### `class Decoder`

- `void setDatabase(const can_dbc::Database* database)`
- `bool canDecode(const can_core::CanEvent& canEvent) const`
- `DecodeResult decode(const can_core::CanEvent& canEvent) const`

## Main Internal Types

- `class BitExtractor`
- `class SignalDecoder`
- `class MultiplexResolver`
- `class DecodePlanCache`

## Internal Design

The decode path is:

1. lookup message definition by CAN ID
2. resolve multiplex state if needed
3. extract raw signal bits
4. convert to typed value
5. apply scale and offset
6. expose decoded views

`DecodePlanCache` stores reusable extraction metadata per message definition to
avoid repeated setup work.

## Performance Notes

- decode only after raw filter acceptance
- reuse decode plans
- avoid building heavyweight decoded objects when view-based output is enough

## Verification

- golden signal decode tests
- endian correctness tests
- IEEE 754 conversion tests
- multiplex behavior tests
