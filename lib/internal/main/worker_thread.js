'use strict';

// In worker threads, execute the script sent through the
// message port.

const {
  ArrayPrototypeForEach,
  ArrayPrototypePushApply,
  ArrayPrototypeSplice,
  AtomicsLoad,
  ObjectDefineProperty,
  PromisePrototypeThen,
  RegExpPrototypeExec,
  SafeWeakMap,
  globalThis: {
    SharedArrayBuffer,
  },
} = primordials;

const {
  prepareWorkerThreadExecution,
  setupUserModules,
  markBootstrapComplete,
} = require('internal/process/pre_execution');

const {
  threadId,
  getEnvMessagePort,
} = internalBinding('worker');

const workerIo = require('internal/worker/io');
const {
  messageTypes: {
    // Messages that may be received by workers
    LOAD_SCRIPT,
    // Messages that may be posted from workers
    UP_AND_RUNNING,
    ERROR_MESSAGE,
    COULD_NOT_SERIALIZE_ERROR,
    // Messages that may be either received or posted
    STDIO_PAYLOAD,
    STDIO_WANTS_MORE_DATA,
  },
  kStdioWantsMoreDataCallback,
} = workerIo;

const { setupMainThreadPort } = require('internal/worker/messaging');

const {
  onGlobalUncaughtException,
  evalScript,
  evalTypeScript,
  evalModuleEntryPoint,
  parseAndEvalCommonjsTypeScript,
  parseAndEvalModuleTypeScript,
} = require('internal/process/execution');

let debug = require('internal/util/debuglog').debuglog('worker', (fn) => {
  debug = fn;
});

const assert = require('internal/assert');
const { getOptionValue } = require('internal/options');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');

prepareWorkerThreadExecution();

debug(`[${threadId}] is setting up worker child environment`);

// Set up the message port and start listening
const port = getEnvMessagePort();

// If the main thread is spawned with env NODE_CHANNEL_FD, it's probably
// spawned by our child_process module. In the work threads, mark the
// related IPC properties as unavailable.
if (process.env.NODE_CHANNEL_FD) {
  const workerThreadSetup = require('internal/process/worker_thread_only');
  ObjectDefineProperty(process, 'channel', {
    __proto__: null,
    enumerable: false,
    get: workerThreadSetup.unavailable('process.channel'),
  });

  ObjectDefineProperty(process, 'connected', {
    __proto__: null,
    enumerable: false,
    get: workerThreadSetup.unavailable('process.connected'),
  });

  process.send = workerThreadSetup.unavailable('process.send()');
  process.disconnect =
    workerThreadSetup.unavailable('process.disconnect()');
}

