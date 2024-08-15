'use strict';

const {
  ArrayPrototypePush,
  ArrayPrototypePushApply,
  AtomicsLoad,
  AtomicsWait,
  AtomicsWaitAsync,
  Int32Array,
  ObjectAssign,
  ObjectDefineProperty,
  ObjectSetPrototypeOf,
  Promise,
  ReflectSet,
  SafeSet,
  StringPrototypeSlice,
  StringPrototypeToUpperCase,
  globalThis,
} = primordials;

const {
  SharedArrayBuffer,
} = globalThis;

const {
  ERR_INTERNAL_ASSERTION,
  ERR_INVALID_ARG_TYPE,
  ERR_INVALID_ARG_VALUE,
  ERR_INVALID_RETURN_PROPERTY_VALUE,
  ERR_INVALID_RETURN_VALUE,
  ERR_LOADER_CHAIN_INCOMPLETE,
  ERR_METHOD_NOT_IMPLEMENTED,
  ERR_WORKER_UNSERIALIZABLE_ERROR,
} = require('internal/errors').codes;
const { exitCodes: { kUnsettledTopLevelAwait } } = internalBinding('errors');
const { URLParse } = require('internal/url');
const { canParse: URLCanParse } = internalBinding('url');
const { receiveMessageOnPort } = require('worker_threads');
const {
  isAnyArrayBuffer,
  isArrayBufferView,
} = require('internal/util/types');
const {
  validateObject,
  validateString,
} = require('internal/validators');
const {
  kEmptyObject,
} = require('internal/util');

const {
  defaultResolve,
  throwIfInvalidParentURL,
} = require('internal/modules/esm/resolve');
const {
  getDefaultConditions,
  loaderWorkerId,
} = require('internal/modules/esm/utils');
const { deserializeError } = require('internal/error_serdes');
const {
  SHARED_MEMORY_BYTE_LENGTH,
  WORKER_TO_MAIN_THREAD_NOTIFICATION,
} = require('internal/modules/esm/shared_constants');
let debug = require('internal/util/debuglog').debuglog('esm', (fn) => {
  debug = fn;
});
let importMetaInitializer;

let importAssertionAlreadyWarned = false;

function emitImportAssertionWarning() {
  if (!importAssertionAlreadyWarned) {
    importAssertionAlreadyWarned = true;
    process.emitWarning('Use `importAttributes` instead of `importAssertions`', 'ExperimentalWarning');
  }
}

function defineImportAssertionAlias(context) {
  return ObjectDefineProperty(context, 'importAssertions', {
    __proto__: null,
    configurable: true,
    get() {
      emitImportAssertionWarning();
      return this.importAttributes;
    },
    set(value) {
      emitImportAssertionWarning();
      return ReflectSet(this, 'importAttributes', value);
    },
  });
}

/**
 * @typedef {object} ExportedHooks
 * @property {Function} resolve Resolve hook.
 * @property {Function} load Load hook.
 */

/**
 * @typedef {object} KeyedHook
 * @property {Function} fn The hook function.
 * @property {URL['href']} url The URL of the module.
 * @property {KeyedHook?} next The next hook in the chain.
 */

// [2] `validate...()`s throw the wrong error

class Hooks {
  #chains = {
    /**
     * Phase 1 of 2 in ESM loading.
     * The output of the `resolve` chain of hooks is passed into the `load` chain of hooks.
     * @private
     * @property {KeyedHook[]} resolve Last-in-first-out collection of resolve hooks.
     */
    resolve: [
      {
        fn: defaultResolve,
        url: 'node:internal/modules/esm/resolve',
      },
    ],

