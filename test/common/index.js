// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

/* eslint-disable node-core/crypto-check */
'use strict';
const process = global.process;  // Some tests tamper with the process global.

const assert = require('assert');
const { exec, execSync, spawn, spawnSync } = require('child_process');
const fs = require('fs');
const net = require('net');
// Do not require 'os' until needed so that test-os-checked-function can
// monkey patch it. If 'os' is required here, that test will fail.
const path = require('path');
const { inspect } = require('util');
const { isMainThread } = require('worker_threads');
const { isModuleNamespaceObject } = require('util/types');

const tmpdir = require('./tmpdir');
const bits = ['arm64', 'loong64', 'mips', 'mipsel', 'ppc64', 'riscv64', 's390x', 'x64']
  .includes(process.arch) ? 64 : 32;
const hasIntl = !!process.config.variables.v8_enable_i18n_support;

const {
  atob,
  btoa,
} = require('buffer');

// Some tests assume a umask of 0o022 so set that up front. Tests that need a
// different umask will set it themselves.
//
// Workers can read, but not set the umask, so check that this is the main
// thread.
if (isMainThread)
  process.umask(0o022);

const noop = () => {};

const hasCrypto = Boolean(process.versions.openssl) &&
                  !process.env.NODE_SKIP_CRYPTO;

// Synthesize OPENSSL_VERSION_NUMBER format with the layout 0xMNN00PPSL
const opensslVersionNumber = (major = 0, minor = 0, patch = 0) => {
  assert(major >= 0 && major <= 0xf);
  assert(minor >= 0 && minor <= 0xff);
  assert(patch >= 0 && patch <= 0xff);
  return (major << 28) | (minor << 20) | (patch << 4);
};

let OPENSSL_VERSION_NUMBER;
const hasOpenSSL = (major = 0, minor = 0, patch = 0) => {
  if (!hasCrypto) return false;
  if (OPENSSL_VERSION_NUMBER === undefined) {
    const regexp = /(?<m>\d+)\.(?<n>\d+)\.(?<p>\d+)/;
    const { m, n, p } = process.versions.openssl.match(regexp).groups;
    OPENSSL_VERSION_NUMBER = opensslVersionNumber(m, n, p);
  }
  return OPENSSL_VERSION_NUMBER >= opensslVersionNumber(major, minor, patch);
};

const hasQuic = hasCrypto && !!process.config.variables.openssl_quic;

function parseTestFlags(filename = process.argv[1]) {
  // The copyright notice is relatively big and the flags could come afterwards.
  const bytesToRead = 1500;
  const buffer = Buffer.allocUnsafe(bytesToRead);
  const fd = fs.openSync(filename, 'r');
  const bytesRead = fs.readSync(fd, buffer, 0, bytesToRead);
  fs.closeSync(fd);
  const source = buffer.toString('utf8', 0, bytesRead);

  const flagStart = source.search(/\/\/ Flags:\s+--/) + 10;

  if (flagStart === 9) {
    return [];
  }
  let flagEnd = source.indexOf('\n', flagStart);
  // Normalize different EOL.
  if (source[flagEnd - 1] === '\r') {
    flagEnd--;
  }
  return source
    .substring(flagStart, flagEnd)
    .split(/\s+/)
    .filter(Boolean);
}

// Check for flags. Skip this for workers (both, the `cluster` module and
// `worker_threads`) and child processes.
// If the binary was built without-ssl then the crypto flags are
// invalid (bad option). The test itself should handle this case.
if (process.argv.length === 2 &&
    !process.env.NODE_SKIP_FLAG_CHECK &&
    isMainThread &&
    hasCrypto &&
    require('cluster').isPrimary &&
    fs.existsSync(process.argv[1])) {
  const flags = parseTestFlags();
  for (const flag of flags) {
    if (!process.execArgv.includes(flag) &&
        // If the binary is build without `intl` the inspect option is
        // invalid. The test itself should handle this case.
        (process.features.inspector || !flag.startsWith('--inspect'))) {
      console.log(
        'NOTE: The test started as a child_process using these flags:',
        inspect(flags),
        'Use NODE_SKIP_FLAG_CHECK to run the test with the original flags.',
      );
      const args = [...flags, ...process.execArgv, ...process.argv.slice(1)];
      const options = { encoding: 'utf8', stdio: 'inherit' };
      const result = spawnSync(process.execPath, args, options);
      if (result.signal) {
        process.kill(0, result.signal);
      } else {
        process.exit(result.status);
      }
    }
  }
}

