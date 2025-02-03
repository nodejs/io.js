'use strict';

const {
  ArrayPrototypeForEach,
  Date,
  DatePrototypeGetDate,
  DatePrototypeGetFullYear,
  DatePrototypeGetHours,
  DatePrototypeGetMinutes,
  DatePrototypeGetMonth,
  DatePrototypeGetSeconds,
  NumberParseInt,
  ObjectDefineProperty,
  ObjectFreeze,
  String,
  globalThis,
} = primordials;

const {
  getOptionValue,
  refreshOptions,
  getEmbedderOptions,
} = require('internal/options');
const { reconnectZeroFillToggle } = require('internal/buffer');
const {
  exposeLazyInterfaces,
  defineReplaceableLazyAttribute,
  setupCoverageHooks,
  emitExperimentalWarning,
  deprecate,
} = require('internal/util');

const {
  ERR_MISSING_OPTION,
  ERR_ACCESS_DENIED,
} = require('internal/errors').codes;
const assert = require('internal/assert');
const {
  namespace: {
    addSerializeCallback,
    isBuildingSnapshot,
  },
  runDeserializeCallbacks,
} = require('internal/v8/startup_snapshot');

function prepareMainThreadExecution(expandArgv1 = false, initializeModules = true) {
  return prepareExecution({
    expandArgv1,
    initializeModules,
    isMainThread: true,
  });
}

function prepareWorkerThreadExecution() {
  prepareExecution({
    expandArgv1: false,
    initializeModules: false,
    isMainThread: false,
  });
}

function prepareShadowRealmExecution() {
  // Patch the process object with legacy properties and normalizations.
  // Do not expand argv1 as it is not available in ShadowRealm.
  patchProcessObject(false);
  setupDebugEnv();

  // Disable custom loaders in ShadowRealm.
  setupUserModules(true);
  const {
    privateSymbols: {
      host_defined_option_symbol,
    },
  } = internalBinding('util');
  const {
    vm_dynamic_import_default_internal,
  } = internalBinding('symbols');

  // For ShadowRealm.prototype.importValue(), the referrer name is
  // always null, so the native ImportModuleDynamically() callback would
  // always fallback to look up the host-defined option from the
  // global object using host_defined_option_symbol. Using
  // vm_dynamic_import_default_internal as the host-defined option
  // instructs the JS-land importModuleDynamicallyCallback() to
  // proxy the request to defaultImportModuleDynamically().
  globalThis[host_defined_option_symbol] =
    vm_dynamic_import_default_internal;
}

function prepareExecution(options) {
  const { expandArgv1, initializeModules, isMainThread } = options;

  refreshRuntimeOptions();
  reconnectZeroFillToggle();

  // Patch the process object and get the resolved main entry point.
  const mainEntry = patchProcessObject(expandArgv1);
  setupTraceCategoryState();
  setupInspectorHooks();
  setupNetworkInspection();
  setupNavigator();
  setupWarningHandler();
  setupSQLite();
  setupQuic();
  setupWebStorage();
  setupWebsocket();
  setupEventsource();
  setupCodeCoverage();
  setupDebugEnv();
  // Process initial diagnostic reporting configuration, if present.
  initializeReport();

  // Load permission system API
  initializePermission();

  initializeSourceMapsHandlers();
  initializeDeprecations();

  require('internal/dns/utils').initializeDns();

  if (isMainThread) {
    assert(internalBinding('worker').isMainThread);
    // Worker threads will get the manifest in the message handler.

    // Print stack trace on `SIGINT` if option `--trace-sigint` presents.
    setupStacktracePrinterOnSigint();
    initializeReportSignalHandlers();  // Main-thread-only.
    initializeHeapSnapshotSignalHandlers();
    // If the process is spawned with env NODE_CHANNEL_FD, it's probably
    // spawned by our child_process module, then initialize IPC.
    // This attaches some internal event listeners and creates:
    // process.send(), process.channel, process.connected,
    // process.disconnect().
    setupChildProcessIpcChannel();
    // If this is a worker in cluster mode, start up the communication
    // channel. This needs to be done before any user code gets executed
    // (including preload modules).
    initializeClusterIPC();

    // TODO(joyeecheung): do this for worker threads as well.
    runDeserializeCallbacks();
  } else {
    assert(!internalBinding('worker').isMainThread);
    // The setup should be called in LOAD_SCRIPT message handler.
    assert(!initializeModules);
  }

  if (initializeModules) {
    setupUserModules();
  }

  return mainEntry;
}

