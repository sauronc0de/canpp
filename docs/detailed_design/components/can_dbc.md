# `can_dbc` Detailed Design

## Purpose

Load and represent DBC database content.

## Responsibilities

- Parse DBC files
- validate DBC structure
- store messages, signals, and multiplex data
- provide lookup by CAN ID and message name

## Main Public Types

- `class Database`
- `struct MessageDefinition`
- `struct SignalDefinition`
- `struct MultiplexDefinition`
- `class DbcLoader`

## Data Types

### `struct MessageDefinition`

Fields:

- `std::uint32_t canId`
- `std::string name`
- `std::uint8_t dlc`
- `std::vector<SignalDefinition> signalDefinitions`

### `struct SignalDefinition`

Fields:

- `std::string name`
- `std::uint16_t startBit`
- `std::uint16_t bitLength`
- `bool isLittleEndian`
- `bool isSigned`
- `double scale`
- `double offset`

### `struct MultiplexDefinition`

Fields:

- `std::string multiplexorSignalName`
- `std::uint64_t multiplexValue`
- `std::vector<std::string> activeSignalNames`

## Public API

### `class DbcLoader`

- `Database loadFromFile(const std::string& path)`
- `Database loadFromText(std::string_view dbcText)`

### `class Database`

- `const MessageDefinition* findMessageByCanId(std::uint32_t canId) const`
- `const MessageDefinition* findMessageByName(std::string_view name) const`
- `std::span<const MessageDefinition> messageDefinitions() const`
- `bool isEmpty() const`

## Main Internal Types

- `class DbcTokenizer`
- `class DbcParser`
- `class DbcSemanticValidator`

## Internal Design

The subsystem is split into:

- lexical scan
- syntactic parse
- semantic validation
- immutable in-memory model

Lookup structures should include:

- CAN ID to message map
- message name map
- signal lookup by message

## Ownership Rules

- `Database` owns all message and signal definitions
- decoded views refer to database-owned definitions without copying

## Verification

- parser tests
- semantic validation tests
- multiplex modeling tests
- database lookup tests