const isWindows = process.platform === 'win32';
const isSunOS = process.platform === 'sunos';
const isFreeBSD = process.platform === 'freebsd';
const isOpenBSD = process.platform === 'openbsd';
const isLinux = process.platform === 'linux';
const isMacOS = process.platform === 'darwin';
const isASan = process.config.variables.asan === 1;
const isDebug = process.features.debug;
const isPi = (() => {
  try {
    // Normal Raspberry Pi detection is to find the `Raspberry Pi` string in
    // the contents of `/sys/firmware/devicetree/base/model` but that doesn't
    // work inside a container. Match the chipset model number instead.
    const cpuinfo = fs.readFileSync('/proc/cpuinfo', { encoding: 'utf8' });
    const ok = /^Hardware\s*:\s*(.*)$/im.exec(cpuinfo)?.[1] === 'BCM2835';
    /^/.test('');  // Clear RegExp.$_, some tests expect it to be empty.
    return ok;
  } catch {
    return false;
  }
})();

const isDumbTerminal = process.env.TERM === 'dumb';

// When using high concurrency or in the CI we need much more time for each connection attempt
net.setDefaultAutoSelectFamilyAttemptTimeout(platformTimeout(net.getDefaultAutoSelectFamilyAttemptTimeout() * 10));
const defaultAutoSelectFamilyAttemptTimeout = net.getDefaultAutoSelectFamilyAttemptTimeout();

const buildType = process.config.target_defaults ?
  process.config.target_defaults.default_configuration :
  'Release';

// If env var is set then enable async_hook hooks for all tests.
if (process.env.NODE_TEST_WITH_ASYNC_HOOKS) {
  const destroydIdsList = {};
  const destroyListList = {};
  const initHandles = {};
  const { internalBinding } = require('internal/test/binding');
  const async_wrap = internalBinding('async_wrap');

  process.on('exit', () => {
    // Iterate through handles to make sure nothing crashes
    for (const k in initHandles)
      inspect(initHandles[k]);
  });

  const _queueDestroyAsyncId = async_wrap.queueDestroyAsyncId;
  async_wrap.queueDestroyAsyncId = function queueDestroyAsyncId(id) {
    if (destroyListList[id] !== undefined) {
      process._rawDebug(destroyListList[id]);
      process._rawDebug();
      throw new Error(`same id added to destroy list twice (${id})`);
    }
    destroyListList[id] = inspect(new Error());
    _queueDestroyAsyncId(id);
  };

  require('async_hooks').createHook({
    init(id, ty, tr, resource) {
      if (initHandles[id]) {
        process._rawDebug(
          `Is same resource: ${resource === initHandles[id].resource}`);
        process._rawDebug(`Previous stack:\n${initHandles[id].stack}\n`);
        throw new Error(`init called twice for same id (${id})`);
      }
      initHandles[id] = {
        resource,
        stack: inspect(new Error()).slice(6),
      };
    },
    before() { },
    after() { },
    destroy(id) {
      if (destroydIdsList[id] !== undefined) {
        process._rawDebug(destroydIdsList[id]);
        process._rawDebug();
        throw new Error(`destroy called for same id (${id})`);
      }
      destroydIdsList[id] = inspect(new Error());
    },
  }).enable();
}

let opensslCli = null;
let inFreeBSDJail = null;
let localhostIPv4 = null;

const localIPv6Hosts =
  isLinux ? [
    // Debian/Ubuntu
    'ip6-localhost',
    'ip6-loopback',

    // SUSE
    'ipv6-localhost',
    'ipv6-loopback',

    // Typically universal
    'localhost',
  ] : [ 'localhost' ];

const PIPE = (() => {
  const localRelative = path.relative(process.cwd(), `${tmpdir.path}/`);
  const pipePrefix = isWindows ? '\\\\.\\pipe\\' : localRelative;
  const pipeName = `node-test.${process.pid}.sock`;
  return path.join(pipePrefix, pipeName);
})();

// Check that when running a test with
// `$node --abort-on-uncaught-exception $file child`
// the process aborts.
function childShouldThrowAndAbort() {
  let testCmd = '';
  if (!isWindows) {
    // Do not create core files, as it can take a lot of disk space on
    // continuous testing and developers' machines
    testCmd += 'ulimit -c 0 && ';
  }
  testCmd += `"${process.argv[0]}" --abort-on-uncaught-exception `;
  testCmd += `"${process.argv[1]}" child`;
  const child = exec(testCmd);
  child.on('exit', function onExit(exitCode, signal) {
    const errMsg = 'Test should have aborted ' +
                   `but instead exited with exit code ${exitCode}` +
                   ` and signal ${signal}`;
    assert(nodeProcessAborted(exitCode, signal), errMsg);
  });
}