function setupUserModules(forceDefaultLoader = false) {
  initializeCJSLoader();
  initializeESMLoader(forceDefaultLoader);
  const {
    hasStartedUserCJSExecution,
    hasStartedUserESMExecution,
  } = require('internal/modules/helpers');
  assert(!hasStartedUserCJSExecution());
  assert(!hasStartedUserESMExecution());
  if (getEmbedderOptions().hasEmbedderPreload) {
    runEmbedderPreload();
  }
  // Do not enable preload modules if custom loaders are disabled.
  // For example, loader workers are responsible for doing this themselves.
  // And preload modules are not supported in ShadowRealm as well.
  if (!forceDefaultLoader) {
    loadPreloadModules();
  }
  // Need to be done after --require setup.
  initializeFrozenIntrinsics();
}

function refreshRuntimeOptions() {
  refreshOptions();
}

/**
 * Patch the process object with legacy properties and normalizations.
 * Replace `process.argv[0]` with `process.execPath`, preserving the original `argv[0]` value as `process.argv0`.
 * Replace `process.argv[1]` with the resolved absolute file path of the entry point, if found.
 * @param {boolean} expandArgv1 - Whether to replace `process.argv[1]` with the resolved absolute file path of
 * the main entry point.
 */
function patchProcessObject(expandArgv1) {
  const binding = internalBinding('process_methods');
  binding.patchProcessObject(process);

  // Since we replace process.argv[0] below, preserve the original value in case the user needs it.
  ObjectDefineProperty(process, 'argv0', {
    __proto__: null,
    enumerable: true,
    // Only set it to true during snapshot building.
    configurable: isBuildingSnapshot(),
    value: process.argv[0],
  });

  process.exitCode = undefined;
  process._exiting = false;
  process.argv[0] = process.execPath;

  /** @type {string} */
  let mainEntry;
  // If requested, update process.argv[1] to replace whatever the user provided with the resolved absolute file path of
  // the entry point.
  if (expandArgv1 && process.argv[1] && process.argv[1][0] !== '-') {
    // Expand process.argv[1] into a full path.
    const path = require('path');
    try {
      mainEntry = path.resolve(process.argv[1]);
      process.argv[1] = mainEntry;
    } catch {
      // Continue regardless of error.
    }
  }

  // We need to initialize the global console here again with process.stdout
  // and friends for snapshot deserialization.
  const globalConsole = require('internal/console/global');
  const { initializeGlobalConsole } = require('internal/console/constructor');
  initializeGlobalConsole(globalConsole);

  // TODO(joyeecheung): most of these should be deprecated and removed,
  // except some that we need to be able to mutate during run time.
  addReadOnlyProcessAlias('_eval', '--eval');
  addReadOnlyProcessAlias('_print_eval', '--print');
  addReadOnlyProcessAlias('_syntax_check_only', '--check');
  addReadOnlyProcessAlias('_forceRepl', '--interactive');
  addReadOnlyProcessAlias('_preload_modules', '--require');
  addReadOnlyProcessAlias('noDeprecation', '--no-deprecation');
  addReadOnlyProcessAlias('noProcessWarnings', '--no-warnings');
  addReadOnlyProcessAlias('traceProcessWarnings', '--trace-warnings');
  addReadOnlyProcessAlias('throwDeprecation', '--throw-deprecation');
  addReadOnlyProcessAlias('profProcess', '--prof-process');
  addReadOnlyProcessAlias('traceDeprecation', '--trace-deprecation');
  addReadOnlyProcessAlias('_breakFirstLine', '--inspect-brk', false);
  addReadOnlyProcessAlias('_breakNodeFirstLine', '--inspect-brk-node', false);

  return mainEntry;
}

function addReadOnlyProcessAlias(name, option, enumerable = true) {
  const value = getOptionValue(option);
  if (value) {
    ObjectDefineProperty(process, name, {
      __proto__: null,
      writable: false,
      configurable: true,
      enumerable,
      value,
    });
  }
}

function setupWarningHandler() {
  const {
    onWarning,
    resetForSerialization,
  } = require('internal/process/warning');
  if (getOptionValue('--warnings') &&
    process.env.NODE_NO_WARNINGS !== '1') {
    process.on('warning', onWarning);

    // The code above would add the listener back during deserialization,
    // if applicable.
    if (isBuildingSnapshot()) {
      addSerializeCallback(() => {
        process.removeListener('warning', onWarning);
        resetForSerialization();
      });
    }
  }
}

