# specgen

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

`specgen` generates C++ standard-library description wording, in the form
[structure.specifications] lays down, from structured headers. This archive is a
prebuilt driver for x86_64 Linux; the project, its documentation and its source
are at <https://github.com/steve-downey/specgen>.

## Running it

Unpack the archive anywhere and run `bin/specgen`, or put `bin/` on your
`PATH`. Keep the directory together: `lib/clang/` holds Clang's builtin headers
(`<stddef.h>`, `<stdarg.h>`, ...), which the driver finds beside itself.

It needs:

- **glibc 2.34 or newer** (Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9 and their
  derivatives, or later) and an x86-64-v2 processor.
- **A C++ standard library**, the host's own: install `g++` (or your
  distribution's libstdc++ development package). Wording is generated against
  the same standard library the header's project builds with, so `specgen`
  does not carry one of its own.

```sh
bin/specgen --version
bin/specgen generate my_header.hpp
```

## If it crashes

The crash report names the version and commit and prints a stack trace of
offsets. The release that this archive came from also carries
`specgen.debug.xz`, the driver's debug information, which resolves them:

```sh
unxz specgen.debug.xz
llvm-symbolizer --obj=specgen.debug <offset>
```

## License

Apache License 2.0 with LLVM Exceptions; see `LICENSE`. The driver links LLVM
and Clang, which are under the same licence.
