# Traceability Matrix

## 1. Purpose

This matrix links requirements to the proposed architecture elements.

## 2. Matrix

| Requirement ID | Architectural Response |
|---|---|
| `SYS-REQ-001` | Entire architecture is defined around modern C++ libraries and applications. |
| `SYS-REQ-002` | File formats, export formats, and scripting runtimes are isolated behind adapters; no proprietary SDKs in the core path. |
| `SYS-REQ-003` | Streaming query execution, chunk processing, optional cache, and optional indexing support large traces. |
| `SYS-REQ-004` | `CanEvent` supports payloads up to 64 bytes and frame-type metadata. |
| `SYS-REQ-005` | Core modules are platform-neutral; portability constraints are enforced at layer boundaries. |
| `SYS-REQ-006` | Module split into core, readers, decode, query, index, cache, export, app, CLI, GUI. |
| `SYS-REQ-007` | `can_app` and core contracts define the public integration surface. |
| `DATA-REQ-001` to `DATA-REQ-005` | `can_core` defines a canonical fixed-layout `CanEvent` and related types. |
| `IO-REQ-001` to `IO-REQ-003` | `can_reader_api` and reader factories provide a common plugin-style reader contract. |
| `IO-REQ-010` to `IO-REQ-012` | `can_readers_text` is phase-1 scope. |
| `IO-REQ-020` to `IO-REQ-022` | `can_readers_binary` is isolated for later phase growth. |
| `IO-REQ-030` to `IO-REQ-032` | `can_export` provides CSV first and columnar export later through adapters. |
| `DBC-REQ-001` to `DBC-REQ-009` | `can_dbc` plus `can_decode` provide database loading, mapping, and decoding with no-DBC fallback. |
| `QRY-REQ-001` to `QRY-REQ-012` | `can_query` owns filter planning, boolean composition, and raw-filter-first execution. |
| `QRY-REQ-013` to `QRY-REQ-017` | `can_query` plus `can_index` and `can_cache` support context retrieval against original trace order. |
| `PERF-REQ-001` to `PERF-REQ-006` | Chunk-based streaming, optional indexing, low-allocation event model, and cache-backed processing. |
| `CACHE-REQ-001` to `CACHE-REQ-005` | `can_cache` defines an open chunked binary cache with random-access support. |
| `CLI-REQ-001` to `CLI-REQ-005` | `can_cli` uses `can_app` as the first integration and validation frontend. |
| `GUI-REQ-001` to `GUI-REQ-021` | `can_gui` is planned on top of `can_app`, with no GUI dependencies in the core. |
| `EXT-REQ-001` to `EXT-REQ-010` | `can_script_api` and `can_script_lua` provide optional sandboxed scripting outside the baseline core path. |
| `NFR-PORT-001` to `NFR-PORT-002` | Architecture isolates platform-specific code and keeps core libraries portable. |
| `NFR-MAINT-001` to `NFR-MAINT-003` | Library boundaries, explicit interfaces, and documented public APIs support maintainability. |
| `NFR-TEST-001` | Benchmarking is a first-class responsibility in `can_app` and the benchmark suite. |
| `CON-REQ-001` to `CON-REQ-002` | Open formats and vendor-independent adapters are architectural constraints. |
| `FUT-REQ-001` to `FUT-REQ-003` | Additional database formats, live CAN, and distributed processing can be added via new adapters or services. |

