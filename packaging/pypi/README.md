# beman-specgen

<!--
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
-->

`specgen` generates C++ standard-library description wording, in the form
[structure.specifications] lays down, from structured headers, and renders it as
draft LaTeX, mpark/wg21 markdown, or org. This package installs the prebuilt
`specgen` command for x86_64 Linux:

```sh
uv tool install beman-specgen      # or: pipx install beman-specgen
specgen --version
specgen generate my_header.hpp
```

It needs glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9 and
their derivatives, or later) and, for any header that includes the standard
library, the host's own `g++`: wording is generated against the standard
library the header's project builds with, so `specgen` does not carry one.
Python is only the installer; the command is a native executable.

The [user guide](https://github.com/steve-downey/specgen/blob/main/docs/user-guide.md)
covers the markup and the commands; the project is
<https://github.com/steve-downey/specgen>.
