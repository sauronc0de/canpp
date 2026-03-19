# `can_gui` Detailed Design

## Purpose

Provide the future interactive frontend over the same application services.

## Responsibilities

- manage GUI session state
- bind user interactions to query and navigation requests
- display raw and decoded data
- manage time navigation and filtering UX

## Main Public Types

- `class GuiApplication`
- `class GuiSession`
- `class TraceTableViewModel`
- `class QueryPanelViewModel`
- `class TimelineViewModel`
- `class SignalPanelViewModel`

## Data Types

### `class GuiSession`

State:

- active query draft
- current result page
- selected event or message
- loaded trace metadata

### `struct GuiQueryState`

Fields:

- `QuerySpec querySpec`
- `std::uint64_t visibleStartTimestampNs`
- `std::uint64_t visibleEndTimestampNs`

## Public API

### `class GuiApplication`

- `int run()`
- `void update()`
- `void render()`

### `class TraceTableViewModel`

- `void refresh(const QuerySpec& querySpec)`
- `std::span<const QueryMatch> visibleRows() const`

### `class TimelineViewModel`

- `void zoomIn()`
- `void zoomOut()`
- `void moveToTimestamp(std::uint64_t timestampNs)`

## Main Internal Types

- `class ResultPageCache`
- `class SelectionState`
- `class FilterDraftBuilder`

## Internal Design

The GUI should separate:

- Dear ImGui widgets
- GUI view models
- application service requests

View models own GUI-facing state but never own the core data model itself.

## Performance Notes

- support incremental refresh
- avoid full-table copies for large traces
- use cache and index services for navigation and paging

## Verification

- view-model tests where practical
- smoke tests for main user journeys
- responsiveness checks on large traces
