# `can_reader_api` Detailed Design

## Purpose

Define the stable contracts for reading traces from any supported source.

## Responsibilities

- Reader interface definition
- reader factory contract
- capability reporting
- source descriptor and options definition

## Main Public Types

- `class ITraceReader`
- `class ITraceReaderFactory`
- `struct ReaderOptions`
- `struct ReaderCapabilities`
- `struct SourceDescriptor`
- `struct ReadResult`

## Data Types

### `struct SourceDescriptor`

Fields:

- `std::string path`
- `std::string extension`
- `bool isFile`

### `struct ReaderOptions`

Fields:

- `std::size_t chunkSize`
- `bool shouldValidateStrictly`
- `bool shouldNormalizeTimestamps`

### `struct ReadResult`

Fields:

- `std::size_t eventCount`
- `bool isEndOfStream`
- `ErrorInfo errorInfo`

## Public API

### `class ITraceReader`

Required operations:

- `virtual bool open(const SourceDescriptor& sourceDescriptor, const ReaderOptions& readerOptions) = 0`
- `virtual ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) = 0`
- `virtual TraceMetadata metadata() const = 0`
- `virtual void close() = 0`

### `class ITraceReaderFactory`

Required operations:

- `virtual bool canOpen(const SourceDescriptor& sourceDescriptor) const = 0`
- `virtual std::unique_ptr<ITraceReader> create() const = 0`
- `virtual std::string formatName() const = 0`

## Internal Design

`ITraceReader` is chunk-oriented and exposes:

- `open()`
- `readChunk()`
- `metadata()`
- `close()`

`readChunk()` should fill a caller-supplied buffer or produce a non-owning
chunk view to avoid uncontrolled allocation patterns.

`ITraceReaderFactory` supports:

- format probing
- reader creation
- capability lookup

## Ownership Rules

- Reader instances own format-specific parsing state
- Caller owns source selection and reader lifecycle
- Chunk buffers are owned by the executor or session layer, not by the reader

## Verification

- contract tests for all reader implementations
- probe-selection tests
- order-preservation tests
