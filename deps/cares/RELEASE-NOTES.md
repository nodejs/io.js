## c-ares version 1.34.2 - October 15 2024

This release contains a fix for downstream packages detecting the c-ares
version based on the contents of the header file rather than the
distributed pkgconf or cmake files.

## c-ares version 1.34.1 - October 9 2024

This release fixes a packaging issue.


## c-ares version 1.34.0 - October 9 2024

This is a feature and bugfix release.

Features:
* adig: read arguments from adigrc.
  [PR #856](https://github.com/c-ares/c-ares/pull/856)
* Add new pending write callback optimization via `ares_set_pending_write_cb`.
  [PR #857](https://github.com/c-ares/c-ares/pull/857)
* New function `ares_process_fds()`.
  [PR #875](https://github.com/c-ares/c-ares/pull/875)
* Failed servers should be probed rather than redirecting queries which could
  cause unexpected latency.
  [PR #877](https://github.com/c-ares/c-ares/pull/877)
* adig: rework command line arguments to mimic dig from bind.
  [PR #890](https://github.com/c-ares/c-ares/pull/890)
* Add new method for overriding network functions
  `ares_set_socket_function_ex()` to properly support all new functionality.
  [PR #894](https://github.com/c-ares/c-ares/pull/894)
* Fix regression with custom socket callbacks due to DNS cookie support.
  [PR #895](https://github.com/c-ares/c-ares/pull/895)
* ares_socket: set IP_BIND_ADDRESS_NO_PORT on ares_set_local_ip* tcp sockets
  [PR #887](https://github.com/c-ares/c-ares/pull/887)
* URI parser/writer for ares_set_servers_csv()/ares_get_servers_csv().
  [PR #882](https://github.com/c-ares/c-ares/pull/882)

Changes:
* Connection handling modularization.
  [PR #857](https://github.com/c-ares/c-ares/pull/857),
  [PR #876](https://github.com/c-ares/c-ares/pull/876)
* Expose library/utility functions to tools.
  [PR #860](https://github.com/c-ares/c-ares/pull/860)
* Remove `ares__` prefix, just use `ares_` for internal functions.
  [PR #872](https://github.com/c-ares/c-ares/pull/872)


Bugfixes:
* fix: potential WIN32_LEAN_AND_MEAN redefinition.
  [PR #869](https://github.com/c-ares/c-ares/pull/869)
* Fix googletest v1.15 compatibility.
  [PR #874](https://github.com/c-ares/c-ares/pull/874)
* Fix pkgconfig thread dependencies.
  [PR #884](https://github.com/c-ares/c-ares/pull/884)


Thanks go to these friendly people for their efforts and contributions for this
release:

* Brad House (@bradh352)
* Cristian Rodríguez (@crrodriguez)
* Georg (@tacerus)
* @lifenjoiner
* Shelley Vohr (@codebytere)
* 前进，前进，进 (@leleliu008)