function createZeroFilledFile(filename) {
  const fd = fs.openSync(filename, 'w');
  fs.ftruncateSync(fd, 10 * 1024 * 1024);
  fs.closeSync(fd);
}


const pwdCommand = isWindows ?
  ['cmd.exe', ['/d', '/c', 'cd']] :
  ['pwd', []];


function platformTimeout(ms) {
  const multipliers = typeof ms === 'bigint' ?
    { two: 2n, four: 4n, seven: 7n } : { two: 2, four: 4, seven: 7 };

  if (isDebug)
    ms = multipliers.two * ms;

  if (exports.isAIX || exports.isIBMi)
    return multipliers.two * ms; // Default localhost speed is slower on AIX

  if (isPi)
    return multipliers.two * ms;  // Raspberry Pi devices

  return ms;
}

let knownGlobals = [
  AbortController,
  atob,
  btoa,
  clearImmediate,
  clearInterval,
  clearTimeout,
  global,
  setImmediate,
  setInterval,
  setTimeout,
  queueMicrotask,
];

if (global.gc) {
  knownGlobals.push(global.gc);
}

if (global.navigator) {
  knownGlobals.push(global.navigator);
}

if (global.Navigator) {
  knownGlobals.push(global.Navigator);
}

if (global.Performance) {
  knownGlobals.push(global.Performance);
}
if (global.performance) {
  knownGlobals.push(global.performance);
}
if (global.PerformanceMark) {
  knownGlobals.push(global.PerformanceMark);
}
if (global.PerformanceMeasure) {
  knownGlobals.push(global.PerformanceMeasure);
}

// TODO(@ethan-arrowood): Similar to previous checks, this can be temporary
// until v16.x is EOL. Once all supported versions have structuredClone we
// can add this to the list above instead.
if (global.structuredClone) {
  knownGlobals.push(global.structuredClone);
}

if (global.EventSource) {
  knownGlobals.push(EventSource);
}

if (global.fetch) {
  knownGlobals.push(fetch);
}
if (hasCrypto && global.crypto) {
  knownGlobals.push(global.crypto);
  knownGlobals.push(global.Crypto);
  knownGlobals.push(global.CryptoKey);
  knownGlobals.push(global.SubtleCrypto);
}
if (global.CustomEvent) {
  knownGlobals.push(global.CustomEvent);
}
if (global.ReadableStream) {
  knownGlobals.push(
    global.ReadableStream,
    global.ReadableStreamDefaultReader,
    global.ReadableStreamBYOBReader,
    global.ReadableStreamBYOBRequest,
    global.ReadableByteStreamController,
    global.ReadableStreamDefaultController,
    global.TransformStream,
    global.TransformStreamDefaultController,
    global.WritableStream,
    global.WritableStreamDefaultWriter,
    global.WritableStreamDefaultController,
    global.ByteLengthQueuingStrategy,
    global.CountQueuingStrategy,
    global.TextEncoderStream,
    global.TextDecoderStream,
    global.CompressionStream,
    global.DecompressionStream,
  );
}

if (global.Storage) {
  knownGlobals.push(
    global.localStorage,
    global.sessionStorage,
    global.Storage,
  );
}

function allowGlobals(...allowlist) {
  knownGlobals = knownGlobals.concat(allowlist);
}

if (process.env.NODE_TEST_KNOWN_GLOBALS !== '0') {
  if (process.env.NODE_TEST_KNOWN_GLOBALS) {
    const knownFromEnv = process.env.NODE_TEST_KNOWN_GLOBALS.split(',');
    allowGlobals(...knownFromEnv);
  }

  function leakedGlobals() {
    const leaked = [];

    for (const val in global) {
      // globalThis.crypto is a getter that throws if Node.js was compiled
      // without OpenSSL.
      if (val !== 'crypto' && !knownGlobals.includes(global[val])) {
        leaked.push(val);
      }
    }

    return leaked;
  }

  process.on('exit', function() {
    const leaked = leakedGlobals();
    if (leaked.length > 0) {
      assert.fail(`Unexpected global(s) found: ${leaked.join(', ')}`);
    }
  });
}

