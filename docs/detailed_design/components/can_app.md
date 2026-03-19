# `can_app` Detailed Design

## Purpose

Provide application-level use cases shared by CLI and GUI.

## Responsibilities

- manage analysis sessions
- coordinate readers, DBC, cache, indexes, and queries
- expose stable use-case APIs
- orchestrate export and benchmark operations

## Main Public Types

- `class TraceSessionService`
- `class BenchmarkService`
- `class TraceSession`
- `struct SessionOptions`
- `struct OpenTraceRequest`
- `struct ExecuteQueryRequest`

## Data Types

### `struct SessionOptions`

Fields:

- `std::size_t queryChunkSize`
- `bool shouldUseCache`
- `bool shouldUseIndex`
- `bool shouldEnableScripting`

### `struct OpenTraceRequest`

Fields:

- `std::string tracePath`
- `ReaderOptions readerOptions`

### `struct ExecuteQueryRequest`

Fields:

- `QuerySpec querySpec`
- `QueryExecutionOptions queryExecutionOptions`

## Public API

### `class TraceSession`

- `bool hasOpenTrace() const`
- `bool hasDatabase() const`
- `const TraceMetadata& traceMetadata() const`

### `class TraceSessionService`

- `bool openTrace(const OpenTraceRequest& openTraceRequest)`
- `bool loadDatabase(const std::string& dbcPath)`
- `void executeQuery(const ExecuteQueryRequest& executeQueryRequest, ResultSink& resultSink)`
- `void exportResults(const ExecuteQueryRequest& executeQueryRequest, const ExportRequest& exportRequest)`
- `ContextWindow getContext(const MatchReference& matchReference, std::size_t beforeCount, std::size_t afterCount)`

### `class BenchmarkService`

- `BenchmarkReport runReaderBenchmark(const OpenTraceRequest& openTraceRequest)`
- `BenchmarkReport runQueryBenchmark(const ExecuteQueryRequest& executeQueryRequest)`

## Main Internal Types

- `class SourceSelector`
- `class QueryUseCase`
- `class ExportUseCase`
- `class CacheBuildUseCase`
- `class BenchmarkScenarioRunner`

## Internal Design

`TraceSession` owns:

- active source
- optional loaded database
- optional cache handle
- optional index handle
- session-level configuration

`TraceSessionService` coordinates user-visible operations and delegates all
core work to lower layers.

## Ownership Rules

- session owns handles to opened resources
- lower layers own their internal state
- frontend receives view or result objects, not mutable core internals

## Verification

- integration tests for session workflows
- end-to-end query and export tests
- benchmark command tests