// https://websockets.spec.whatwg.org/
function setupWebsocket() {
  if (getOptionValue('--no-experimental-websocket')) {
    delete globalThis.WebSocket;
    delete globalThis.CloseEvent;
  }
}

// https://html.spec.whatwg.org/multipage/server-sent-events.html
function setupEventsource() {
  if (!getOptionValue('--experimental-eventsource')) {
    delete globalThis.EventSource;
  }
}

// TODO(aduh95): move this to internal/bootstrap/web/* when the CLI flag is
//               removed.
function setupNavigator() {
  if (getEmbedderOptions().noBrowserGlobals ||
      getOptionValue('--no-experimental-global-navigator')) {
    return;
  }

  // https://html.spec.whatwg.org/multipage/system-state.html#the-navigator-object
  exposeLazyInterfaces(globalThis, 'internal/navigator', ['Navigator']);
  defineReplaceableLazyAttribute(globalThis, 'internal/navigator', ['navigator'], false);
}

function setupSQLite() {
  if (getOptionValue('--no-experimental-sqlite')) {
    return;
  }

  const { BuiltinModule } = require('internal/bootstrap/realm');
  BuiltinModule.allowRequireByUsers('sqlite');
}

function setupQuic() {
  if (!getOptionValue('--experimental-quic')) {
    return;
  }

  const { BuiltinModule } = require('internal/bootstrap/realm');
  BuiltinModule.allowRequireByUsers('quic');
}

function setupWebStorage() {
  if (getEmbedderOptions().noBrowserGlobals ||
      !getOptionValue('--experimental-webstorage')) {
    return;
  }

  // https://html.spec.whatwg.org/multipage/webstorage.html#webstorage
  exposeLazyInterfaces(globalThis, 'internal/webstorage', ['Storage']);
  defineReplaceableLazyAttribute(globalThis, 'internal/webstorage', [
    'localStorage', 'sessionStorage',
  ]);
}

function setupCodeCoverage() {
  // Resolve the coverage directory to an absolute path, and
  // overwrite process.env so that the original path gets passed
  // to child processes even when they switch cwd. Don't do anything if the
  // --experimental-test-coverage flag is present, as the test runner will
  // handle coverage.
  if (process.env.NODE_V8_COVERAGE &&
      !getOptionValue('--experimental-test-coverage')) {
    process.env.NODE_V8_COVERAGE =
      setupCoverageHooks(process.env.NODE_V8_COVERAGE);
  }
}

function setupStacktracePrinterOnSigint() {
  if (!getOptionValue('--trace-sigint')) {
    return;
  }
  const { SigintWatchdog } = require('internal/watchdog');

  const watchdog = new SigintWatchdog();
  watchdog.start();
}

function initializeReport() {
  ObjectDefineProperty(process, 'report', {
    __proto__: null,
    enumerable: true,
    configurable: true,
    get() {
      const { report } = require('internal/process/report');
      return report;
    },
  });
}

function setupDebugEnv() {
  require('internal/util/debuglog').initializeDebugEnv(process.env.NODE_DEBUG);
  if (getOptionValue('--expose-internals')) {
    require('internal/bootstrap/realm').BuiltinModule.exposeInternals();
  }
}

// This has to be called after initializeReport() is called
function initializeReportSignalHandlers() {
  if (getOptionValue('--report-on-signal')) {
    const { addSignalHandler } = require('internal/process/report');
    addSignalHandler();
  }
}

function initializeHeapSnapshotSignalHandlers() {
  const signal = getOptionValue('--heapsnapshot-signal');
  const diagnosticDir = getOptionValue('--diagnostic-dir');

  if (!signal)
    return;

  require('internal/validators').validateSignalName(signal);
  const { writeHeapSnapshot } = require('v8');

  function doWriteHeapSnapshot() {
    const heapSnapshotFilename = getHeapSnapshotFilename(diagnosticDir);
    writeHeapSnapshot(heapSnapshotFilename);
  }
  process.on(signal, doWriteHeapSnapshot);

  // The code above would add the listener back during deserialization,
  // if applicable.
  if (isBuildingSnapshot()) {
    addSerializeCallback(() => {
      process.removeListener(signal, doWriteHeapSnapshot);
    });
  }
}

function setupTraceCategoryState() {
  const { isTraceCategoryEnabled } = internalBinding('trace_events');
  const { toggleTraceCategoryState } = require('internal/process/per_thread');
  toggleTraceCategoryState(isTraceCategoryEnabled('node.async_hooks'));
}