port.on('message', (message) => {
  if (message.type === LOAD_SCRIPT) {
    port.unref();
    const {
      argv,
      cwdCounter,
      doEval,
      environmentData,
      filename,
      hasStdin,
      publicPort,
      workerData,
      mainThreadPort,
    } = message;

    if (doEval !== 'internal') {
      if (argv !== undefined) {
        ArrayPrototypePushApply(process.argv, argv);
      }

      const publicWorker = require('worker_threads');
      publicWorker.parentPort = publicPort;
      publicWorker.workerData = workerData;
    }

    require('internal/worker').assignEnvironmentData(environmentData);
    setupMainThreadPort(mainThreadPort);

    if (SharedArrayBuffer !== undefined) {
      // The counter is only passed to the workers created by the main thread,
      // not to workers created by other workers.
      let cachedCwd = '';
      let lastCounter = -1;
      const originalCwd = process.cwd;

      process.cwd = function() {
        const currentCounter = AtomicsLoad(cwdCounter, 0);
        if (currentCounter === lastCounter)
          return cachedCwd;
        lastCounter = currentCounter;
        cachedCwd = originalCwd();
        return cachedCwd;
      };
      workerIo.sharedCwdCounter = cwdCounter;
    }

    const isLoaderWorker =
      doEval === 'internal' &&
      filename === require('internal/modules/esm/utils').loaderWorkerId;
    // Disable custom loaders in loader worker.
    setupUserModules(isLoaderWorker);

    if (!hasStdin)
      process.stdin.push(null);

    debug(`[${threadId}] starts worker script ${filename} ` +
          `(eval = ${doEval}) at cwd = ${process.cwd()}`);
    port.postMessage({ type: UP_AND_RUNNING });
    switch (doEval) {
      case 'internal': {
        // Create this WeakMap in js-land because V8 has no C++ API for WeakMap.
        internalBinding('module_wrap').callbackMap = new SafeWeakMap();
        require(filename)(workerData, publicPort);
        break;
      }

      case 'classic': if (getOptionValue('--input-type') !== 'module') {
        const name = '[worker eval]';
        // This is necessary for CJS module compilation.
        // TODO: pass this with something really internal.
        ObjectDefineProperty(process, '_eval', {
          __proto__: null,
          configurable: true,
          enumerable: true,
          value: filename,
        });
        ArrayPrototypeSplice(process.argv, 1, 0, name);
        const tsEnabled = getOptionValue('--experimental-strip-types');
        const inputType = getOptionValue('--input-type');

        if (inputType === 'module-typescript' && tsEnabled) {
          // This is a special case where we want to parse and eval the
          // TypeScript code as a module
          parseAndEvalModuleTypeScript(filename, false);
          break;
        }

        let evalFunction;
        if (inputType === 'commonjs') {
          evalFunction = evalScript;
        } else if (inputType === 'commonjs-typescript' && tsEnabled) {
          evalFunction = parseAndEvalCommonjsTypeScript;
        } else if (tsEnabled) {
          evalFunction = evalTypeScript;
        } else {
          // Default to commonjs.
          evalFunction = evalScript;
        }

        evalFunction(name, filename);
        break;
      }

      // eslint-disable-next-line no-fallthrough
      case 'module': {
        PromisePrototypeThen(evalModuleEntryPoint(filename), undefined, (e) => {
          workerOnGlobalUncaughtException(e, true);
        });
        break;
      }

      default: {
        // script filename
        // runMain here might be monkey-patched by users in --require.
        // XXX: the monkey-patchability here should probably be deprecated.
        ArrayPrototypeSplice(process.argv, 1, 0, filename);
        const CJSLoader = require('internal/modules/cjs/loader');
        CJSLoader.Module.runMain(filename);
        break;
      }
    }
  } else if (message.type === STDIO_PAYLOAD) {
    const { stream, chunks } = message;
    ArrayPrototypeForEach(chunks, ({ chunk, encoding }) => {
      process[stream].push(chunk, encoding);
    });
  } else {
    assert(
      message.type === STDIO_WANTS_MORE_DATA,
      `Unknown worker message type ${message.type}`,
    );
    const { stream } = message;
    process[stream][kStdioWantsMoreDataCallback]();
  }
});

function workerOnGlobalUncaughtException(error, fromPromise) {
  debug(`[${threadId}] gets uncaught exception`);
  let handled = false;
  let handlerThrew = false;
  try {
    handled = onGlobalUncaughtException(error, fromPromise);
  } catch (e) {
    error = e;
    handlerThrew = true;
  }
  debug(`[${threadId}] uncaught exception handled = ${handled}`);

  if (handled) {
    return true;
  }

  if (!process._exiting) {
    try {
      process._exiting = true;
      process.exitCode = kGenericUserError;
      if (!handlerThrew) {
        process.emit('exit', process.exitCode);
      }
    } catch {
      // Continue regardless of error.
    }
  }

  let serialized;
  try {
    const { serializeError } = require('internal/error_serdes');
    serialized = serializeError(error);
  } catch {
    // Continue regardless of error.
  }
  debug(`[${threadId}] uncaught exception serialized = ${!!serialized}`);
  if (serialized)
    port.postMessage({
      type: ERROR_MESSAGE,
      error: serialized,
    });
  else
    port.postMessage({ type: COULD_NOT_SERIALIZE_ERROR });

  const { clearAsyncIdStack } = require('internal/async_hooks');
  clearAsyncIdStack();

  process.exit();
}

// Patch the global uncaught exception handler so it gets picked up by
// node::errors::TriggerUncaughtException().
process._fatalException = workerOnGlobalUncaughtException;

markBootstrapComplete();

// Necessary to reset RegExp statics before user code runs.
RegExpPrototypeExec(/^/, '');

port.start();
