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
    addSerializeCallback,
    addDeserializeCallback,
    isBuildingSnapshot,
  },
} = require('internal/v8/startup_snapshot');

const {
  prepareMainThreadExecution,
} = require('internal/process/pre_execution');

const path = require('path');

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

  let stackTraceLimitDesc;
  addDeserializeCallback(() => {
    if (stackTraceLimitDesc !== undefined) {
      ObjectDefineProperty(Error, 'stackTraceLimit', stackTraceLimitDesc);
    }
  });
  addSerializeCallback(() => {
    stackTraceLimitDesc = ObjectGetOwnPropertyDescriptor(Error, 'stackTraceLimit');

    if (stackTraceLimitDesc !== undefined) {
      // We want to use null-prototype objects to not rely on globally mutable
      // %Object.prototype%.
      ObjectSetPrototypeOf(stackTraceLimitDesc, null);
      process._rawDebug('Deleting Error.stackTraceLimit from the snapshot. ' +
                        'It will be re-installed after deserialization');
      delete Error.stackTraceLimit;
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
