# `can_readers_text` Detailed Design

## Purpose

Provide initial text-based reader implementations.

## Responsibilities

- Parse candump/log
- Parse CSV traces
- Parse ASC traces

## Main Internal Types

- `class CandumpReader`
- `class CsvTraceReader`
- `class AscTraceReader`
- `class TextLineReader`
- `struct ParsedTextRecord`

## Data Types

### `struct ParsedTextRecord`

Fields:

- `std::string_view timestampToken`
- `std::string_view canIdToken`
- `std::string_view channelToken`
- `std::string_view payloadToken`
- `FrameType frameType`

## Public API

Reader classes implement `ITraceReader`:

- `bool open(const SourceDescriptor& sourceDescriptor, const ReaderOptions& readerOptions) override`
- `ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) override`
- `TraceMetadata metadata() const override`
- `void close() override`

## Internal Design

Each reader follows this internal flow:

1. Read raw line
2. Tokenize minimal fields
3. Validate required columns
4. Normalize into `CanEvent`
5. Append into output chunk

Shared helpers handle:

- line buffering
- numeric parsing
- timestamp normalization
- payload parsing

## Performance Notes

- Prefer streaming line parsing
- reuse token buffers
- avoid per-field string allocation when possible

## Verification

- golden file tests per format
- malformed-input tests
- throughput benchmarks on large text traces