const mustCallChecks = [];

function runCallChecks(exitCode) {
  if (exitCode !== 0) return;

  const failed = mustCallChecks.filter(function(context) {
    if ('minimum' in context) {
      context.messageSegment = `at least ${context.minimum}`;
      return context.actual < context.minimum;
    }
    context.messageSegment = `exactly ${context.exact}`;
    return context.actual !== context.exact;
  });

  failed.forEach(function(context) {
    console.log('Mismatched %s function calls. Expected %s, actual %d.',
                context.name,
                context.messageSegment,
                context.actual);
    console.log(context.stack.split('\n').slice(2).join('\n'));
  });

  if (failed.length) process.exit(1);
}

function mustCall(fn, exact) {
  return _mustCallInner(fn, exact, 'exact');
}

function mustSucceed(fn, exact) {
  return mustCall(function(err, ...args) {
    assert.ifError(err);
    if (typeof fn === 'function')
      return fn.apply(this, args);
  }, exact);
}

function mustCallAtLeast(fn, minimum) {
  return _mustCallInner(fn, minimum, 'minimum');
}

function _mustCallInner(fn, criteria = 1, field) {
  if (process._exiting)
    throw new Error('Cannot use common.mustCall*() in process exit handler');
  if (typeof fn === 'number') {
    criteria = fn;
    fn = noop;
  } else if (fn === undefined) {
    fn = noop;
  }

  if (typeof criteria !== 'number')
    throw new TypeError(`Invalid ${field} value: ${criteria}`);

  const context = {
    [field]: criteria,
    actual: 0,
    stack: inspect(new Error()),
    name: fn.name || '<anonymous>',
  };

  // Add the exit listener only once to avoid listener leak warnings
  if (mustCallChecks.length === 0) process.on('exit', runCallChecks);

  mustCallChecks.push(context);

  const _return = function() { // eslint-disable-line func-style
    context.actual++;
    return fn.apply(this, arguments);
  };
  // Function instances have own properties that may be relevant.
  // Let's replicate those properties to the returned function.
  // Refs: https://tc39.es/ecma262/#sec-function-instances
  Object.defineProperties(_return, {
    name: {
      value: fn.name,
      writable: false,
      enumerable: false,
      configurable: true,
    },
    length: {
      value: fn.length,
      writable: false,
      enumerable: false,
      configurable: true,
    },
  });
  return _return;
}

function hasMultiLocalhost() {
  const { internalBinding } = require('internal/test/binding');
  const { TCP, constants: TCPConstants } = internalBinding('tcp_wrap');
  const t = new TCP(TCPConstants.SOCKET);
  const ret = t.bind('127.0.0.2', 0);
  t.close();
  return ret === 0;
}

function skipIfEslintMissing() {
  if (!fs.existsSync(
    path.join(__dirname, '..', '..', 'tools', 'eslint', 'node_modules', 'eslint'),
  )) {
    skip('missing ESLint');
  }
}

function canCreateSymLink() {
  // On Windows, creating symlinks requires admin privileges.
  // We'll only try to run symlink test if we have enough privileges.
  // On other platforms, creating symlinks shouldn't need admin privileges
  if (isWindows) {
    // whoami.exe needs to be the one from System32
    // If unix tools are in the path, they can shadow the one we want,
    // so use the full path while executing whoami
    const whoamiPath = path.join(process.env.SystemRoot,
                                 'System32', 'whoami.exe');

    try {
      const output = execSync(`${whoamiPath} /priv`, { timeout: 1000 });
      return output.includes('SeCreateSymbolicLinkPrivilege');
    } catch {
      return false;
    }
  }
  // On non-Windows platforms, this always returns `true`
  return true;
}

function getCallSite(top) {
  const originalStackFormatter = Error.prepareStackTrace;
  Error.prepareStackTrace = (err, stack) =>
    `${stack[0].getFileName()}:${stack[0].getLineNumber()}`;
  const err = new Error();
  Error.captureStackTrace(err, top);
  // With the V8 Error API, the stack is not formatted until it is accessed
  err.stack; // eslint-disable-line no-unused-expressions
  Error.prepareStackTrace = originalStackFormatter;
  return err.stack;
}

