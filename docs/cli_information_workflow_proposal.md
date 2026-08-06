# CLI information workflow proposal

## Goal

Make Canpp useful when an engineer knows what they are looking for, but does
not know the exact DBC signal or message names. Every command should support a
small, predictable set of operations:

1. select a resource;
2. optionally list a compact catalog;
3. optionally narrow it with structured predicates or text search;
4. print or plot the result.

The CLI should remain scriptable: normal output is stable text, and commands
must not unexpectedly mutate the trace selection.

## Command vocabulary

| Word | Meaning |
| --- | --- |
| `list` | Compact catalog output: names only, one per line. |
| `grep PATTERN` | Case-sensitive substring search over rendered output. |
| `where EXPR` | Structured filtering over fields, before rendering. |
| `limit N` / `offset N` | Pagination after selection and before rendering. |
| `original` | Use the complete trace instead of the current selection (trace filters only). |
| `plot` | Create graph data from selected, message-qualified DBC signals. |

`grep` is for discovery and should be available on every listing/printing
command. `where` is for reliable scripts and should be preferred when a field
has a defined type. Both may be used together; structured selection happens
first and grep is applied to the final lines.

Suggested future aliases are `search` for `grep` and `find` for `where`, but
only one spelling should be shown in the primary help output.

## DBC catalog commands

The DBC commands inspect the loaded schema and never change trace selection or
history:

```text
dbc status [grep PATTERN]
dbc messages [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
dbc variables [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
dbc message NAME [where EXPR] [grep PATTERN]
dbc variable NAME [where EXPR] [grep PATTERN]
```

Examples:

```text
# Fast discovery: names only
dbc messages list
dbc messages list grep "LS"
dbc variables list grep "speed"

# Full metadata for a known object
dbc message EngineStatus
dbc variable VehicleSpeed

# Search metadata without knowing exact names
dbc messages grep "ADAS"
dbc variables grep "temperature"
```

Behavior:

- `list` prints only names, one per line, with no header. This makes output
  easy to pipe into another command and fast to scan.
- Without `list`, messages include ID, extended status, payload size, and
  signal count. Variables include parent message, ID, bit layout, byte order,
  signedness, scaling, range, unit, multiplexing, and enum values.
- `dbc variable NAME` prints every matching signal. Duplicate signal names
  are valid in DBC; full identity should be displayed in detailed output.
- A future qualified form such as `MESSAGE.SIGNAL` should be added before
  allowing a single duplicate to be selected.
- `dbc status` reports DBC path and message/signal totals.

## Trace print commands

Keep existing trace selection separate from rendering:

```text
print [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
print messages [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
print ids [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
print timestamps [list] [where EXPR] [grep PATTERN] [limit N] [offset N]
print variables NAME... [where EXPR] [grep PATTERN] [limit N] [offset N]
```

Compatibility aliases should remain: singular `message`, `id`,
`timestamp`, and `variable` continue to work.

Recommended meaning of `list` for trace output:

- `print messages list`: message names only, deduplicated in first-seen order;
- `print variables list`: decoded variable names found in the selected trace;
- `print ids list`: IDs only;
- `print timestamps list`: timestamps only.

Without `list`, retain the current tabular output. `grep` filters complete
rendered lines, including detailed output, while `where` filters records using
fields such as `message.name`, `message.id`, `signal.Name`, `time`, and
`record.direction`.

Examples:

```text
print messages list grep "LS"
print messages where message.extended == true grep "ADAS"
print variables list grep "speed"
print variable VehicleSpeed grep "N/A"
print messages limit 50 offset 100
```

`grep` should be case-sensitive by default. Add `grep -i PATTERN` later if
needed; do not silently make searches case-insensitive.

## Filtering model

There are two intentionally different filters:

### Non-mutating output filters

`where` and `grep` affect only the current command's result. They should never
change `selection_`, never enter history, and should be legal on `dbc`, `print`,
and later `plot` catalog commands.

- `where` operates on typed fields and uses the existing query expression
  parser where possible.
- `grep` operates on text after rendering and is useful for quick exploration.
- A malformed filter returns an error and does not print partial output.

### Mutating trace filters

The existing `filter ...` command changes the current trace selection and
remains the right tool for repeated analysis:

```text
filter can name EngineStatus
filter message.extended == true
filter signal.VehicleSpeed > 80 original
reset
```