    /**
     * Phase 2 of 2 in ESM loading.
     * @private
     * @property {KeyedHook[]} load Last-in-first-out collection of loader hooks.
     */
    load: [
      {
        fn: require('internal/modules/esm/load').defaultLoad,
        url: 'node:internal/modules/esm/load',
      },
    ],
  };

  // Cache URLs we've already validated to avoid repeated validation
  #validatedUrls = new SafeSet();

  allowImportMetaResolve = false;

  /**
   * Import and register custom/user-defined module loader hook(s).
   * @param {string} urlOrSpecifier
   * @param {string} parentURL
   * @param {any} [data] Arbitrary data to be passed from the custom
   * loader (user-land) to the worker.
   */
  async register(urlOrSpecifier, parentURL, data, isInternal) {
    const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
    const keyedExports = isInternal ?
      require(urlOrSpecifier) :
      await cascadedLoader.import(
        urlOrSpecifier,
        parentURL,
        kEmptyObject,
      );
    await this.addCustomLoader(urlOrSpecifier, keyedExports, data);
  }

  /**
   * Collect custom/user-defined module loader hook(s).
   * @param {string} url Custom loader specifier
   * @param {Record<string, unknown>} exports
   * @param {any} [data] Arbitrary data to be passed from the custom loader (user-land)
   * to the worker.
   * @returns {any | Promise<any>} User data, ignored unless it's a promise, in which case it will be awaited.
   */
  addCustomLoader(url, exports, data) {
    const {
      initialize,
      resolve,
      load,
    } = pluckHooks(exports);

    if (resolve) {
      const next = this.#chains.resolve[this.#chains.resolve.length - 1];
      ArrayPrototypePush(this.#chains.resolve, { __proto__: null, fn: resolve, url, next });
    }
    if (load) {
      const next = this.#chains.load[this.#chains.load.length - 1];
      ArrayPrototypePush(this.#chains.load, { __proto__: null, fn: load, url, next });
    }
    return initialize?.(data);
  }

  /**
   * Resolve the location of the module.
   *
   * Internally, this behaves like a backwards iterator, wherein the stack of
   * hooks starts at the top and each call to `nextResolve()` moves down 1 step
   * until it reaches the bottom or short-circuits.
   * @param {string} originalSpecifier The specified URL path of the module to
   *                                   be resolved.
   * @param {string} [parentURL] The URL path of the module's parent.
   * @param {ImportAttributes} [importAttributes] Attributes from the import
   *                                              statement or expression.
   * @returns {Promise<{ format: string, url: URL['href'] }>}
   */
  async resolve(
    originalSpecifier,
    parentURL,
    importAttributes = { __proto__: null },
  ) {
    throwIfInvalidParentURL(parentURL);

    const chain = this.#chains.resolve;
    const context = {
      conditions: getDefaultConditions(),
      importAttributes,
      parentURL,
    };
    const meta = {
      chainFinished: null,
      context,
      hookErrIdentifier: '',
      hookName: 'resolve',
      shortCircuited: false,
    };

    const validateArgs = (hookErrIdentifier, suppliedSpecifier, ctx) => {
      validateString(
        suppliedSpecifier,
        `${hookErrIdentifier} specifier`,
      ); // non-strings can be coerced to a URL string

      if (ctx) { validateObject(ctx, `${hookErrIdentifier} context`); }
    };
    const validateOutput = (hookErrIdentifier, output) => {
      if (typeof output !== 'object' || output === null) { // [2]
        throw new ERR_INVALID_RETURN_VALUE(
          'an object',
          hookErrIdentifier,
          output,
        );
      }
    };

    const nextResolve = nextHookFactory(chain[chain.length - 1], meta, { validateArgs, validateOutput });

    const resolution = await nextResolve(originalSpecifier, context);
    const { hookErrIdentifier } = meta; // Retrieve the value after all settled

    validateOutput(hookErrIdentifier, resolution);

    if (resolution?.shortCircuit === true) { meta.shortCircuited = true; }

    if (!meta.chainFinished && !meta.shortCircuited) {
      throw new ERR_LOADER_CHAIN_INCOMPLETE(hookErrIdentifier);
    }

    let resolvedImportAttributes;
    const {
      format,
      url,
    } = resolution;

    if (typeof url !== 'string') {
      // non-strings can be coerced to a URL string
      // validateString() throws a less-specific error
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'a URL string',
        hookErrIdentifier,
        'url',
        url,
      );
    }

    // Avoid expensive URL instantiation for known-good URLs
    if (!this.#validatedUrls.has(url)) {
      // No need to convert to string, since the type is already validated
      if (!URLCanParse(url)) {
        throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
          'a URL string',
          hookErrIdentifier,
          'url',
          url,
        );
      }

      this.#validatedUrls.add(url);
    }

    if (!('importAttributes' in resolution) && ('importAssertions' in resolution)) {
      emitImportAssertionWarning();
      resolvedImportAttributes = resolution.importAssertions;
    } else {
      resolvedImportAttributes = resolution.importAttributes;
    }

    if (
      resolvedImportAttributes != null &&
      typeof resolvedImportAttributes !== 'object'
    ) {
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'an object',
        hookErrIdentifier,
        'importAttributes',
        resolvedImportAttributes,
      );
    }

    if (
      format != null &&
      typeof format !== 'string' // [2]
    ) {
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'a string',
        hookErrIdentifier,
        'format',
        format,
      );
    }

    return {
      __proto__: null,
      format,
      importAttributes: resolvedImportAttributes,
      url,
    };
  }

  resolveSync(_originalSpecifier, _parentURL, _importAttributes) {
    throw new ERR_METHOD_NOT_IMPLEMENTED('resolveSync()');
  }

  /**
   * Provide source that is understood by one of Node's translators.
   *
   * Internally, this behaves like a backwards iterator, wherein the stack of
   * hooks starts at the top and each call to `nextLoad()` moves down 1 step
   * until it reaches the bottom or short-circuits.
   * @param {URL['href']} url The URL/path of the module to be loaded
   * @param {object} context Metadata about the module
   * @returns {Promise<{ format: ModuleFormat, source: ModuleSource }>}
   */
  async load(url, context = {}) {
    const chain = this.#chains.load;
    const meta = {
      chainFinished: null,
      context,
      hookErrIdentifier: '',
      hookName: 'load',
      shortCircuited: false,
    };

    const validateArgs = (hookErrIdentifier, nextUrl, ctx) => {
      if (typeof nextUrl !== 'string') {
        // Non-strings can be coerced to a URL string
        // validateString() throws a less-specific error
        throw new ERR_INVALID_ARG_TYPE(
          `${hookErrIdentifier} url`,
          'a URL string',
          nextUrl,
        );
      }

      // Avoid expensive URL instantiation for known-good URLs
      if (!this.#validatedUrls.has(nextUrl)) {
        // No need to convert to string, since the type is already validated
        if (!URLCanParse(nextUrl)) {
          throw new ERR_INVALID_ARG_VALUE(
            `${hookErrIdentifier} url`,
            nextUrl,
            'should be a URL string',
          );
        }

        this.#validatedUrls.add(nextUrl);
      }

      if (ctx) { validateObject(ctx, `${hookErrIdentifier} context`); }
    };
    const validateOutput = (hookErrIdentifier, output) => {
      if (typeof output !== 'object' || output === null) { // [2]
        throw new ERR_INVALID_RETURN_VALUE(
          'an object',
          hookErrIdentifier,
          output,
        );
      }
    };

    const nextLoad = nextHookFactory(chain[chain.length - 1], meta, { validateArgs, validateOutput });

    const loaded = await nextLoad(url, defineImportAssertionAlias(context));
    const { hookErrIdentifier } = meta; // Retrieve the value after all settled

    validateOutput(hookErrIdentifier, loaded);

    if (loaded?.shortCircuit === true) { meta.shortCircuited = true; }

    if (!meta.chainFinished && !meta.shortCircuited) {
      throw new ERR_LOADER_CHAIN_INCOMPLETE(hookErrIdentifier);
    }

    const {
      format,
      source,
    } = loaded;
    let responseURL = loaded.responseURL;

    if (responseURL === undefined) {
      responseURL = url;
    }

    let responseURLObj;
    if (typeof responseURL === 'string') {
      responseURLObj = URLParse(responseURL);
    }

    if (responseURLObj?.href !== responseURL) {
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'undefined or a fully resolved URL string',
        hookErrIdentifier,
        'responseURL',
        responseURL,
      );
    }

    if (format == null) {
      require('internal/modules/esm/load').throwUnknownModuleFormat(url, format);
    }

    if (typeof format !== 'string') { // [2]
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'a string',
        hookErrIdentifier,
        'format',
        format,
      );
    }

    if (
      source != null &&
      typeof source !== 'string' &&
      !isAnyArrayBuffer(source) &&
      !isArrayBufferView(source)
    ) {
      throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
        'a string, an ArrayBuffer, or a TypedArray',
        hookErrIdentifier,
        'source',
        source,
      );
    }

    return {
      __proto__: null,
      format,
      responseURL,
      source,
    };
  }

  forceLoadHooks() {
    // No-op
  }

  importMetaInitialize(meta, context, loader) {
    importMetaInitializer ??= require('internal/modules/esm/initialize_import_meta').initializeImportMeta;
    meta = importMetaInitializer(meta, context, loader);
    return meta;
  }
}
ObjectSetPrototypeOf(Hooks.prototype, null);

