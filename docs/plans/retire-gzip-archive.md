# Retire the gzip release archive (#164)

The xz archive is the documented download. The compatibility gzip archive can
be retired after checking the known consumers for links to its asset name.

## Consumer audit, 2026-11-05

GitHub code searches for the exact specgen gzip asset name under `bemanproject`
and `steve-downey` found only specgen's own publishing code and documentation.
The known consumers were also checked across their available branches:

| Consumer | Branches checked | Files mentioning specgen | Result |
| --- | ---: | ---: | --- |
| expected | 29 local branches | 14 | Installed executable; no archive link |
| movable_box | 3 local branches | 21 | Installed executable or source build; no archive link |
| transcode | 12 local branches | 179 | Pinned source build; no specgen archive link |
| transpose | 27 branches from a fresh upstream clone | 103 | Installed executable or source build; no archive link |

The search covered workflows, makefiles, CMake, scripts, paper sources, and
user documentation. Gzip links in transcode's workflows download gitleaks.
No known consumer requires the compatibility specgen asset. Previously
published release assets remain available.

## Change

Publish the xz archive, its checksum, the debug information, and the wheel.
Smoke-test the checked xz tree. Staging also clears obsolete gzip files from a
reused output directory so a broad release upload cannot pick them up.
