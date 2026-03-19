# Performance Design

## 1. Purpose

This document proposes the architecture decisions that directly support the
performance requirements of CAN Trace Explorer.

## 2. Performance Targets

The architecture is designed to support:

- Streaming processing of very large traces
- At least 1 million messages per second on a modern desktop CPU
- No full-trace load requirement for typical queries
- Minimal heap allocation in the event processing hot path

## 3. Hot Paths

The main hot paths are:

- Reader parse and normalization
- Event chunk transfer
- Raw predicate evaluation
- Decode candidate evaluation
- Cache serialization and deserialization
- Indexed candidate narrowing

These paths must remain structurally simple and measurable.

## 4. Proposed Performance Decisions

### 4.1 Fixed-Layout `CanEvent`

Rationale:

- Avoid per-event heap allocation
- Improve cache locality
- Simplify serialization into the internal cache format

### 4.2 Chunk-Based Processing

Rationale:

- Reduce virtual call and function dispatch overhead
- Improve streaming throughput
- Align naturally with cache blocks and index segments

Proposal:

- Readers emit chunks of `CanEvent`
- Query executor consumes spans or block iterators
- Cache stores chunked blocks

### 4.3 Predicate Partitioning

Rationale:

- Raw filters are cheaper than decoded filters
- Most events should be rejected before decode

Proposal:

- Split query predicates at plan time
- Run raw predicates first
- Decode only surviving candidates

### 4.4 Optional Indexes, Never Mandatory

Rationale:

- Correctness must not depend on index availability
- The system must still operate in pure streaming mode

Proposal:

- Executor can accelerate with indexes when present
- Fallback path remains direct streaming scan

### 4.5 Cache as a Performance Optimization Layer

Rationale:

- Repeated work on large traces should avoid reparsing slow source formats
- Chunked binary cache can support both throughput and random access

Proposal:

- Build cache from any reader source
- Reuse cache for repeated analysis
- Keep cache format open and documented

### 4.6 Lightweight Decoded Views

Rationale:

- Avoid building heavyweight decoded objects when only output views are needed

Proposal:

- Produce read-only decoded views or compact decoded records
- Keep original `CanEvent` as the source of truth

## 5. Memory Strategy

The architecture should favor:

- Preallocated chunk buffers
- Reusable decode scratch state
- View-based APIs where ownership allows
- Bounded working sets for streaming queries

The architecture should avoid:

- Per-event heap allocation
- Frontend-managed copies of large result sets by default
- Parser designs that allocate per token on the hot path

## 6. Index Strategy

The initial index should optimize the highest-value access paths:

- Time range narrowing
- Event ordinal to chunk resolution
- CAN ID narrowing
- Channel or bus narrowing

Decoded-value indexing is intentionally deferred because it is more expensive
and should be justified by real workloads.

## 7. Benchmark Strategy

Benchmarks should be provided for:

- Reader throughput per format
- End-to-end raw query throughput
- End-to-end decoded query throughput
- Cache build speed
- Cache-backed query speed
- Index build speed
- Context retrieval latency

## 8. Performance Guardrails

- Any new dependency introduced into the hot path must be justified
- Any new allocation in event processing must be measurable and reviewed
- GUI-specific requirements must not shape phase-1 data structures in ways that
  hurt throughput
- Scripting must remain opt-in and out of the baseline hot path