/**
 * There may be multiple instances of Hooks/HooksProxy, but there is only 1 Internal worker, so
 * there is only 1 MessageChannel.
 */
let MessageChannel;
class HooksProxy {
  /**
   * Shared memory. Always use Atomics method to read or write to it.
   * @type {Int32Array}
   */
  #lock;
  /**
   * The InternalWorker instance, which lets us communicate with the loader thread.
   */
  #worker;

  /**
   * The last notification ID received from the worker. This is used to detect
   * if the worker has already sent a notification before putting the main
   * thread to sleep, to avoid a race condition.
   * @type {number}
   */
  #workerNotificationLastId = 0;

  /**
   * Track how many async responses the main thread should expect.
   * @type {number}
   */
  #numberOfPendingAsyncResponses = 0;

  #isReady = false;

  constructor() {
    const { InternalWorker } = require('internal/worker');
    MessageChannel ??= require('internal/worker/io').MessageChannel;

    const lock = new SharedArrayBuffer(SHARED_MEMORY_BYTE_LENGTH);
    this.#lock = new Int32Array(lock);

    this.#worker = new InternalWorker(loaderWorkerId, {
      stderr: false,
      stdin: false,
      stdout: false,
      trackUnmanagedFds: false,
      workerData: {
        lock,
      },
    });
    this.#worker.unref(); // ! Allows the process to eventually exit.
    this.#worker.on('exit', process.exit);
  }

  waitForWorker() {
    if (!this.#isReady) {
      const { kIsOnline } = require('internal/worker');
      if (!this.#worker[kIsOnline]) {
        debug('wait for signal from worker');
        AtomicsWait(this.#lock, WORKER_TO_MAIN_THREAD_NOTIFICATION, 0);
        const response = this.#worker.receiveMessageSync();
        if (response == null || response.message.status === 'exit') { return; }

        // ! This line catches initialization errors in the worker thread.
        this.#unwrapMessage(response);
      }

      this.#isReady = true;
    }
  }

  /**
   * Invoke a remote method asynchronously.
   * @param {string} method Method to invoke
   * @param {any[]} [transferList] Objects in `args` to be transferred
   * @param  {any[]} args Arguments to pass to `method`
   * @returns {Promise<any>}
   */
  async makeAsyncRequest(method, transferList, ...args) {
    this.waitForWorker();

    MessageChannel ??= require('internal/worker/io').MessageChannel;
    const asyncCommChannel = new MessageChannel();

    // Pass work to the worker.
    debug('post async message to worker', { method, args, transferList });
    const finalTransferList = [asyncCommChannel.port2];
    if (transferList) {
      ArrayPrototypePushApply(finalTransferList, transferList);
    }
    this.#worker.postMessage({
      __proto__: null,
      method, args,
      port: asyncCommChannel.port2,
    }, finalTransferList);

    if (this.#numberOfPendingAsyncResponses++ === 0) {
      // On the next lines, the main thread will await a response from the worker thread that might
      // come AFTER the last task in the event loop has run its course and there would be nothing
      // left keeping the thread alive (and once the main thread dies, the whole process stops).
      // However we want to keep the process alive until the worker thread responds (or until the
      // event loop of the worker thread is also empty), so we ref the worker until we get all the
      // responses back.
      this.#worker.ref();
    }

    let response;
    do {
      debug('wait for async response from worker', { method, args });
      await AtomicsWaitAsync(this.#lock, WORKER_TO_MAIN_THREAD_NOTIFICATION, this.#workerNotificationLastId).value;
      this.#workerNotificationLastId = AtomicsLoad(this.#lock, WORKER_TO_MAIN_THREAD_NOTIFICATION);

      response = receiveMessageOnPort(asyncCommChannel.port1);
    } while (response == null);
    debug('got async response from worker', { method, args }, this.#lock);

    if (--this.#numberOfPendingAsyncResponses === 0) {
      // We got all the responses from the worker, its job is done (until next time).
      this.#worker.unref();
    }

    const body = this.#unwrapMessage(response);
    asyncCommChannel.port1.close();
    return body;
  }

  /**
   * Invoke a remote method synchronously.
   * @param {string} method Method to invoke
   * @param {any[]} [transferList] Objects in `args` to be transferred
   * @param  {any[]} args Arguments to pass to `method`
   * @returns {any}
   */
  makeSyncRequest(method, transferList, ...args) {
    this.waitForWorker();

    // Pass work to the worker.
    debug('post sync message to worker', { method, args, transferList });
    this.#worker.postMessage({ __proto__: null, method, args }, transferList);

    let response;
    do {
      debug('wait for sync response from worker', { method, args });
      // Sleep until worker responds.
      AtomicsWait(this.#lock, WORKER_TO_MAIN_THREAD_NOTIFICATION, this.#workerNotificationLastId);
      this.#workerNotificationLastId = AtomicsLoad(this.#lock, WORKER_TO_MAIN_THREAD_NOTIFICATION);

      response = this.#worker.receiveMessageSync();
    } while (response == null);
    debug('got sync response from worker', { method, args });
    if (response.message.status === 'never-settle') {
      process.exit(kUnsettledTopLevelAwait);
    } else if (response.message.status === 'exit') {
      process.exit(response.message.body);
    }
    return this.#unwrapMessage(response);
  }

  #unwrapMessage(response) {
    if (response.message.status === 'never-settle') {
      return new Promise(() => {});
    }
    const { status, body } = response.message;
    if (status === 'error') {
      if (body == null || typeof body !== 'object') { throw body; }
      if (body.serializationFailed || body.serialized == null) {
        throw new ERR_WORKER_UNSERIALIZABLE_ERROR();
      }

      // eslint-disable-next-line no-restricted-syntax
      throw deserializeError(body.serialized);
    } else {
      return body;
    }
  }

  #importMetaInitializer = require('internal/modules/esm/initialize_import_meta').initializeImportMeta;

  importMetaInitialize(meta, context, loader) {
    this.#importMetaInitializer(meta, context, loader);
  }
}
ObjectSetPrototypeOf(HooksProxy.prototype, null);

