'use strict';

const {
  Error,
  ObjectDefineProperty,
  ObjectGetOwnPropertyDescriptor,
  ObjectSetPrototypeOf,
  SafeArrayIterator,
  SafeSet,
} = primordials;

const { BuiltinModule: { normalizeRequirableId } } = require('internal/bootstrap/realm');
const {
  compileSerializeMain,
  anonymousMainPath,
} = internalBinding('mksnapshot');

const { isExperimentalSeaWarningNeeded } = internalBinding('sea');

const { emitExperimentalWarning } = require('internal/util');
const { emitWarningSync } = require('internal/process/warning');

const {
  initializeCallbacks,
  namespace: {
    addDeserializeCallback,
    isBuildingSnapshot,
  },
  addAfterUserSerializeCallback,
} = require('internal/v8/startup_snapshot');

const {
  prepareMainThreadExecution,
} = require('internal/process/pre_execution');

const path = require('path');
const { getOptionValue } = require('internal/options');

const supportedModules = new SafeSet(new SafeArrayIterator([
  // '_http_agent',
  // '_http_client',
  // '_http_common',
  // '_http_incoming',
  // '_http_outgoing',
  // '_http_server',
  '_stream_duplex',
  '_stream_passthrough',
  '_stream_readable',
  '_stream_transform',
  '_stream_wrap',
  '_stream_writable',
  // '_tls_common',
  // '_tls_wrap',
  'assert',
  'assert/strict',
  // 'async_hooks',
  'buffer',
  // 'child_process',
  // 'cluster',
  'console',
  'constants',
  'crypto',
  // 'dgram',
  'diagnostics_channel',
  'dns',
  // 'dns/promises',
  // 'domain',
  'events',
  'fs',
  'fs/promises',
  // 'http',
  // 'http2',
  // 'https',
  // 'inspector',
  // 'module',
  'net',
  'os',
  'path',
  'path/posix',
  'path/win32',
  // 'perf_hooks',
  'process',
  'punycode',
  'querystring',
  // 'readline',
  // 'repl',
  'stream',
  'stream/promises',
  'string_decoder',
  'sys',
  'timers',
  'timers/promises',
  // 'tls',
  // 'trace_events',
  // 'tty',
  'url',
  'util',
  'util/types',
  'v8',
  // 'vm',
  // 'worker_threads',
  'zlib',
]));

const warnedModules = new SafeSet();
function supportedInUserSnapshot(id) {
  return supportedModules.has(id);
}

function requireForUserSnapshot(id) {
  const normalizedId = normalizeRequirableId(id);
  if (!normalizedId) {
    // eslint-disable-next-line no-restricted-syntax
    const err = new Error(
      `Cannot find module '${id}'. `,
    );
    err.code = 'MODULE_NOT_FOUND';
    throw err;
  }
  if (isBuildingSnapshot() && !supportedInUserSnapshot(normalizedId)) {
    if (!warnedModules.has(normalizedId)) {
      // Emit the warning synchronously in case we don't get to process
      // the tick and print it before the unsupported built-in causes a
      // crash.
      emitWarningSync(
        `It's not yet fully verified whether built-in module "${id}" ` +
        'works in user snapshot builder scripts.\n' +
        'It may still work in some cases, but in other cases certain ' +
        'run-time states may be out-of-sync after snapshot deserialization.\n' +
        'To request support for the module, use the Node.js issue tracker: ' +
        'https://github.com/nodejs/node/issues');
      warnedModules.add(normalizedId);
    }
  }

  return require(normalizedId);
}


function main() {
  prepareMainThreadExecution(false, false);
  initializeCallbacks();

  // In a context created for building snapshots, V8 does not install Error.stackTraceLimit and as
  // a result, if an error is created during the snapshot building process, error.stack would be
  // undefined. To prevent users from tripping over this, install Error.stackTraceLimit based on
  // --stack-trace-limit by ourselves (which defaults to 10).
  // See https://chromium-review.googlesource.com/c/v8/v8/+/3319481
  const initialStackTraceLimitDesc = {
    value: getOptionValue('--stack-trace-limit'),
    configurable: true,
    writable: true,
    enumerable: true,
    __proto__: null,
  };
  ObjectDefineProperty(Error, 'stackTraceLimit', initialStackTraceLimitDesc);

  let stackTraceLimitDescToRestore;
  // Error.stackTraceLimit needs to be removed during serialization, because when V8 deserializes
  // the snapshot, it expects Error.stackTraceLimit to be unset so that it can install it as a new property
  // using the value of --stack-trace-limit.
  addAfterUserSerializeCallback(() => {
    const desc = ObjectGetOwnPropertyDescriptor(Error, 'stackTraceLimit');

    // If it's modified by users, emit a warning.
    if (desc && (
      desc.value !== initialStackTraceLimitDesc.value ||
        desc.configurable !== initialStackTraceLimitDesc.configurable ||
        desc.writable !== initialStackTraceLimitDesc.writable ||
        desc.enumerable !== initialStackTraceLimitDesc.enumerable
    )) {
      process._rawDebug('Error.stackTraceLimit has been modified by the snapshot builder script.');
      // We want to use null-prototype objects to not rely on globally mutable
      // %Object.prototype%.
      if (desc.configurable) {
        stackTraceLimitDescToRestore = desc;
        ObjectSetPrototypeOf(stackTraceLimitDescToRestore, null);
        process._rawDebug('It will be preserved after snapshot deserialization and override ' +
                          '--stack-trace-limit passed into the deserialized application.\n' +
                          'To allow --stack-trace-limit override in the deserialized application, ' +
                          'delete Error.stackTraceLimit.');
      } else {
        process._rawDebug('It is not configurable and will crash the application upon deserialization.\n' +
                          'To fix the error, make Error.stackTraceLimit configurable.');
      }
    }

    delete Error.stackTraceLimit;
  });

  addDeserializeCallback(() => {
    if (stackTraceLimitDescToRestore) {
      ObjectDefineProperty(Error, 'stackTraceLimit', stackTraceLimitDescToRestore);
    }
  });

  // TODO(addaleax): Make this `embedderRunCjs` once require('module')
  // is supported in snapshots.
  function minimalRunCjs(source) {
    let filename;
    let dirname;
    if (process.argv[1] === anonymousMainPath) {
      filename = dirname = process.argv[1];
    } else {
      filename = path.resolve(process.argv[1]);
      dirname = path.dirname(filename);
    }

    const fn = compileSerializeMain(filename, source);
    return fn(requireForUserSnapshot, filename, dirname);
  }

  if (isExperimentalSeaWarningNeeded()) {
    emitExperimentalWarning('Single executable application');
  }

  return [process, requireForUserSnapshot, minimalRunCjs];
}

return main();
