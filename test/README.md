# Node.js Core Tests

This directory contains code and data used to test the Node.js implementation.

For a detailed guide on how to write tests in this
directory, see [the guide on writing tests](../doc/contributing/writing-tests.md).

On how to run tests in this directory, see
[the contributing guide](../doc/contributing/pull-requests.md#step-6-test).

For the tests to run on Windows, be sure to clone Node.js source code with the
`autocrlf` git config flag set to true.

## Test Directories

| Directory        | Runs on CI | Purpose                                                                                                       |
| ---------------- | ---------- | ------------------------------------------------------------------------------------------------------------- |
| `abort`          | Yes        | Tests that use `--abort-on-uncaught-exception` and other cases where we want to avoid generating a core file. |
| `addons`         | Yes        | Tests for [addon][] functionality along with some tests that require an addon.                                |
| `async-hooks`    | Yes        | Tests for [async\_hooks][async_hooks] functionality.                                                          |
| `benchmark`      | Yes        | Test minimal functionality of benchmarks.                                                                     |
| `cctest`         | Yes        | C++ tests that are run as part of the build process.                                                          |
| `code-cache`     | No         | Tests for a Node.js binary compiled with V8 code cache.                                                       |
| `common`         | _N/A_      | Common modules shared among many tests.[^1]                                                                   |
| `doctool`        | Yes        | Tests for the documentation generator.                                                                        |
| `es-module`      | Yes        | Test ESM module loading.                                                                                      |
| `fixtures`       | _N/A_      | Test fixtures used in various tests throughout the test suite.                                                |
| `internet`       | No         | Tests that make real outbound network connections.[^2]                                                        |
| `js-native-api`  | Yes        | Tests for Node.js-agnostic [Node-API][] functionality.                                                        |
| `known_issues`   | Yes        | Tests reproducing known issues within the system.[^3]                                                         |
| `message`        | Yes        | Tests for messages that are output for various conditions                                                     |
| `node-api`       | Yes        | Tests for Node.js-specific [Node-API][] functionality.                                                        |
| `parallel`       | Yes        | Various tests that are able to be run in parallel.                                                            |
| `pseudo-tty`     | Yes        | Tests that require stdin/stdout/stderr to be a TTY.                                                           |
| `pummel`         | No         | Various tests for various modules / system functionality operating under load.                                |
| `sequential`     | Yes        | Various tests that must not run in parallel.                                                                  |
| `testpy`         | _N/A_      | Test configuration utility used by various test suites.                                                       |
| `tick-processor` | No         | Tests for the V8 tick processor integration.[^4]                                                              |
| `v8-updates`     | No         | Tests for V8 performance integration.                                                                         |

[^1]: [Documentation](../test/common/README.md)

[^2]: Tests for networking related modules may also be present in other directories, but those tests do
    not make outbound connections.

[^3]: All tests inside of this directory are expected to fail. If a test doesn't fail on certain platforms,
    those should be skipped via `known_issues.status`.

[^4]: The tests are for the logic in `lib/internal/v8_prof_processor.js` and `lib/internal/v8_prof_polyfill.js`.
    The tests confirm that the profile processor packages the correct set of scripts from V8 and introduces the
    correct platform specific logic.

[Node-API]: https://nodejs.org/api/n-api.html
[addon]: https://nodejs.org/api/addons.html
[async_hooks]: https://nodejs.org/api/async_hooks.html