function setupInspectorHooks() {
  // If Debugger.setAsyncCallStackDepth is sent during bootstrap,
  // we cannot immediately call into JS to enable the hooks, which could
  // interrupt the JS execution of bootstrap. So instead we save the
  // notification in the inspector agent if it's sent in the middle of
  // bootstrap, and process the notification later here.
  if (internalBinding('config').hasInspector) {
    const {
      enable,
      disable,
    } = require('internal/inspector_async_hook');
    internalBinding('inspector').registerAsyncHook(enable, disable);
  }
}

function setupNetworkInspection() {
  if (internalBinding('config').hasInspector && getOptionValue('--experimental-network-inspection')) {
    const {
      enable,
      disable,
    } = require('internal/inspector_network_tracking');
    internalBinding('inspector').setupNetworkTracking(enable, disable);
  }
}

// In general deprecations are initialized wherever the APIs are implemented,
// this is used to deprecate APIs implemented in C++ where the deprecation
// utilities are not easily accessible.
function initializeDeprecations() {
  const pendingDeprecation = getOptionValue('--pending-deprecation');

  // DEP0103: access to `process.binding('util').isX` type checkers
  // TODO(addaleax): Turn into a full runtime deprecation.
  const utilBinding = internalBinding('util');
  const types = require('internal/util/types');
  for (const name of [
    'isArrayBuffer',
    'isArrayBufferView',
    'isAsyncFunction',
    'isDataView',
    'isDate',
    'isExternal',
    'isMap',
    'isMapIterator',
    'isNativeError',
    'isPromise',
    'isRegExp',
    'isSet',
    'isSetIterator',
    'isTypedArray',
    'isUint8Array',
    'isAnyArrayBuffer',
  ]) {
    utilBinding[name] = pendingDeprecation ?
      deprecate(types[name],
                'Accessing native typechecking bindings of Node ' +
                'directly is deprecated. ' +
                `Please use \`util.types.${name}\` instead.`,
                'DEP0103') :
      types[name];
  }

  // TODO(joyeecheung): this is a legacy property exposed to process.
  // Now that we use the config binding to carry this information, remove
  // it from the process. We may consider exposing it properly in
  // process.features.
  const { noBrowserGlobals } = internalBinding('config');
  if (noBrowserGlobals) {
    ObjectDefineProperty(process, '_noBrowserGlobals', {
      __proto__: null,
      writable: false,
      enumerable: true,
      configurable: true,
      value: noBrowserGlobals,
    });
  }

  if (pendingDeprecation) {
    process.binding = deprecate(process.binding,
                                'process.binding() is deprecated. ' +
                                'Please use public APIs instead.', 'DEP0111');

    process._tickCallback = deprecate(process._tickCallback,
                                      'process._tickCallback() is deprecated',
                                      'DEP0134');
  }
}

function setupChildProcessIpcChannel() {
  if (process.env.NODE_CHANNEL_FD) {
    const fd = NumberParseInt(process.env.NODE_CHANNEL_FD, 10);
    assert(fd >= 0);

    // Make sure it's not accidentally inherited by child processes.
    delete process.env.NODE_CHANNEL_FD;

    const serializationMode =
      process.env.NODE_CHANNEL_SERIALIZATION_MODE || 'json';
    delete process.env.NODE_CHANNEL_SERIALIZATION_MODE;

    require('child_process')._forkChild(fd, serializationMode);
    assert(process.send);
  }
}

function initializeClusterIPC() {
  if (process.argv[1] && process.env.NODE_UNIQUE_ID) {
    const cluster = require('cluster');
    cluster._setupWorker();
    // Make sure it's not accidentally inherited by child processes.
    delete process.env.NODE_UNIQUE_ID;
  }
}

