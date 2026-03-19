# `can_readers_binary` Detailed Design

## Purpose

Provide binary reader implementations for extended formats.

## Responsibilities

- Parse BLF
- Parse MDF4/MF4
- Parse TRC

## Main Internal Types

- `class BlfReader`
- `class Mf4Reader`
- `class TrcReader`
- `class BinaryCursor`
- `class ChunkDecoder`

## Data Types

### `class BinaryCursor`

State:

- current byte position
- buffer pointer
- buffer size

### `struct BinaryRecordHeader`

Fields:

- `std::uint32_t recordType`
- `std::uint32_t recordSize`
- `std::uint64_t timestampNs`

## Public API

Reader classes implement `ITraceReader`:

- `bool open(const SourceDescriptor& sourceDescriptor, const ReaderOptions& readerOptions) override`
- `ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) override`
- `TraceMetadata metadata() const override`
- `void close() override`

## Internal Design

Binary readers should separate:

- file container navigation
- record decoding
- normalization into `CanEvent`

Shared helpers should provide:

- endian-aware reads
- bounds-safe cursor operations
- timestamp conversion
- record-type dispatch

## Performance Notes

- use buffered block reads
- decode into preallocated event chunks
- isolate complex format parsing from the query path

## Verification

- sample corpus tests
- corrupted-block tests
- throughput comparison against cached replay
