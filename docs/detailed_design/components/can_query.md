# `can_query` Detailed Design

## Purpose

Plan and execute queries over traces, caches, or indexed sources.

## Responsibilities

- compile `QuerySpec`
- partition predicates
- execute streaming queries
- resolve context around selected matches
- coordinate raw and decoded filtering

## Main Public Types

- `class QueryPlanner`
- `class QueryExecutor`
- `class ContextResolver`
- `struct CompiledQuery`
- `struct QueryExecutionOptions`
- `struct QueryMatch`

## Data Types

### `struct QueryExecutionOptions`

Fields:

- `std::size_t chunkSize`
- `bool shouldUseIndex`
- `bool shouldUseCache`
- `bool shouldStopAtFirstMatch`

### `struct QueryMatch`

Fields:

- `std::uint64_t ordinal`
- `can_core::CanEvent canEvent`
- `bool hasDecodedView`

### `struct CompiledQuery`

Fields:

- raw predicate plan
- decoded predicate plan
- context plan
- projection plan

## Public API

### `class QueryPlanner`

- `CompiledQuery compile(const can_core::QuerySpec& querySpec) const`

### `class QueryExecutor`

- `void execute(const CompiledQuery& compiledQuery, ITraceReader& traceReader, ResultSink& resultSink) const`
- `void execute(const CompiledQuery& compiledQuery, can_cache::CacheReader& cacheReader, ResultSink& resultSink) const`

### `class ContextResolver`

- `ContextWindow resolve(const MatchReference& matchReference, std::size_t beforeCount, std::size_t afterCount) const`

## Main Internal Types

- `class RawPredicateEvaluator`
- `class DecodedPredicateEvaluator`
- `class MatchCollector`
- `class WindowBuffer`
- `class SourceAdapter`

## Internal Design

The executor uses four stages:

1. source adaptation
2. raw predicate evaluation
3. optional decode stage
4. result delivery

Context retrieval uses:

- a `WindowBuffer` for preceding events in pure streaming mode
- source-position lookups for following events
- index assistance when available

## Ownership Rules

- query plan owns predicate objects
- source adapter owns traversal state
- result sinks own persisted outputs

## Performance Notes

- raw predicates must be evaluated before decode
- chunk iteration is the default execution model
- query execution must work without full materialization of the result set

## Verification

- boolean predicate tests
- streaming correctness tests
- context retrieval tests
- index-assisted path tests
