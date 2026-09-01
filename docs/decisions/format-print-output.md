# std::format and std::print for formatting; streams only for bulk I/O

## Status

Accepted

## Context

The C++26 baseline (see [cxx26-baseline](cxx26-baseline.md)) makes `<format>`
and `<print>` unconditional on GCC 16 / clang 22–23, so `<<` chains and
`+ std::to_string(...)` concatenations have no remaining justification. What
remains to decide is the exact boundary: where streams keep a job, and what
shape emission takes.

## Decision

Text that interpolates a value is built with `std::format`, and terminal
output is written with `std::print`/`std::println` to a `FILE*`. Streams
survive in one role: moving bytes that are already built (slurping a file or
stdin, writing a finished document to an `ofstream`). Running a rendered
fragment back through `std::print(out, "{}", text)` adds a format scan and
nothing else, so `read_all`'s `buffer << in.rdbuf()` and the `-o` path's
`out << text` stay, and are the only reason `<iostream>`/`<sstream>` remain
anywhere in the tree.

**Emission produces a value.** Do not thread a `std::ostream&` through a
producer so its leaves can `<<` into it — return the text and write it once.
Concretely: `foundation::json_writer`'s scopes and `json_descriptor`'s emit
half take a `std::string&`, and `ir::emit_json` returns the JSON as a
`std::string`. `backend/latex.cpp`'s prolog argues for this shape ("nothing
writes to an ambient `std::ostream` until the very end"); the JSON half
follows it. The sink is a concrete `std::string&` rather than a template
on an output iterator because there is exactly one sink; genericity would add
instantiations and no caller.

## Consequences

- `<ostream>` is absent from `ir.hpp`, `backend/latex.hpp`,
  `foundation/json_writer.hpp` and `foundation/json_descriptor.hpp`;
  `backend::latex::render_to_string` is the backend's whole surface.
- Formatting a `std::size_t` is locale-*independent*: a small correctness
  gain for a frozen on-disk format that locale-sensitive `os << n` quietly
  declines.
- The hand-rolled lowercase hex table in `write_json_string` is `{:02x}`.
  This is the one substitution a golden could not catch on its own (every
  corpus control character is below 0x10, where upper and lower case hex
  coincide), so `json_writer.test.cpp` carries a `0x1f` case that can tell
  `{:02x}` from `{:02X}`. If you touch JSON escaping, only the unit tests
  will catch it.