// TODO(JakobJingleheimer): Remove this when loaders go "stable".
let globalPreloadWarningWasEmitted = false;

/**
 * A utility function to pluck the hooks from a user-defined loader.
 * @param {import('./loader.js).ModuleExports} exports
 * @returns {ExportedHooks}
 */
function pluckHooks({
  globalPreload,
  initialize,
  resolve,
  load,
}) {
  const acceptedHooks = { __proto__: null };

  if (resolve) {
    acceptedHooks.resolve = resolve;
  }
  if (load) {
    acceptedHooks.load = load;
  }

  if (initialize) {
    acceptedHooks.initialize = initialize;
  } else if (globalPreload && !globalPreloadWarningWasEmitted) {
    process.emitWarning(
      '`globalPreload` has been removed; use `initialize` instead.',
      'UnsupportedWarning',
    );
    globalPreloadWarningWasEmitted = true;
  }

  return acceptedHooks;
}


/**
 * A utility function to iterate through a hook chain, track advancement in the
 * chain, and generate and supply the `next<HookName>` argument to the custom
 * hook.
 * @param {KeyedHook} current The (currently) first hook in the chain (this shifts
 * on every call).
 * @param {object} meta Properties that change as the current hook advances
 * along the chain.
 * @param {boolean} meta.chainFinished Whether the end of the chain has been
 * reached AND invoked.
 * @param {string} meta.hookErrIdentifier A user-facing identifier to help
 *  pinpoint where an error occurred. Ex "file:///foo.mjs 'resolve'".
 * @param {string} meta.hookName The kind of hook the chain is (ex 'resolve')
 * @param {boolean} meta.shortCircuited Whether a hook signaled a short-circuit.
 * @param {(hookErrIdentifier, hookArgs) => void} validate A wrapper function
 *  containing all validation of a custom loader hook's intermediary output. Any
 *  validation within MUST throw.
 * @returns {function next<HookName>(...hookArgs)} The next hook in the chain.
 */
