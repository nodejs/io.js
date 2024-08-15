# Node.js Core Test Common Modules

This directory contains modules used to test the Node.js implementation.

## Table of contents

* [ArrayStream module](#arraystream-module)
* [Benchmark module](#benchmark-module)
* [Child process module](#child-process-module)
* [Common module API](#common-module-api)
* [Countdown module](#countdown-module)
* [CPU Profiler module](#cpu-profiler-module)
* [Debugger module](#debugger-module)
* [DNS module](#dns-module)
* [Environment variables](#environment-variables)
* [Fixtures module](#fixtures-module)
* [Heap dump checker module](#heap-dump-checker-module)
* [hijackstdio module](#hijackstdio-module)
* [HTTP2 module](#http2-module)
* [Internet module](#internet-module)
* [ongc module](#ongc-module)
* [process-exit-code-test-cases module](#process-exit-code-test-cases-module)
* [Report module](#report-module)
* [tick module](#tick-module)
* [tmpdir module](#tmpdir-module)
* [UDP pair helper](#udp-pair-helper)
* [WPT module](#wpt-module)

## Benchmark module

The `benchmark` module is used by tests to run benchmarks.

### `runBenchmark(name, env)`

* `name` [\<string>][<string>] Name of benchmark suite to be run.
* `env` [\<Object>][<Object>] Environment variables to be applied during the
  run.

## Child Process module

The `child_process` module is used by tests that launch child processes.

### `spawnSyncAndExit(command[, args][, spawnOptions], expectations)`

Spawns a child process synchronously using [`child_process.spawnSync()`][] and
check if it runs in the way expected. If it does not, print the stdout and
stderr output from the child process and additional information about it to
the stderr of the current process before throwing and error. This helps
gathering more information about test failures coming from child processes.

* `command`, `args`, `spawnOptions` See [`child_process.spawnSync()`][]
* `expectations` [\<Object>][<Object>]
  * `status` [\<number>][<number>] Expected `child.status`
  * `signal` [\<string>][<string>] | `null` Expected `child.signal`
  * `stderr` [\<string>][<string>] | [\<RegExp>][<RegExp>] |
    [\<Function>][<Function>] Optional. If it's a string, check that the output
    to the stderr of the child process is exactly the same as the string. If
    it's a regular expression, check that the stderr matches it. If it's a
    function, invoke it with the stderr output as a string and check
    that it returns true. The function can just throw errors (e.g. assertion
    errors) to provide more information if the check fails.
  * `stdout` [\<string>][<string>] | [\<RegExp>][<RegExp>] |
    [\<Function>][<Function>] Optional. Similar to `stderr` but for the stdout.
  * `trim` [\<boolean>][<boolean>] Optional. Whether this method should trim
    out the whitespace characters when checking `stderr` and `stdout` outputs.
    Defaults to `false`.
* return [\<Object>][<Object>]
  * `child` [\<ChildProcess>][<ChildProcess>] The child process returned by
    [`child_process.spawnSync()`][].
  * `stderr` [\<string>][<string>] The output from the child process to stderr.
  * `stdout` [\<string>][<string>] The output from the child process to stdout.

### `spawnSyncAndExitWithoutError(command[, args][, spawnOptions])`

Similar to `expectSyncExit()` with the `status` expected to be 0 and
`signal` expected to be `null`.

### `spawnSyncAndAssert(command[, args][, spawnOptions], expectations)`

Similar to `spawnSyncAndExitWithoutError()`, but with an additional
`expectations` parameter.

## Common module API

The `common` module is used by tests for consistency across repeated
tasks.

### `allowGlobals(...allowlist)`

* `allowlist` [\<Array>][<Array>] Array of Globals
* return [\<Array>][<Array>]

Takes `allowlist` and concats that with predefined `knownGlobals`.

### `canCreateSymLink()`

* return [\<boolean>][<boolean>]

Checks whether the current running process can create symlinks. On Windows, this
returns `false` if the process running doesn't have privileges to create
symlinks
([SeCreateSymbolicLinkPrivilege](https://msdn.microsoft.com/en-us/library/windows/desktop/bb530716\(v=vs.85\).aspx)).
On non-Windows platforms, this always returns `true`.

### `createZeroFilledFile(filename)`

Creates a 10 MiB file of all null characters.

### `enoughTestMem`

* [\<boolean>][<boolean>]

Indicates if there is more than 1gb of total memory.

### `expectsError(validator[, exact])`

* `validator` [\<Object>][<Object>] | [\<RegExp>][<RegExp>] |
  [\<Function>][<Function>] | [\<Error>][<Error>] The validator behaves
  identical to `assert.throws(fn, validator)`.
* `exact` [\<number>][<number>] default = 1
* return [\<Function>][<Function>] A callback function that expects an error.

A function suitable as callback to validate callback based errors. The error is
validated using `assert.throws(() => { throw error; }, validator)`. If the
returned function has not been called exactly `exact` number of times when the
test is complete, then the test will fail.

### `expectWarning(name[, expected[, code]])`

* `name` [\<string>][<string>] | [\<Object>][<Object>]
* `expected` [\<string>][<string>] | [\<Array>][<Array>] | [\<Object>][<Object>]
* `code` [\<string>][<string>]

Tests whether `name`, `expected`, and `code` are part of a raised warning.

The code is required in case the name is set to `'DeprecationWarning'`.

Examples:

```js
const { expectWarning } = require('../common');

expectWarning('Warning', 'Foobar is really bad');

expectWarning('DeprecationWarning', 'Foobar is deprecated', 'DEP0XXX');

expectWarning('DeprecationWarning', [
  'Foobar is deprecated', 'DEP0XXX',
]);

expectWarning('DeprecationWarning', [
  ['Foobar is deprecated', 'DEP0XXX'],
  ['Baz is also deprecated', 'DEP0XX2'],
]);

expectWarning('DeprecationWarning', {
  DEP0XXX: 'Foobar is deprecated',
  DEP0XX2: 'Baz is also deprecated',
});

expectWarning({
  DeprecationWarning: {
    DEP0XXX: 'Foobar is deprecated',
    DEP0XX1: 'Baz is also deprecated',
  },
  Warning: [
    ['Multiple array entries are fine', 'SpecialWarningCode'],
    ['No code is also fine'],
  ],
  SingleEntry: ['This will also work', 'WarningCode'],
  SingleString: 'Single string entries without code will also work',
});
```

### `getArrayBufferViews(buf)`

* `buf` [\<Buffer>][<Buffer>]
* return [\<ArrayBufferView>][<ArrayBufferView>]\[]

Returns an instance of all possible `ArrayBufferView`s of the provided Buffer.

### `getBufferSources(buf)`

* `buf` [\<Buffer>][<Buffer>]
* return [\<BufferSource>][<BufferSource>]\[]

Returns an instance of all possible `BufferSource`s of the provided Buffer,
consisting of all `ArrayBufferView` and an `ArrayBuffer`.

### `getCallSite(func)`

* `func` [\<Function>][<Function>]
* return [\<string>][<string>]

Returns the file name and line number for the provided Function.

### `getTTYfd()`

Attempts to get a valid TTY file descriptor. Returns `-1` if it fails.

The TTY file descriptor is assumed to be capable of being writable.

### `hasCrypto`

* [\<boolean>][<boolean>]

Indicates whether OpenSSL is available.

### `hasFipsCrypto`

* [\<boolean>][<boolean>]

Indicates that Node.js has been linked with a FIPS compatible OpenSSL library,
and that FIPS as been enabled using `--enable-fips`.

To only detect if the OpenSSL library is FIPS compatible, regardless if it has
been enabled or not, then `process.config.variables.openssl_is_fips` can be
used to determine that situation.

### `hasIntl`

* [\<boolean>][<boolean>]

Indicates if [internationalization][] is supported.

### `hasIPv6`

* [\<boolean>][<boolean>]

Indicates whether `IPv6` is supported on this platform.

### `hasMultiLocalhost`

* [\<boolean>][<boolean>]

Indicates if there are multiple localhosts available.

### `inFreeBSDJail`

* [\<boolean>][<boolean>]

Checks whether free BSD Jail is true or false.

### `isAIX`

* [\<boolean>][<boolean>]

Platform check for Advanced Interactive eXecutive (AIX).

### `isAlive(pid)`

* `pid` [\<number>][<number>]
* return [\<boolean>][<boolean>]

Attempts to 'kill' `pid`

### `isDumbTerminal`

* [\<boolean>][<boolean>]

### `isFreeBSD`

* [\<boolean>][<boolean>]

Platform check for Free BSD.

### `isIBMi`

* [\<boolean>][<boolean>]

Platform check for IBMi.

### `isLinux`

* [\<boolean>][<boolean>]

Platform check for Linux.

### `isLinuxPPCBE`

* [\<boolean>][<boolean>]

Platform check for Linux on PowerPC.

### `isMacOS`

* [\<boolean>][<boolean>]

Platform check for macOS.

### `isSunOS`

* [\<boolean>][<boolean>]

Platform check for SunOS.

### `isWindows`

* [\<boolean>][<boolean>]

Platform check for Windows.

### `localhostIPv4`

* [\<string>][<string>]

IP of `localhost`.

### `localIPv6Hosts`

* [\<Array>][<Array>]

Array of IPV6 representations for `localhost`.

### `mustCall([fn][, exact])`

* `fn` [\<Function>][<Function>] default = () => {}
* `exact` [\<number>][<number>] default = 1
* return [\<Function>][<Function>]

Returns a function that calls `fn`. If the returned function has not been called
exactly `exact` number of times when the test is complete, then the test will
fail.

If `fn` is not provided, an empty function will be used.

### `mustCallAtLeast([fn][, minimum])`

* `fn` [\<Function>][<Function>] default = () => {}
* `minimum` [\<number>][<number>] default = 1
* return [\<Function>][<Function>]

Returns a function that calls `fn`. If the returned function has not been called
at least `minimum` number of times when the test is complete, then the test will
fail.

If `fn` is not provided, an empty function will be used.

### `mustNotCall([msg])`

* `msg` [\<string>][<string>] default = 'function should not have been called'
* return [\<Function>][<Function>]

Returns a function that triggers an `AssertionError` if it is invoked. `msg` is
used as the error message for the `AssertionError`.

### `mustNotMutateObjectDeep([target])`

* `target` [\<any>][<any>] default = `undefined`
* return [\<any>][<any>]

If `target` is an Object, returns a proxy object that triggers
an `AssertionError` on mutation attempt, including mutation of deeply nested
Objects. Otherwise, it returns `target` directly.

Use of this function is encouraged for relevant regression tests.

```mjs
import { open } from 'node:fs/promises';
import { mustNotMutateObjectDeep } from '../common/index.mjs';

const _mutableOptions = { length: 4, position: 8 };
const options = mustNotMutateObjectDeep(_mutableOptions);

// In filehandle.read or filehandle.write, attempt to mutate options will throw
// In the test code, options can still be mutated via _mutableOptions
const fh = await open('/path/to/file', 'r+');
const { buffer } = await fh.read(options);
_mutableOptions.position = 4;
await fh.write(buffer, options);

// Inline usage
const stats = await fh.stat(mustNotMutateObjectDeep({ bigint: true }));
console.log(stats.size);
```

Caveats: built-in objects that make use of their internal slots (for example,
`Map`s and `Set`s) might not work with this function. It returns Functions
directly, not preventing their mutation.

### `mustSucceed([fn])`

* `fn` [\<Function>][<Function>] default = () => {}
* return [\<Function>][<Function>]

Returns a function that accepts arguments `(err, ...args)`. If `err` is not
`undefined` or `null`, it triggers an `AssertionError`. Otherwise, it calls
`fn(...args)`.

### `nodeProcessAborted(exitCode, signal)`

* `exitCode` [\<number>][<number>]
* `signal` [\<string>][<string>]
* return [\<boolean>][<boolean>]

Returns `true` if the exit code `exitCode` and/or signal name `signal` represent
the exit code and/or signal name of a node process that aborted, `false`
otherwise.

### `opensslCli`

* [\<boolean>][<boolean>]

Indicates whether 'opensslCli' is supported.

### `platformTimeout(ms)`

* `ms` [\<number>][<number>] | [\<bigint>][<bigint>]
* return [\<number>][<number>] | [\<bigint>][<bigint>]

Returns a timeout value based on detected conditions. For example, a debug build
may need extra time so the returned value will be larger than on a release
build.

### `PIPE`

* [\<string>][<string>]

Path to the test socket.

### `PORT`

* [\<number>][<number>]

A port number for tests to use if one is needed.

### `printSkipMessage(msg)`

* `msg` [\<string>][<string>]

Logs '1..0 # Skipped: ' + `msg`

### `pwdCommand`

* [\<array>][<array>] First two argument for the `spawn`/`exec` functions.

Platform normalized `pwd` command options. Usage example:

```js
const common = require('../common');
const { spawn } = require('node:child_process');

spawn(...common.pwdCommand, { stdio: ['pipe'] });
```

### `requireNoPackageJSONAbove([dir])`

* `dir` [\<string>][<string>] default = \_\_dirname

Throws an `AssertionError` if a `package.json` file exists in any ancestor
directory above `dir`. Such files may interfere with proper test functionality.

### `runWithInvalidFD(func)`

* `func` [\<Function>][<Function>]

Runs `func` with an invalid file descriptor that is an unsigned integer and
can be used to trigger `EBADF` as the first argument. If no such file
descriptor could be generated, a skip message will be printed and the `func`
will not be run.

### `skip(msg)`

* `msg` [\<string>][<string>]

Logs '1..0 # Skipped: ' + `msg` and exits with exit code `0`.

### `skipIfDumbTerminal()`

Skip the rest of the tests if the current terminal is a dumb terminal

### `skipIfEslintMissing()`

Skip the rest of the tests in the current file when `ESLint` is not available
at `tools/eslint/node_modules/eslint`

### `skipIfInspectorDisabled()`

Skip the rest of the tests in the current file when the Inspector
was disabled at compile time.

### `skipIf32Bits()`

Skip the rest of the tests in the current file when the Node.js executable
was compiled with a pointer size smaller than 64 bits.

### `skipIfWorker()`

Skip the rest of the tests in the current file when not running on a main
thread.

## ArrayStream module

The `ArrayStream` module provides a simple `Stream` that pushes elements from
a given array.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
const ArrayStream = require('../common/arraystream');
const stream = new ArrayStream();
stream.run(['a', 'b', 'c']);
```

It can be used within tests as a simple mock stream.

## Countdown module

The `Countdown` module provides a simple countdown mechanism for tests that
require a particular action to be taken after a given number of completed
tasks (for instance, shutting down an HTTP server after a specific number of
requests). The Countdown will fail the test if the remainder did not reach 0.

<!-- eslint-disable strict, node-core/require-common-first, node-core/required-modules -->

```js
const Countdown = require('../common/countdown');

function doSomething() {
  console.log('.');
}

const countdown = new Countdown(2, doSomething);
countdown.dec();
countdown.dec();
```

### `new Countdown(limit, callback)`

* `limit` {number}
* `callback` {function}

Creates a new `Countdown` instance.

### `Countdown.prototype.dec()`

Decrements the `Countdown` counter.

### `Countdown.prototype.remaining`

Specifies the remaining number of times `Countdown.prototype.dec()` must be
called before the callback is invoked.

## CPU Profiler module

The `cpu-prof` module provides utilities related to CPU profiling tests.

### `env`

* Default: { ...process.env, NODE\_DEBUG\_NATIVE: 'INSPECTOR\_PROFILER' }

Environment variables used in profiled processes.

### `getCpuProfiles(dir)`

* `dir` {string} The directory containing the CPU profile files.
* return [\<string>][<string>]

Returns an array of all `.cpuprofile` files found in `dir`.

### `getFrames(file, suffix)`

* `file` {string} Path to a `.cpuprofile` file.
* `suffix` {string} Suffix of the URL of call frames to retrieve.
* returns { frames: [\<Object>][<Object>], nodes: [\<Object>][<Object>] }

Returns an object containing an array of the relevant call frames and an array
of all the profile nodes.

### `kCpuProfInterval`

Sampling interval in microseconds.

### `verifyFrames(output, file, suffix)`

* `output` {string}
* `file` {string}
* `suffix` {string}

Throws an `AssertionError` if there are no call frames with the expected
`suffix` in the profiling data contained in `file`.

## Debugger module

Provides common functionality for tests for `node inspect`.

### `startCLI(args[[, flags], spawnOpts])`

* `args` [\<string>][<string>]
* `flags` [\<string>][<string>] default = \[]
* `showOpts` [\<Object>][<Object>] default = {}
* return [\<Object>][<Object>]

Returns a null-prototype object with properties that are functions and getters
used to interact with the `node inspect` CLI. These functions are:

* `flushOutput()`
* `waitFor()`
* `waitForPrompt()`
* `waitForInitialBreak()`
* `breakInfo`
* `ctrlC()`
* `output`
* `rawOutput`
* `parseSourceLines()`
* `writeLine()`
* `command()`
* `stepCommand()`
* `quit()`

## `DNS` module

The `DNS` module provides utilities related to the `dns` built-in module.

### `errorLookupMock(code, syscall)`

* `code` [\<string>][<string>] Defaults to `dns.mockedErrorCode`.
* `syscall` [\<string>][<string>] Defaults to `dns.mockedSysCall`.
* return [\<Function>][<Function>]

A mock for the `lookup` option of `net.connect()` that would result in an error
with the `code` and the `syscall` specified. Returns a function that has the
same signature as `dns.lookup()`.

### `mockedErrorCode`

The default `code` of errors generated by `errorLookupMock`.

### `mockedSysCall`

The default `syscall` of errors generated by `errorLookupMock`.

### `readDomainFromPacket(buffer, offset)`

* `buffer` [\<Buffer>][<Buffer>]
* `offset` [\<number>][<number>]
* return [\<Object>][<Object>]

Reads the domain string from a packet and returns an object containing the
number of bytes read and the domain.

### `parseDNSPacket(buffer)`

* `buffer` [\<Buffer>][<Buffer>]
* return [\<Object>][<Object>]

Parses a DNS packet. Returns an object with the values of the various flags of
the packet depending on the type of packet.

### `writeIPv6(ip)`

* `ip` [\<string>][<string>]
* return [\<Buffer>][<Buffer>]

Reads an IPv6 String and returns a Buffer containing the parts.

### `writeDomainName(domain)`

* `domain` [\<string>][<string>]
* return [\<Buffer>][<Buffer>]

Reads a Domain String and returns a Buffer containing the domain.

### `writeDNSPacket(parsed)`

* `parsed` [\<Object>][<Object>]
* return [\<Buffer>][<Buffer>]

Takes in a parsed Object and writes its fields to a DNS packet as a Buffer
object.

## Environment variables

The behavior of the Node.js test suite can be altered using the following
environment variables.

### `NODE_COMMON_PORT`

If set, `NODE_COMMON_PORT`'s value overrides the `common.PORT` default value of
12346\.

### `NODE_REGENERATE_SNAPSHOTS`

If set, test snapshots for a the current test are regenerated.
for example `NODE_REGENERATE_SNAPSHOTS=1 out/Release/node test/parallel/test-runner-output.mjs`
will update all the test runner output snapshots.

### `NODE_SKIP_FLAG_CHECK`

If set, command line arguments passed to individual tests are not validated.

### `NODE_SKIP_CRYPTO`

If set, crypto tests are skipped.

### `NODE_TEST_KNOWN_GLOBALS`

A comma-separated list of variables names that are appended to the global
variable allowlist. Alternatively, if `NODE_TEST_KNOWN_GLOBALS` is set to `'0'`,
global leak detection is disabled.

## Fixtures module

The `common/fixtures` module provides convenience methods for working with
files in the `test/fixtures` directory.

### `fixtures.fixturesDir`

* [\<string>][<string>]

The absolute path to the `test/fixtures/` directory.

### `fixtures.path(...args)`

* `...args` [\<string>][<string>]

Returns the result of `path.join(fixtures.fixturesDir, ...args)`.

### `fixtures.fileURL(...args)`

* `...args` [\<string>][<string>]

Returns the result of `url.pathToFileURL(fixtures.path(...args))`.

### `fixtures.readSync(args[, enc])`

* `args` [\<string>][<string>] | [\<Array>][<Array>]

Returns the result of
`fs.readFileSync(path.join(fixtures.fixturesDir, ...args), 'enc')`.

### `fixtures.readKey(arg[, enc])`

* `arg` [\<string>][<string>]

Returns the result of
`fs.readFileSync(path.join(fixtures.fixturesDir, 'keys', arg), 'enc')`.

## Heap dump checker module

This provides utilities for checking the validity of heap dumps.
This requires the usage of `--expose-internals`.

### `heap.recordState()`

Create a heap dump and an embedder graph copy for inspection.
The returned object has a `validateSnapshotNodes` function similar to the
one listed below. (`heap.validateSnapshotNodes(...)` is a shortcut for
`heap.recordState().validateSnapshotNodes(...)`.)

### `heap.validateSnapshotNodes(name, expected, options)`

* `name` [\<string>][<string>] Look for this string as the name of heap dump
  nodes.
* `expected` [\<Array>][<Array>] A list of objects, possibly with an `children`
  property that points to expected other adjacent nodes.
* `options` [\<Array>][<Array>]
  * `loose` [\<boolean>][<boolean>] Do not expect an exact listing of
    occurrences of nodes with name `name` in `expected`.

Create a heap dump and an embedder graph copy and validate occurrences.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
validateSnapshotNodes('TLSWRAP', [
  {
    children: [
      { name: 'enc_out' },
      { name: 'enc_in' },
      { name: 'TLSWrap' },
    ],
  },
]);
```

## hijackstdio module

The `hijackstdio` module provides utility functions for temporarily redirecting
`stdout` and `stderr` output.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
const { hijackStdout, restoreStdout } = require('../common/hijackstdio');

hijackStdout((data) => {
  /* Do something with data */
  restoreStdout();
});

console.log('this is sent to the hijacked listener');
```

### `hijackStderr(listener)`

* `listener` [\<Function>][<Function>]: a listener with a single parameter
  called `data`.

Eavesdrop to `process.stderr.write()` calls. Once `process.stderr.write()` is
called, `listener` will also be called and the `data` of `write` function will
be passed to `listener`. What's more, `process.stderr.writeTimes` is a count of
the number of calls.

### `hijackStdout(listener)`

* `listener` [\<Function>][<Function>]: a listener with a single parameter
  called `data`.

Eavesdrop to `process.stdout.write()` calls. Once `process.stdout.write()` is
called, `listener` will also be called and the `data` of `write` function will
be passed to `listener`. What's more, `process.stdout.writeTimes` is a count of
the number of calls.

### restoreStderr()

Restore the original `process.stderr.write()`. Used to restore `stderr` to its
original state after calling [`hijackstdio.hijackStdErr()`][].

### restoreStdout()

Restore the original `process.stdout.write()`. Used to restore `stdout` to its
original state after calling [`hijackstdio.hijackStdOut()`][].

## HTTP/2 module

The http2.js module provides a handful of utilities for creating mock HTTP/2
frames for testing of HTTP/2 endpoints

<!-- eslint-disable no-unused-vars, node-core/require-common-first, node-core/required-modules -->

```js
const http2 = require('../common/http2');
```

### Class: Frame

The `http2.Frame` is a base class that creates a `Buffer` containing a
serialized HTTP/2 frame header.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
// length is a 24-bit unsigned integer
// type is an 8-bit unsigned integer identifying the frame type
// flags is an 8-bit unsigned integer containing the flag bits
// id is the 32-bit stream identifier, if any.
const frame = new http2.Frame(length, type, flags, id);

// Write the frame data to a socket
socket.write(frame.data);
```

The serialized `Buffer` may be retrieved using the `frame.data` property.

### Class: DataFrame extends Frame

The `http2.DataFrame` is a subclass of `http2.Frame` that serializes a `DATA`
frame.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
// id is the 32-bit stream identifier
// payload is a Buffer containing the DATA payload
// padlen is an 8-bit integer giving the number of padding bytes to include
// final is a boolean indicating whether the End-of-stream flag should be set,
// defaults to false.
const frame = new http2.DataFrame(id, payload, padlen, final);

socket.write(frame.data);
```

### Class: HeadersFrame

The `http2.HeadersFrame` is a subclass of `http2.Frame` that serializes a
`HEADERS` frame.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
// id is the 32-bit stream identifier
// payload is a Buffer containing the HEADERS payload (see either
// http2.kFakeRequestHeaders or http2.kFakeResponseHeaders).
// padlen is an 8-bit integer giving the number of padding bytes to include
// final is a boolean indicating whether the End-of-stream flag should be set,
// defaults to false.
const frame = new http2.HeadersFrame(id, payload, padlen, final);

socket.write(frame.data);
```

### Class: SettingsFrame

The `http2.SettingsFrame` is a subclass of `http2.Frame` that serializes an
empty `SETTINGS` frame.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
// ack is a boolean indicating whether or not to set the ACK flag.
const frame = new http2.SettingsFrame(ack);

socket.write(frame.data);
```

### `http2.kFakeRequestHeaders`

Set to a `Buffer` instance that contains a minimal set of serialized HTTP/2
request headers to be used as the payload of a `http2.HeadersFrame`.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
const frame = new http2.HeadersFrame(1, http2.kFakeRequestHeaders, 0, true);

socket.write(frame.data);
```

### `http2.kFakeResponseHeaders`

Set to a `Buffer` instance that contains a minimal set of serialized HTTP/2
response headers to be used as the payload a `http2.HeadersFrame`.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
const frame = new http2.HeadersFrame(1, http2.kFakeResponseHeaders, 0, true);

socket.write(frame.data);
```

### `http2.kClientMagic`

Set to a `Buffer` containing the preamble bytes an HTTP/2 client must send
upon initial establishment of a connection.

<!-- eslint-disable no-undef, node-core/require-common-first, node-core/required-modules -->

```js
socket.write(http2.kClientMagic);
```

## Internet module

The `common/internet` module provides utilities for working with
internet-related tests.

### `internet.addresses`

* [\<Object>][<Object>]
  * `INET_HOST` [\<string>][<string>] A generic host that has registered common
    DNS records, supports both IPv4 and IPv6, and provides basic HTTP/HTTPS
    services
  * `INET4_HOST` [\<string>][<string>] A host that provides IPv4 services
  * `INET6_HOST` [\<string>][<string>] A host that provides IPv6 services
  * `INET4_IP` [\<string>][<string>] An accessible IPv4 IP, defaults to the
    Google Public DNS IPv4 address
  * `INET6_IP` [\<string>][<string>] An accessible IPv6 IP, defaults to the
    Google Public DNS IPv6 address
  * `INVALID_HOST` [\<string>][<string>] An invalid host that cannot be resolved
  * `MX_HOST` [\<string>][<string>] A host with MX records registered
  * `SRV_HOST` [\<string>][<string>] A host with SRV records registered
  * `PTR_HOST` [\<string>][<string>] A host with PTR records registered
  * `NAPTR_HOST` [\<string>][<string>] A host with NAPTR records registered
  * `SOA_HOST` [\<string>][<string>] A host with SOA records registered
  * `CNAME_HOST` [\<string>][<string>] A host with CNAME records registered
  * `NS_HOST` [\<string>][<string>] A host with NS records registered
  * `TXT_HOST` [\<string>][<string>] A host with TXT records registered
  * `DNS4_SERVER` [\<string>][<string>] An accessible IPv4 DNS server
  * `DNS6_SERVER` [\<string>][<string>] An accessible IPv6 DNS server

A set of addresses for internet-related tests. All properties are configurable
via `NODE_TEST_*` environment variables. For example, to configure
`internet.addresses.INET_HOST`, set the environment
variable `NODE_TEST_INET_HOST` to a specified host.

## ongc module

The `ongc` module allows a garbage collection listener to be installed. The
module exports a single `onGC()` function.

```js
require('../common');
const onGC = require('../common/ongc');

onGC({}, { ongc() { console.log('collected'); } });
```

### `onGC(target, listener)`

* `target` [\<Object>][<Object>]
* `listener` [\<Object>][<Object>]
  * `ongc` [\<Function>][<Function>]

Installs a GC listener for the collection of `target`.

This uses `async_hooks` for GC tracking. This means that it enables
`async_hooks` tracking, which may affect the test functionality. It also
means that between a `global.gc()` call and the listener being invoked
a full `setImmediate()` invocation passes.

`listener` is an object to make it easier to use a closure; the target object
should not be in scope when `listener.ongc()` is created.

## process-exit-code-test-cases module

The `process-exit-code-test-cases` module provides a set of shared test cases
for testing the exit codes of the `process` object. The test cases are shared
between `test/parallel/test-process-exit-code.js` and
`test/parallel/test-worker-exit-code.js`.

### `getTestCases(isWorker)`

* `isWorker` [\<boolean>][<boolean>]
* return [\<Array>][<Array>]

Returns an array of test cases for testing the exit codes of the `process`. Each
test case is an object with a `func` property that is a function that runs the
test case, a `result` property that is the expected exit code, and sometimes an
`error` property that is a regular expression that the error message should
match when the test case is run in a worker thread.

The `isWorker` parameter is used to adjust the test cases for worker threads.
The default value is `false`.

## Report module

The `report` module provides helper functions for testing diagnostic reporting
functionality.

### `findReports(pid, dir)`

* `pid` [\<number>][<number>] Process ID to retrieve diagnostic report files
  for.
* `dir` [\<string>][<string>] Directory to search for diagnostic report files.
* return [\<Array>][<Array>]

Returns an array of diagnostic report file names found in `dir`. The files
should have been generated by a process whose PID matches `pid`.

### `validate(filepath)`

* `filepath` [\<string>][<string>] Diagnostic report filepath to validate.

Validates the schema of a diagnostic report file whose path is specified in
`filepath`. If the report fails validation, an exception is thrown.

### `validateContent(report)`

* `report` [\<Object>][<Object>] | [\<string>][<string>] JSON contents of a
  diagnostic report file, the parsed Object thereof, or the result of
  `process.report.getReport()`.

Validates the schema of a diagnostic report whose content is specified in
`report`. If the report fails validation, an exception is thrown.

## SEA Module

The `sea` module provides helper functions for testing Single Executable
Application functionality.

### `skipIfSingleExecutableIsNotSupported()`

Skip the rest of the tests if single executable applications are not supported
in the current configuration.

### `generateSEA(targetExecutable, sourceExecutable, seaBlob, verifyWorkflow)`

Copy `sourceExecutable` to `targetExecutable`, use postject to inject `seaBlob`
into `targetExecutable` and sign it if necessary.

If `verifyWorkflow` is false (default) and any of the steps fails,
it skips the tests. Otherwise, an error is thrown.

## tick module

The `tick` module provides a helper function that can be used to call a callback
after a given number of event loop "ticks".

### `tick(x, cb)`

* `x` [\<number>][<number>] Number of event loop "ticks".
* `cb` [\<Function>][<Function>] A callback function.

## tmpdir module

The `tmpdir` module supports the use of a temporary directory for testing.

### `path`

* [\<string>][<string>]

The realpath of the testing temporary directory.

### `fileURL([...paths])`

* `...paths` [\<string>][<string>]
* return [\<URL>][<URL>]

Resolves a sequence of paths into absolute url in the temporary directory.

When called without arguments, returns absolute url of the testing
temporary directory with explicit trailing `/`.

### `refresh(useSpawn)`

* `useSpawn` [\<boolean>][<boolean>] default = false

Deletes and recreates the testing temporary directory. When `useSpawn` is true
this action is performed using `child_process.spawnSync`.

The first time `refresh()` runs, it adds a listener to process `'exit'` that
cleans the temporary directory. Thus, every file under `tmpdir.path` needs to
be closed before the test completes. A good way to do this is to add a
listener to process `'beforeExit'`. If a file needs to be left open until
Node.js completes, use a child process and call `refresh()` only in the
parent.

It is usually only necessary to call `refresh()` once in a test file.
Avoid calling it more than once in an asynchronous context as one call
might refresh the temporary directory of a different context, causing
the test to fail somewhat mysteriously.

### `resolve([...paths])`

* `...paths` [\<string>][<string>]
* return [\<string>][<string>]

Resolves a sequence of paths into absolute path in the temporary directory.

### `hasEnoughSpace(size)`

* `size` [\<number>][<number>] Required size, in bytes.

Returns `true` if the available blocks of the file system underlying `path`
are likely sufficient to hold a single file of `size` bytes. This is useful for
skipping tests that require hundreds of megabytes or even gigabytes of temporary
files, but it is inaccurate and susceptible to race conditions.

## UDP pair helper

The `common/udppair` module exports a function `makeUDPPair` and a class
`FakeUDPWrap`.

`FakeUDPWrap` emits `'send'` events when data is to be sent on it, and provides
an `emitReceived()` API for actin as if data has been received on it.

`makeUDPPair` returns an object `{ clientSide, serverSide }` where each side
is an `FakeUDPWrap` connected to the other side.

There is no difference between client or server side beyond their names.

## WPT module

### `harness`

A legacy port of [Web Platform Tests][] harness.

See the source code for definitions. Please avoid using it in new
code - the current usage of this port in tests is being migrated to
the original WPT harness, see [the WPT tests README][].

### Class: WPTRunner

A driver class for running WPT with the WPT harness in a worker thread.

See [the WPT tests README][] for details.

[<Array>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Array
[<ArrayBufferView>]: https://developer.mozilla.org/en-US/docs/Web/API/ArrayBufferView
[<Buffer>]: https://nodejs.org/api/buffer.html#buffer_class_buffer
[<BufferSource>]: https://developer.mozilla.org/en-US/docs/Web/API/BufferSource
[<ChildProcess>]: ../../doc/api/child_process.md#class-childprocess
[<Error>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Error
[<Function>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Function
[<Object>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Object
[<RegExp>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/RegExp
[<URL>]: https://developer.mozilla.org/en-US/docs/Web/API/URL
[<any>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Data_structures#Data_types
[<bigint>]: https://github.com/tc39/proposal-bigint
[<boolean>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Data_structures#Boolean_type
[<number>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Data_structures#Number_type
[<string>]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Data_structures#String_type
[Web Platform Tests]: https://github.com/web-platform-tests/wpt
[`child_process.spawnSync()`]: ../../doc/api/child_process.md#child_processspawnsynccommand-args-options
[`hijackstdio.hijackStdErr()`]: #hijackstderrlistener
[`hijackstdio.hijackStdOut()`]: #hijackstdoutlistener
[internationalization]: ../../doc/api/intl.md
[the WPT tests README]: ../wpt/README.md