`print ... where ...` is equivalent to a temporary filter for one command;
`filter ...` is persistent until `reset` or another filter. Help should make
this distinction explicit.

## Plot workflow

Plotting should accept the same discovery and selection concepts instead of
requiring exact names up front:

```text
plot variables list [grep PATTERN]
plot variables NAME [& NAME ...] [source selection|full] [where EXPR]
plot variables grep PATTERN [source selection|full]
```

Recommended workflow:

```text
plot variables list grep "speed"
plot variables VehicleSpeed & EngineSpeed source full
```

Plot requests must use message-qualified signal keys internally, not signal
names alone. The UI/CLI may display `Message.Signal [0xID]`, and duplicate
names must remain independently selectable. `where` applies to trace records;
`grep` applies to the catalog before a plot is created. Plot commands should
not mutate trace selection.

## Consistent output and errors

- Lists: one name per line, no decoration.
- Details: stable labels and deterministic DBC/file order.
- Tables: existing headers remain stable.
- Empty result: print the header for detailed/table output, but no rows;
  lists print nothing and return success.
- Suffixes are order-independent: `list`, `grep PATTERN`, `limit N`, and
  `offset N` may be mixed in any order. Each named suffix may occur once;
  missing values, negative/non-numeric values, duplicate suffixes, and
  positional ranges mixed with named `limit`/`offset` are errors. `list`
  cannot be combined with a named variable selection (for example,
  `print variable Speed list`); use `print variables list` for the catalog.
  Pagination is applied before rendering, then `grep` searches the rendered lines.
- Unknown name: identify the requested name and available command context.
- No DBC: DBC catalog commands report `No DBC database is loaded`.
- No trace: trace print/plot commands report `No communication trace is open`.
- Invalid combinations produce usage help rather than silently interpreting a
  token as a name.

## Help layout

`help` should present a short grammar first, then examples:

```text
Discovery: dbc messages list [grep PATTERN]
           dbc variables list [grep PATTERN]
Details:   dbc message NAME | dbc variable NAME
Trace:     print messages [list] [grep PATTERN] [limit N] [offset N]
Filter:    where EXPR (one command) | filter EXPR (persistent selection)
Plot:      plot variables NAME [& NAME ...] [source selection|full]
```

Readline completion should offer `list`, `grep`, `where`, `limit`, `offset`,
`messages`, and `variables` at the appropriate positions. Completion should
use the loaded DBC catalog where available.

## Implementation phases

### Phase 1: consistent catalogs

1. Add `dbc variables` and `dbc messages` plural commands.
2. Add `list` mode with names-only output.
3. Add a shared command suffix parser for `list`, `grep`, `limit`, and `offset`.
4. Add CLI tests for ordering, empty results, quoted grep patterns, and
   duplicate signal names.

Phase 1 also retains singular `dbc variable NAME` detail output and the
legacy positional `print LIMIT OFFSET` syntax. `dbc variable list` is accepted
as the singular compatibility spelling for the signal catalog.

### Phase 2: typed non-mutating filters

1. Add `where` to DBC message/signal descriptors and trace print commands.
2. Reuse the query parser, separating metadata evaluation from trace-record
   evaluation.
3. Add field documentation and errors for invalid fields.

### Phase 3: plot integration

1. Add catalog search and message-qualified signal selection.
2. Add `plot variables` with current-selection/full-trace source.
3. Reuse the same `grep`/`where` grammar and output naming.

### Phase 4: scale and automation

1. Add machine-readable `format json` output while retaining stable text.
2. Add CSV export for tables and plot series.
3. Add case-insensitive search, regex search, and decimation only as explicit
   options after measuring need.

## Decisions to confirm before implementation

1. Should `list` preserve duplicate names or deduplicate them? The
   recommendation is to preserve DBC entries in details but deduplicate simple
   name lists, with a future `--qualified` option for ambiguity.
2. Should `grep` match headers? The recommendation is yes for literal text
   consistency, with list mode avoiding headers entirely.
3. Should `where` be introduced in the first implementation or after the list
   workflow? The recommendation is to implement list first, then typed `where`
   to avoid duplicating ad-hoc parsers.
4. Should `plot` be a CLI command immediately or remain GUI-only until the
   catalog/filter grammar is stable?
