## c-ares version 1.33.0 - August 2 2024

This is a feature and bugfix release.

Features:
* Add DNS cookie support (RFC7873 + RFC9018) to help prevent off-path cache
  poisoning attacks. [PR #833](https://github.com/c-ares/c-ares/pull/833)
* Implement TCP FastOpen (TFO) RFC7413, which will make TCP reconnects 0-RTT
  on supported systems. [PR #840](https://github.com/c-ares/c-ares/pull/840)

Changes:
* Reorganize source tree. [PR #822](https://github.com/c-ares/c-ares/pull/822)
* Refactoring of connection handling to prevent code duplication.
  [PR #839](https://github.com/c-ares/c-ares/pull/839)
* New dynamic array data structure to prevent simple logic flaws in array
  handling in various code paths.
  [PR #841](https://github.com/c-ares/c-ares/pull/841)

Bugfixes:
* `ares_destroy()` race condition during shutdown due to missing lock.
  [PR #831](https://github.com/c-ares/c-ares/pull/831)
* Android: Preserve thread name after attaching it to JVM.
  [PR #838](https://github.com/c-ares/c-ares/pull/838)
* Windows UWP (Store) support fix.
  [PR #845](https://github.com/c-ares/c-ares/pull/845)


Thanks go to these friendly people for their efforts and contributions for this
release:

* Brad House (@bradh352)
* Yauheni Khnykin (@Hsilgos)

