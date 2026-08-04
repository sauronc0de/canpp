# Query language

The trace CLI accepts `filter <expression>` and `filter range <event-expression>`.
Expressions are parsed before the current selection is changed. Records are
visited in source-record order.

References are:

- `signal.<Name>` (decoded DBC physical value)
- `message.name`, `message.id`, `message.extended`
- `record.stream_id`, `record.protocol`, `record.direction`
- `timestamp_ns` and `time` (`time` is `timestamp_ns` converted to seconds)

Numbers, quoted strings (`'` or `"`, with basic backslash escapes), and
`true`/`false` are supported. Operators are `==`, `!=`, `<`, `<=`, `>`, `>=`,
`&&`, `||`, and `!`. Precedence is `!`, comparisons, `&&`, then `||`;
parentheses can override it. Protocol and direction references compare as
strings such as `"can"`, `"rx"`, and `"tx"`.

`filter range E` selects records strictly between each successive pair of
records matching event expression `E`; event records themselves are excluded.
Without `original`, the current selection is used as the source and therefore
also supplies the selection intersection. With `original`, the complete trace
is used. A range with fewer than two matching events is empty. The optional
`original` suffix is also supported by ordinary expression filters.
