## c-ares version 1.34.4 - December 14 2024

This is a bugfix release.

Changes:
* QNX Port: Port to QNX 8, add primary config reading support, add CI build. [PR #934](https://github.com/c-ares/c-ares/pull/934), [PR #937](https://github.com/c-ares/c-ares/pull/937), [PR #938](https://github.com/c-ares/c-ares/pull/938)

Bugfixes:
* Empty TXT records were not being preserved. [PR #922](https://github.com/c-ares/c-ares/pull/922)
* docs: update deprecation notices for `ares_create_query()` and `ares_mkquery()`. [PR #910](https://github.com/c-ares/c-ares/pull/910)
* license: some files weren't properly updated. [PR #920](https://github.com/c-ares/c-ares/pull/920)
* Fix bind local device regression from 1.34.0. [PR #929](https://github.com/c-ares/c-ares/pull/929), [PR #931](https://github.com/c-ares/c-ares/pull/931), [PR #935](https://github.com/c-ares/c-ares/pull/935)
* CMake: set policy version to prevent deprecation warnings. [PR #932](https://github.com/c-ares/c-ares/pull/932)
* CMake: shared and static library names should be the same on unix platforms like autotools uses. [PR #933](https://github.com/c-ares/c-ares/pull/933)
* Update to latest autoconf archive macros for enhanced system compatibility. [PR #936](https://github.com/c-ares/c-ares/pull/936)

Thanks go to these friendly people for their efforts and contributions for this
release:

* Brad House (@bradh352)
* Daniel Stenberg (@bagder)
* Gregor Jasny (@gjasny)
* @marcovsz
* Nikolaos Chatzikonstantinou (@createyourpersonalaccount)
* @vlasovsoft1979