function mustNotCall(msg) {
  const callSite = getCallSite(mustNotCall);
  return function mustNotCall(...args) {
    const argsInfo = args.length > 0 ?
      `\ncalled with arguments: ${args.map((arg) => inspect(arg)).join(', ')}` : '';
    assert.fail(
      `${msg || 'function should not have been called'} at ${callSite}` +
      argsInfo);
  };
}

const _mustNotMutateObjectDeepProxies = new WeakMap();

function mustNotMutateObjectDeep(original) {
  // Return primitives and functions directly. Primitives are immutable, and
  // proxied functions are impossible to compare against originals, e.g. with
  // `assert.deepEqual()`.
  if (original === null || typeof original !== 'object') {
    return original;
  }

  const cachedProxy = _mustNotMutateObjectDeepProxies.get(original);
  if (cachedProxy) {
    return cachedProxy;
  }

  const _mustNotMutateObjectDeepHandler = {
    __proto__: null,
    defineProperty(target, property, descriptor) {
      assert.fail(`Expected no side effects, got ${inspect(property)} ` +
                  'defined');
    },
    deleteProperty(target, property) {
      assert.fail(`Expected no side effects, got ${inspect(property)} ` +
                  'deleted');
    },
    get(target, prop, receiver) {
      return mustNotMutateObjectDeep(Reflect.get(target, prop, receiver));
    },
    preventExtensions(target) {
      assert.fail('Expected no side effects, got extensions prevented on ' +
                  inspect(target));
    },
    set(target, property, value, receiver) {
      assert.fail(`Expected no side effects, got ${inspect(value)} ` +
                  `assigned to ${inspect(property)}`);
    },
    setPrototypeOf(target, prototype) {
      assert.fail(`Expected no side effects, got set prototype to ${prototype}`);
    },
  };

  const proxy = new Proxy(original, _mustNotMutateObjectDeepHandler);
  _mustNotMutateObjectDeepProxies.set(original, proxy);
  return proxy;
}

function printSkipMessage(msg) {
  console.log(`1..0 # Skipped: ${msg}`);
}

function skip(msg) {
  printSkipMessage(msg);
  // In known_issues test, skipping should produce a non-zero exit code.
  process.exit(require.main?.filename.startsWith(path.resolve(__dirname, '../known_issues/')) ? 1 : 0);
}

// Returns true if the exit code "exitCode" and/or signal name "signal"
// represent the exit code and/or signal name of a node process that aborted,
// false otherwise.
function nodeProcessAborted(exitCode, signal) {
  // Depending on the compiler used, node will exit with either
  // exit code 132 (SIGILL), 133 (SIGTRAP) or 134 (SIGABRT).
  let expectedExitCodes = [132, 133, 134];

  // On platforms using KSH as the default shell (like SmartOS),
  // when a process aborts, KSH exits with an exit code that is
  // greater than 256, and thus the exit code emitted with the 'exit'
  // event is null and the signal is set to either SIGILL, SIGTRAP,
  // or SIGABRT (depending on the compiler).
  const expectedSignals = ['SIGILL', 'SIGTRAP', 'SIGABRT'];

  // On Windows, 'aborts' are of 2 types, depending on the context:
  // (i) Exception breakpoint, if --abort-on-uncaught-exception is on
  // which corresponds to exit code 2147483651 (0x80000003)
  // (ii) Otherwise, _exit(134) which is called in place of abort() due to
  // raising SIGABRT exiting with ambiguous exit code '3' by default
  if (isWindows)
    expectedExitCodes = [0x80000003, 134];

  // When using --abort-on-uncaught-exception, V8 will use
  // base::OS::Abort to terminate the process.
  // Depending on the compiler used, the shell or other aspects of
  // the platform used to build the node binary, this will actually
  // make V8 exit by aborting or by raising a signal. In any case,
  // one of them (exit code or signal) needs to be set to one of
  // the expected exit codes or signals.
  if (signal !== null) {
    return expectedSignals.includes(signal);
  }
  return expectedExitCodes.includes(exitCode);
}

function isAlive(pid) {
  try {
    process.kill(pid, 'SIGCONT');
    return true;
  } catch {
    return false;
  }
}

