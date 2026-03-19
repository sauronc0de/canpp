# `can_index` Detailed Design

## Purpose

Provide optional acceleration structures for large datasets.

## Responsibilities

- build indexes from source or cache
- resolve time ranges
- resolve event ordinal positions
- narrow candidate areas by CAN ID and channel

## Main Public Types

- `class IndexBuilder`
- `class TraceIndex`
- `struct ChunkLocation`
- `struct TimeRangeLookup`

## Data Types

### `struct ChunkLocation`

Fields:

- `std::uint64_t chunkId`
- `std::uint64_t fileOffset`
- `std::uint32_t eventOffset`

### `struct TimeRangeLookup`

Fields:

- `std::uint64_t startChunkId`
- `std::uint64_t endChunkId`
- `bool hasMatch`

## Public API

### `class IndexBuilder`

- `TraceIndex buildFromReader(ITraceReader& traceReader)`
- `TraceIndex buildFromCache(const can_cache::CacheReader& cacheReader)`

### `class TraceIndex`

- `TimeRangeLookup findTimeRange(std::uint64_t startTimestampNs, std::uint64_t endTimestampNs) const`
- `std::vector<ChunkLocation> findCanIdCandidates(std::uint32_t canId) const`
- `ChunkLocation findOrdinal(std::uint64_t ordinal) const`

## Main Internal Types

- `class TimestampIndex`
- `class CanIdIndex`
- `class ChannelIndex`
- `class OrdinalIndex`

## Internal Design

Indexes should be organized per chunk, not per event table only. Each index
stores enough metadata to narrow the scan region before full event evaluation.

Initial index scopes:

- timestamp to chunk mapping
- ordinal to chunk offset
- CAN ID to candidate chunk list
- channel to candidate chunk list

## Performance Notes

- index build is optional
- indexes optimize candidate narrowing, not final correctness
- decoded-value indexing is deferred

## Verification

- lookup correctness tests
- skip-efficiency benchmarks
- rebuild and reload consistency tests