function nextHookFactory(current, meta, { validateArgs, validateOutput }) {
  // First, prepare the current
  const { hookName } = meta;
  const {
    fn: hook,
    url: hookFilePath,
    next,
  } = current;

  // ex 'nextResolve'
  const nextHookName = `next${
    StringPrototypeToUpperCase(hookName[0]) +
    StringPrototypeSlice(hookName, 1)
  }`;

  let nextNextHook;
  if (next) {
    nextNextHook = nextHookFactory(next, meta, { validateArgs, validateOutput });
  } else {
    // eslint-disable-next-line func-name-matching
    nextNextHook = function chainAdvancedTooFar() {
      throw new ERR_INTERNAL_ASSERTION(
        `ESM custom loader '${hookName}' advanced beyond the end of the chain.`,
      );
    };
  }

  return ObjectDefineProperty(
    async (arg0 = undefined, context) => {
      // Update only when hook is invoked to avoid fingering the wrong filePath
      meta.hookErrIdentifier = `${hookFilePath} '${hookName}'`;

      validateArgs(`${meta.hookErrIdentifier} hook's ${nextHookName}()`, arg0, context);

      const outputErrIdentifier = `${hookFilePath} '${hookName}' hook's ${nextHookName}()`;

      // Set when next<HookName> is actually called, not just generated.
      if (!next) { meta.chainFinished = true; }

      if (context) { // `context` has already been validated, so no fancy check needed.
        ObjectAssign(meta.context, context);
      }

      const output = await hook(arg0, meta.context, nextNextHook);
      validateOutput(outputErrIdentifier, output);

      if (output?.shortCircuit === true) { meta.shortCircuited = true; }

      return output;
    },
    'name',
    { __proto__: null, value: nextHookName },
  );
}


exports.Hooks = Hooks;
exports.HooksProxy = HooksProxy;