function _expectWarning(name, expected, code) {
  if (typeof expected === 'string') {
    expected = [[expected, code]];
  } else if (!Array.isArray(expected)) {
    expected = Object.entries(expected).map(([a, b]) => [b, a]);
  } else if (expected.length !== 0 && !Array.isArray(expected[0])) {
    expected = [[expected[0], expected[1]]];
  }
  // Deprecation codes are mandatory, everything else is not.
  if (name === 'DeprecationWarning') {
    expected.forEach(([_, code]) => assert(code, `Missing deprecation code: ${expected}`));
  }
  return mustCall((warning) => {
    const expectedProperties = expected.shift();
    if (!expectedProperties) {
      assert.fail(`Unexpected extra warning received: ${warning}`);
    }
    const [ message, code ] = expectedProperties;
    assert.strictEqual(warning.name, name);
    if (typeof message === 'string') {
      assert.strictEqual(warning.message, message);
    } else {
      assert.match(warning.message, message);
    }
    assert.strictEqual(warning.code, code);
  }, expected.length);
}

let catchWarning;

// Accepts a warning name and description or array of descriptions or a map of
// warning names to description(s) ensures a warning is generated for each
// name/description pair.
// The expected messages have to be unique per `expectWarning()` call.
function expectWarning(nameOrMap, expected, code) {
  if (catchWarning === undefined) {
    catchWarning = {};
    process.on('warning', (warning) => {
      if (!catchWarning[warning.name]) {
        throw new TypeError(
          `"${warning.name}" was triggered without being expected.\n` +
          inspect(warning),
        );
      }
      catchWarning[warning.name](warning);
    });
  }
  if (typeof nameOrMap === 'string') {
    catchWarning[nameOrMap] = _expectWarning(nameOrMap, expected, code);
  } else {
    Object.keys(nameOrMap).forEach((name) => {
      catchWarning[name] = _expectWarning(name, nameOrMap[name]);
    });
  }
}

// Useful for testing expected internal/error objects
function expectsError(validator, exact) {
  return mustCall((...args) => {
    if (args.length !== 1) {
      // Do not use `assert.strictEqual()` to prevent `inspect` from
      // always being called.
      assert.fail(`Expected one argument, got ${inspect(args)}`);
    }
    const error = args.pop();
    // The error message should be non-enumerable
    assert.strictEqual(Object.prototype.propertyIsEnumerable.call(error, 'message'), false);

    assert.throws(() => { throw error; }, validator);
    return true;
  }, exact);
}

function skipIfInspectorDisabled() {
  if (!process.features.inspector) {
    skip('V8 inspector is disabled');
  }
}

function skipIf32Bits() {
  if (bits < 64) {
    skip('The tested feature is not available in 32bit builds');
  }
}

function skipIfWorker() {
  if (!isMainThread) {
    skip('This test only works on a main thread');
  }
}

function getArrayBufferViews(buf) {
  const { buffer, byteOffset, byteLength } = buf;

  const out = [];

  const arrayBufferViews = [
    Int8Array,
    Uint8Array,
    Uint8ClampedArray,
    Int16Array,
    Uint16Array,
    Int32Array,
    Uint32Array,
    Float32Array,
    Float64Array,
    BigInt64Array,
    BigUint64Array,
    DataView,
  ];

  for (const type of arrayBufferViews) {
    const { BYTES_PER_ELEMENT = 1 } = type;
    if (byteLength % BYTES_PER_ELEMENT === 0) {
      out.push(new type(buffer, byteOffset, byteLength / BYTES_PER_ELEMENT));
    }
  }
  return out;
}

function getBufferSources(buf) {
  return [...getArrayBufferViews(buf), new Uint8Array(buf).buffer];
}

function getTTYfd() {
  // Do our best to grab a tty fd.
  const tty = require('tty');
  // Don't attempt fd 0 as it is not writable on Windows.
  // Ref: ef2861961c3d9e9ed6972e1e84d969683b25cf95
  const ttyFd = [1, 2, 4, 5].find(tty.isatty);
  if (ttyFd === undefined) {
    try {
      return fs.openSync('/dev/tty');
    } catch {
      // There aren't any tty fd's available to use.
      return -1;
    }
  }
  return ttyFd;
}

function runWithInvalidFD(func) {
  let fd = 1 << 30;
  // Get first known bad file descriptor. 1 << 30 is usually unlikely to
  // be an valid one.
  try {
    while (fs.fstatSync(fd--) && fd > 0);
  } catch {
    return func(fd);
  }

  printSkipMessage('Could not generate an invalid fd');
}

