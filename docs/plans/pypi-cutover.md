<!-- markdownlint-disable MD013 -->

# PyPI cutover and downstream pins

Issue [#161](https://github.com/steve-downey/specgen/issues/161) remains open
until the package installs from production and the consuming projects use it.
The pending publisher is reported configured in the issue; its account-side
configuration still needs verification. This document prepares the cutover.

## Release gates

1. Merge the current issue-fix stack in dependency order. Require green CI on
   each final revision, including the portable archive and wheel checks in
   `release-binary.yml`. #148 and #149 are already closed.
2. Select the latest merged version and its exact commit. Check the mainline
   release run's archive, wheel, and five-distribution smoke jobs. Use that
   run's existing wheel; rebuilding from a local development build does not
   establish the manylinux compatibility promise.
3. On pypi.org, verify the pending publisher names project `beman-specgen`,
   owner `steve-downey`, repository `specgen`, workflow `release-binary.yml`,
   and environment `pypi`. Create the GitHub environment `pypi` and configure
   protection rules before changing `PYPI_PUBLISH`.
4. Set `PYPI_PUBLISH=pypi`. Re-run only the selected latest mainline run's
   **Publish the wheel** job, or publish through the next deliberately
   version-bumped mainline push. Re-running all jobs also re-creates the
   GitHub Release tag and fails. Keep the chosen version and commit in the
   release record. Check that the uploaded wheel is that version.

## Verify the production install

Use a fresh temporary tool directory and the chosen version, replacing
`<version>` with the version selected above:

```sh
export UV_TOOL_DIR="$(mktemp -d)"
export UV_TOOL_BIN_DIR="$UV_TOOL_DIR/bin"
uv tool install --default-index https://pypi.org/simple 'beman-specgen==<version>'
"$UV_TOOL_BIN_DIR/specgen" --version
"$UV_TOOL_BIN_DIR/specgen" generate tests/corpus/spec_rsec_wrapped.hpp > /tmp/specgen-direct.tex
"$UV_TOOL_BIN_DIR/specgen" generate --emit-ir tests/corpus/spec_rsec_wrapped.hpp > /tmp/specgen-installed.json
"$UV_TOOL_BIN_DIR/specgen" render --from-ir /tmp/specgen-installed.json > /tmp/specgen-saved.tex
cmp /tmp/specgen-direct.tex /tmp/specgen-saved.tex
```

Check the reported version and Git commit against the release record. Exercise
`uvx --from 'beman-specgen==<version>' specgen --version` as well. Restore the
README and user-guide installation instructions to the verified production
commands, and update the publishing status in `docs/building.md`.

## Downstream PRs

Migrate `beman.transpose` first, then each other consuming project in its own
branch and worktree. Pin the verified production version in its uv tool or
Python development dependencies; remove the old source-build or TestPyPI path
only after generation works with that pin. Regenerate wording, compare saved
IR with single-pass rendering, and run each project's own checks. For
`beman.expected` and `beman.constexpr_callables`, review the resulting fragments
against the draft as well as checking for validation findings.

Record each migration PR in #161. Close the tracker after the production
install, documentation update, and agreed downstream migrations have all
completed. A prepared stack or a TestPyPI rehearsal alone does not meet those
gates.