function initializePermission() {
  const permission = getOptionValue('--permission');
  if (permission) {
    process.binding = function binding(_module) {
      throw new ERR_ACCESS_DENIED('process.binding');
    };
    // Guarantee path module isn't monkey-patched to bypass permission model
    ObjectFreeze(require('path'));
    const { has } = require('internal/process/permission');
    const warnFlags = [
      '--allow-addons',
      '--allow-child-process',
      '--allow-wasi',
      '--allow-worker',
    ];
    for (const flag of warnFlags) {
      if (getOptionValue(flag)) {
        process.emitWarning(
          `The flag ${flag} must be used with extreme caution. ` +
        'It could invalidate the permission model.', 'SecurityWarning');
      }
    }
    const warnCommaFlags = [
      '--allow-fs-read',
      '--allow-fs-write',
    ];
    for (const flag of warnCommaFlags) {
      const value = getOptionValue(flag);
      if (value.length === 1 && value[0].includes(',')) {
        process.emitWarning(
          `The ${flag} CLI flag has changed. ` +
        'Passing a comma-separated list of paths is no longer valid. ' +
        'Documentation can be found at ' +
        'https://nodejs.org/api/permissions.html#file-system-permissions',
          'Warning',
        );
      }
    }

    ObjectDefineProperty(process, 'permission', {
      __proto__: null,
      enumerable: true,
      configurable: false,
      value: {
        has,
      },
    });
  } else {
    const availablePermissionFlags = [
      '--allow-fs-read',
      '--allow-fs-write',
      '--allow-addons',
      '--allow-child-process',
      '--allow-wasi',
      '--allow-worker',
    ];
    ArrayPrototypeForEach(availablePermissionFlags, (flag) => {
      const value = getOptionValue(flag);
      if (value.length) {
        throw new ERR_MISSING_OPTION('--permission');
      }
    });
  }
}

function initializeCJSLoader() {
  const { initializeCJS } = require('internal/modules/cjs/loader');
  initializeCJS();
}

function initializeESMLoader(forceDefaultLoader) {
  const { initializeESM } = require('internal/modules/esm/utils');
  initializeESM(forceDefaultLoader);

  // Patch the vm module when --experimental-vm-modules is on.
  // Please update the comments in vm.js when this block changes.
  if (getOptionValue('--experimental-vm-modules')) {
    const {
      Module, SourceTextModule, SyntheticModule,
    } = require('internal/vm/module');
    const vm = require('vm');
    vm.Module = Module;
    vm.SourceTextModule = SourceTextModule;
    vm.SyntheticModule = SyntheticModule;
  }
}

function initializeSourceMapsHandlers() {
  const {
    setSourceMapsSupport,
  } = require('internal/source_map/source_map_cache');
  const enabled = getOptionValue('--enable-source-maps');
  setSourceMapsSupport(enabled, {
    __proto__: null,
    // TODO(legendecas): In order to smoothly improve the source map support,
    // skip source maps in node_modules and generated code with
    // `--enable-source-maps` in a semver major version.
    nodeModules: enabled,
    generatedCode: enabled,
  });
}

function initializeFrozenIntrinsics() {
  if (getOptionValue('--frozen-intrinsics')) {
    emitExperimentalWarning('Frozen intristics');
    require('internal/freeze_intrinsics')();
  }
}

function runEmbedderPreload() {
  internalBinding('mksnapshot').runEmbedderPreload(process, require);
}

function loadPreloadModules() {
  // For user code, we preload modules if `-r` is passed
  const preloadModules = getOptionValue('--require');
  if (preloadModules && preloadModules.length > 0) {
    const {
      Module: {
        _preloadModules,
      },
    } = require('internal/modules/cjs/loader');
    _preloadModules(preloadModules);
  }
}

function markBootstrapComplete() {
  internalBinding('performance').markBootstrapComplete();
}

// Sequence number for diagnostic filenames
let sequenceNumOfheapSnapshot = 0;

// To generate the HeapSnapshotFilename while using custom diagnosticDir
function getHeapSnapshotFilename(diagnosticDir) {
  if (!diagnosticDir) return undefined;

  const date = new Date();

  const year = DatePrototypeGetFullYear(date);
  const month = String(DatePrototypeGetMonth(date) + 1).padStart(2, '0');
  const day = String(DatePrototypeGetDate(date)).padStart(2, '0');
  const hours = String(DatePrototypeGetHours(date)).padStart(2, '0');
  const minutes = String(DatePrototypeGetMinutes(date)).padStart(2, '0');
  const seconds = String(DatePrototypeGetSeconds(date)).padStart(2, '0');

  const dateString = `${year}${month}${day}`;
  const timeString = `${hours}${minutes}${seconds}`;
  const pid = process.pid;
  const threadId = internalBinding('worker').threadId;
  const fileSequence = (++sequenceNumOfheapSnapshot).toString().padStart(3, '0');

  return `${diagnosticDir}/Heap.${dateString}.${timeString}.${pid}.${threadId}.${fileSequence}.heapsnapshot`;
}

module.exports = {
  setupUserModules,
  prepareMainThreadExecution,
  prepareWorkerThreadExecution,
  prepareShadowRealmExecution,
  markBootstrapComplete,
  loadPreloadModules,
  initializeFrozenIntrinsics,
};
