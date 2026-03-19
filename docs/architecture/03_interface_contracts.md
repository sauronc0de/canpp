# Interface Contracts

## 1. Purpose

This document proposes the architecture-level interfaces that separate the core
domain and processing logic from concrete file formats, frontends, and runtime
adapters.

## 2. Core Data Contracts

### 2.1 `CanEvent`

Purpose:

- Canonical representation for every input frame

Required fields:

- Timestamp in nanoseconds
- CAN ID
- DLC
- Payload storage up to 64 bytes
- Bus or channel
- Frame type
- Flags for CAN FD and other frame properties

Contract notes:

- Fixed-size value type
- No per-event heap allocation
- Suitable for chunked processing and cache persistence

### 2.2 `QuerySpec`

Purpose:

- Declarative representation of a user request

Contains:

- Raw filter predicates
- Decode-dependent predicates
- Boolean composition tree
- Context retrieval settings
- Output selection options

### 2.3 `DecodedMessageView` and `DecodedSignalView`

Purpose:

- Read-only views over decoded output

Contract notes:

- Produced only when decoding is requested or required
- Must remain lightweight and presentation-neutral

## 3. Reader Interfaces

### 3.1 `ITraceReader`

Purpose:

- Stream trace data from a concrete source

Required operations:

- `open(source, options)`
- `readChunk(buffer_or_sink)`
- `close()`
- `metadata()`

Contract:

- Produces canonical `CanEvent` chunks
- Must preserve source order
- Must report parse errors with source context where possible

### 3.2 `ITraceReaderFactory`

Purpose:

- Select and construct the correct reader for a source

Required operations:

- `probe(source)`
- `create(source, options)`

Contract:

- Supports automatic format selection
- Keeps reader registration independent of the application layer

## 4. Database and Decode Interfaces

### 4.1 `IDatabaseProvider`

Purpose:

- Provide access to message and signal definitions

Required operations:

- `load(path_or_stream)`
- `lookup(can_id)`
- `metadata()`

### 4.2 `IDecoder`

Purpose:

- Decode a `CanEvent` using an active database

Required operations:

- `decode(event)`
- `canDecode(event)`

Contract:

- Safe when no DBC is loaded
- Supports big-endian and little-endian extraction
- Supports multiplexed messages

## 5. Query Interfaces

### 5.1 `IQueryPlanner`

Purpose:

- Partition a `QuerySpec` into execution stages

Required outputs:

- Raw predicate plan
- Decode stage plan
- Context retrieval plan

### 5.2 `IQueryExecutor`

Purpose:

- Execute a query against a trace source, cache, or indexed dataset

Required operations:

- `execute(query, source, sink)`
- `estimate(query, source)`

Contract:

- Raw predicates must execute before decode-dependent predicates
- Streaming execution is the default mode
- Indexes are optional accelerators, not correctness dependencies

### 5.3 `IContextResolver`

Purpose:

- Retrieve neighboring events around a matched occurrence

Required operations:

- `resolve(match_reference, before_count, after_count)`

Contract:

- Must operate relative to original trace order
- Must remain correct even if the active filtered result set is smaller than
  the original trace

## 6. Cache and Index Interfaces

### 6.1 `ICacheWriter` and `ICacheReader`

Purpose:

- Persist and reload the internal binary cache format

Contract:

- Chunk-based storage
- Stable documented on-disk format
- Random access support via metadata and indexes

### 6.2 `ITraceIndex`

Purpose:

- Accelerate queries and context access

Required capabilities:

- Locate time ranges
- Narrow candidate areas for CAN ID or channel filters
- Map event ordinals to chunk locations

## 7. Export Interfaces

### 7.1 `IExporter`

Purpose:

- Write query output to a target format

Required operations:

- `open(target)`
- `writeRaw(event)`
- `writeDecoded(view)`
- `close()`

Contract:

- Exporters must not own query logic
- CSV is phase 1
- Columnar export is an adapter extension

## 8. Scripting Interfaces

### 8.1 `IScriptEngine`

Purpose:

- Execute user-defined filtering or transformation logic

Required operations:

- `compile(script)`
- `run(event_or_decoded_view)`
- `enable()`
- `disable()`

Contract:

- Optional at runtime
- Sandboxed
- Must not be required for baseline trace processing

## 9. Application Service Interfaces

### 9.1 `ITraceSessionService`

Purpose:

- Manage an analysis session

Responsibilities:

- Open trace
- Attach DBC
- Select cache or index
- Execute query
- Export results

### 9.2 `IBenchmarkService`

Purpose:

- Execute repeatable performance measurements against the core stack

## 10. Frontend Boundary Rules

- CLI interacts only with application services and stable result views
- GUI interacts only with application services and GUI-specific view models
- No frontend toolkit types below the application layer
- No command-line parsing types below the CLI layer