// A helper function to simplify checking for ERR_INVALID_ARG_TYPE output.
function invalidArgTypeHelper(input) {
  if (input == null) {
    return ` Received ${input}`;
  }
  if (typeof input === 'function') {
    return ` Received function ${input.name}`;
  }
  if (typeof input === 'object') {
    if (input.constructor?.name) {
      return ` Received an instance of ${input.constructor.name}`;
    }
    return ` Received ${inspect(input, { depth: -1 })}`;
  }

  let inspected = inspect(input, { colors: false });
  if (inspected.length > 28) { inspected = `${inspected.slice(inspected, 0, 25)}...`; }

  return ` Received type ${typeof input} (${inspected})`;
}

function skipIfDumbTerminal() {
  if (isDumbTerminal) {
    skip('skipping - dumb terminal');
  }
}

function gcUntil(name, condition) {
  if (typeof name === 'function') {
    condition = name;
    name = undefined;
  }
  return new Promise((resolve, reject) => {
    let count = 0;
    function gcAndCheck() {
      setImmediate(() => {
        count++;
        global.gc();
        if (condition()) {
          resolve();
        } else if (count < 10) {
          gcAndCheck();
        } else {
          reject(name === undefined ? undefined : 'Test ' + name + ' failed');
        }
      });
    }
    gcAndCheck();
  });
}

function requireNoPackageJSONAbove(dir = __dirname) {
  let possiblePackage = path.join(dir, '..', 'package.json');
  let lastPackage = null;
  while (possiblePackage !== lastPackage) {
    if (fs.existsSync(possiblePackage)) {
      assert.fail(
        'This test shouldn\'t load properties from a package.json above ' +
        `its file location. Found package.json at ${possiblePackage}.`);
    }
    lastPackage = possiblePackage;
    possiblePackage = path.join(possiblePackage, '..', '..', 'package.json');
  }
}

function spawnPromisified(...args) {
  let stderr = '';
  let stdout = '';

  const child = spawn(...args);
  child.stderr.setEncoding('utf8');
  child.stderr.on('data', (data) => { stderr += data; });
  child.stdout.setEncoding('utf8');
  child.stdout.on('data', (data) => { stdout += data; });

  return new Promise((resolve, reject) => {
    child.on('close', (code, signal) => {
      resolve({
        code,
        signal,
        stderr,
        stdout,
      });
    });
    child.on('error', (code, signal) => {
      reject({
        code,
        signal,
        stderr,
        stdout,
      });
    });
  });
}

function getPrintedStackTrace(stderr) {
  const lines = stderr.split('\n');

  let state = 'initial';
  const result = {
    message: [],
    nativeStack: [],
    jsStack: [],
  };
  for (let i = 0; i < lines.length; ++i) {
    const line = lines[i].trim();
    if (line.length === 0) {
      continue;  // Skip empty lines.
    }

    switch (state) {
      case 'initial':
        result.message.push(line);
        if (line.includes('Native stack trace')) {
          state = 'native-stack';
        } else {
          result.message.push(line);
        }
        break;
      case 'native-stack':
        if (line.includes('JavaScript stack trace')) {
          state = 'js-stack';
        } else {
          result.nativeStack.push(line);
        }
        break;
      case 'js-stack':
        result.jsStack.push(line);
        break;
    }
  }
  return result;
}

/**
 * Check the exports of require(esm).
 * TODO(joyeecheung): use it in all the test-require-module-* tests to minimize changes
 * if/when we change the layout of the result returned by require(esm).
 * @param {object} mod result returned by require()
 * @param {object} expectation shape of expected namespace.
 */
function expectRequiredModule(mod, expectation, checkESModule = true) {
  const clone = { ...mod };
  if (Object.hasOwn(mod, 'default') && checkESModule) {
    assert.strictEqual(mod.__esModule, true);
    delete clone.__esModule;
  }
  assert(isModuleNamespaceObject(mod));
  assert.deepStrictEqual(clone, { ...expectation });
}

