# `can_cache` Detailed Design

## Purpose

Persist normalized events in an internal binary cache format.

## Responsibilities

- write chunked event cache
- read cached events
- store metadata and directory information
- support random access

## Main Public Types

- `class CacheWriter`
- `class CacheReader`
- `struct CacheFileHeader`
- `struct CacheChunkHeader`
- `struct CacheDirectoryEntry`

## Data Types

### `struct CacheFileHeader`

Fields:

- `std::uint32_t magic`
- `std::uint16_t majorVersion`
- `std::uint16_t minorVersion`
- `std::uint64_t chunkCount`

### `struct CacheChunkHeader`

Fields:

- `std::uint64_t chunkId`
- `std::uint32_t eventCount`
- `std::uint64_t firstTimestampNs`
- `std::uint64_t lastTimestampNs`

### `struct CacheDirectoryEntry`

Fields:

- `std::uint64_t chunkId`
- `std::uint64_t fileOffset`
- `std::uint64_t byteSize`

## Public API

### `class CacheWriter`

- `bool open(const std::string& path)`
- `void writeChunk(std::span<const can_core::CanEvent> chunk)`
- `void close()`

### `class CacheReader`

- `bool open(const std::string& path)`
- `std::size_t readChunk(std::uint64_t chunkId, std::span<can_core::CanEvent> outputBuffer) const`
- `TraceMetadata metadata() const`
- `void close()`

## Main Internal Types

- `class ChunkSerializer`
- `class ChunkDeserializer`
- `class MetadataWriter`

## Internal Design

The cache file should contain:

- file header
- chunk directory
- chunk payload blocks
- optional index references

Chunk payloads store contiguous `CanEvent` records. Directory entries store:

- chunk offset
- chunk size
- event count
- first and last timestamps
- optional CAN ID summary

## Performance Notes

- binary cache is optimized for replay and query reuse
- random access should avoid reparsing source formats
- header and directory reads should be small and predictable

## Verification

- read/write round-trip tests
- random access tests
- cross-version format guard tests