const common = {
  allowGlobals,
  buildType,
  canCreateSymLink,
  childShouldThrowAndAbort,
  createZeroFilledFile,
  defaultAutoSelectFamilyAttemptTimeout,
  expectsError,
  expectRequiredModule,
  expectWarning,
  gcUntil,
  getArrayBufferViews,
  getBufferSources,
  getCallSite,
  getPrintedStackTrace,
  getTTYfd,
  hasIntl,
  hasCrypto,
  hasOpenSSL,
  hasQuic,
  hasMultiLocalhost,
  invalidArgTypeHelper,
  isAlive,
  isASan,
  isDebug,
  isDumbTerminal,
  isFreeBSD,
  isLinux,
  isMainThread,
  isOpenBSD,
  isMacOS,
  isPi,
  isSunOS,
  isWindows,
  localIPv6Hosts,
  mustCall,
  mustCallAtLeast,
  mustNotCall,
  mustNotMutateObjectDeep,
  mustSucceed,
  nodeProcessAborted,
  PIPE,
  parseTestFlags,
  platformTimeout,
  printSkipMessage,
  pwdCommand,
  requireNoPackageJSONAbove,
  runWithInvalidFD,
  skip,
  skipIf32Bits,
  skipIfDumbTerminal,
  skipIfEslintMissing,
  skipIfInspectorDisabled,
  skipIfWorker,
  spawnPromisified,

  get enoughTestMem() {
    return require('os').totalmem() > 0x70000000; /* 1.75 Gb */
  },

  get hasFipsCrypto() {
    return hasCrypto && require('crypto').getFips();
  },

  get hasIPv6() {
    const iFaces = require('os').networkInterfaces();
    let re;
    if (isWindows) {
      re = /Loopback Pseudo-Interface/;
    } else if (this.isIBMi) {
      re = /\*LOOPBACK/;
    } else {
      re = /lo/;
    }
    return Object.keys(iFaces).some((name) => {
      return re.test(name) &&
             iFaces[name].some(({ family }) => family === 'IPv6');
    });
  },

  get hasOpenSSL3() {
    return hasOpenSSL(3);
  },

  get hasOpenSSL31() {
    return hasOpenSSL(3, 1);
  },

  get hasOpenSSL32() {
    return hasOpenSSL(3, 2);
  },

  get inFreeBSDJail() {
    if (inFreeBSDJail !== null) return inFreeBSDJail;

    if (exports.isFreeBSD &&
        execSync('sysctl -n security.jail.jailed').toString() === '1\n') {
      inFreeBSDJail = true;
    } else {
      inFreeBSDJail = false;
    }
    return inFreeBSDJail;
  },

  // On IBMi, process.platform and os.platform() both return 'aix',
  // when built with Python versions earlier than 3.9.
  // It is not enough to differentiate between IBMi and real AIX system.
  get isAIX() {
    return require('os').type() === 'AIX';
  },

  get isIBMi() {
    return require('os').type() === 'OS400';
  },

  get isLinuxPPCBE() {
    return (process.platform === 'linux') && (process.arch === 'ppc64') &&
           (require('os').endianness() === 'BE');
  },

  get localhostIPv4() {
    if (localhostIPv4 !== null) return localhostIPv4;

    if (this.inFreeBSDJail) {
      // Jailed network interfaces are a bit special - since we need to jump
      // through loops, as well as this being an exception case, assume the
      // user will provide this instead.
      if (process.env.LOCALHOST) {
        localhostIPv4 = process.env.LOCALHOST;
      } else {
        console.error('Looks like we\'re in a FreeBSD Jail. ' +
                      'Please provide your default interface address ' +
                      'as LOCALHOST or expect some tests to fail.');
      }
    }

    if (localhostIPv4 === null) localhostIPv4 = '127.0.0.1';

    return localhostIPv4;
  },

  // opensslCli defined lazily to reduce overhead of spawnSync
  get opensslCli() {
    if (opensslCli !== null) return opensslCli;

    if (process.config.variables.node_shared_openssl) {
      // Use external command
      opensslCli = 'openssl';
    } else {
      // Use command built from sources included in Node.js repository
      opensslCli = path.join(path.dirname(process.execPath), 'openssl-cli');
    }

    if (exports.isWindows) opensslCli += '.exe';

    const opensslCmd = spawnSync(opensslCli, ['version']);
    if (opensslCmd.status !== 0 || opensslCmd.error !== undefined) {
      // OpenSSL command cannot be executed
      opensslCli = false;
    }
    return opensslCli;
  },

  get PORT() {
    if (+process.env.TEST_PARALLEL) {
      throw new Error('common.PORT cannot be used in a parallelized test');
    }
    return +process.env.NODE_COMMON_PORT || 12346;
  },

  /**
   * Returns the EOL character used by this Git checkout.
   */
  get checkoutEOL() {
    return fs.readFileSync(__filename).includes('\r\n') ? '\r\n' : '\n';
  },
};

const validProperties = new Set(Object.keys(common));
module.exports = new Proxy(common, {
  get(obj, prop) {
    if (!validProperties.has(prop))
      throw new Error(`Using invalid common property: '${prop}'`);
    return obj[prop];
  },
});
