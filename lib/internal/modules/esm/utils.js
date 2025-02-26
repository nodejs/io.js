'use strict';

const {
  ArrayIsArray,
  ObjectFreeze,
  SafeSet,
  SafeWeakMap,
} = primordials;

const {
  privateSymbols: {
    host_defined_option_symbol,
  },
} = internalBinding('util');
const {
  source_text_module_default_hdo,
  vm_dynamic_import_default_internal,
  vm_dynamic_import_main_context_default,
  vm_dynamic_import_missing_flag,
  vm_dynamic_import_no_callback,
} = internalBinding('symbols');

const { ModuleWrap } = internalBinding('module_wrap');
const {
  maybeCacheSourceMap,
} = require('internal/source_map/source_map_cache');

const {
  ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING_FLAG,
  ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING,
  ERR_INVALID_ARG_VALUE,
} = require('internal/errors').codes;
const { getOptionValue } = require('internal/options');
const {
  loadPreloadModules,
  initializeFrozenIntrinsics,
} = require('internal/process/pre_execution');
const {
  emitExperimentalWarning,
  getCWDURL,
} = require('internal/util');
const {
  setImportModuleDynamicallyCallback,
  setInitializeImportMetaObjectCallback,
} = internalBinding('module_wrap');
const assert = require('internal/assert');
const {
  normalizeReferrerURL,
} = require('internal/modules/helpers');

let defaultConditions;
/**
 * Returns the default conditions for ES module loading.
 */
function getDefaultConditions() {
  assert(defaultConditions !== undefined);
  return defaultConditions;
}

/** @type {Set<string>} */
let defaultConditionsSet;
/**
 * Returns the default conditions for ES module loading, as a Set.
 */
function getDefaultConditionsSet() {
  assert(defaultConditionsSet !== undefined);
  return defaultConditionsSet;
}

/**
 * Initializes the default conditions for ESM module loading.
 * This function is called during pre-execution, before any user code is run.
 */
function initializeDefaultConditions() {
  const userConditions = getOptionValue('--conditions');
  const noAddons = getOptionValue('--no-addons');
  const addonConditions = noAddons ? [] : ['node-addons'];
  const moduleConditions = getOptionValue('--experimental-require-module') ? ['module-sync'] : [];
  defaultConditions = ObjectFreeze([
    'node',
    'import',
    ...moduleConditions,
    ...addonConditions,
    ...userConditions,
  ]);
  defaultConditionsSet = new SafeSet(defaultConditions);
}

/**
 * @param {string[]} [conditions]
 * @returns {Set<string>}
 */
function getConditionsSet(conditions) {
  if (conditions !== undefined && conditions !== getDefaultConditions()) {
    if (!ArrayIsArray(conditions)) {
      throw new ERR_INVALID_ARG_VALUE('conditions', conditions,
                                      'expected an array');
    }
    return new SafeSet(conditions);
  }
  return getDefaultConditionsSet();
}

/**
 * @typedef {{
 *   [Symbol.toStringTag]: 'Module',
 * }} ModuleNamespaceObject
 */

/**
 * @callback ImportModuleDynamicallyCallback
 * @param {string} specifier
 * @param {ModuleWrap|ContextifyScript|Function|vm.Module} callbackReferrer
 * @param {Record<string, string>} attributes
 * @returns {Promise<ModuleNamespaceObject>}
 */

/**
 * @callback InitializeImportMetaCallback
 * @param {object} meta
 * @param {ModuleWrap|ContextifyScript|Function|vm.Module} callbackReferrer
 */

/**
 * @typedef {{
 *   callbackReferrer: ModuleWrap|ContextifyScript|Function|vm.Module
 *   initializeImportMeta? : InitializeImportMetaCallback,
 *   importModuleDynamically? : ImportModuleDynamicallyCallback
 * }} ModuleRegistry
 */

/**
 * @type {WeakMap<symbol, ModuleRegistry>}
 */
const moduleRegistries = new SafeWeakMap();

/**
 * @typedef {ContextifyScript|Function|ModuleWrap|ContextifiedObject} Referrer
 * A referrer can be a Script Record, a Cyclic Module Record, or a Realm Record
 * as defined in https://tc39.es/ecma262/#sec-HostLoadImportedModule.
 *
 * In Node.js, a referrer is represented by a wrapper object of these records.
 * A referrer object has a field |host_defined_option_symbol| initialized with
 * a symbol.
 */

/**
 * V8 would make sure that as long as import() can still be initiated from
 * the referrer, the symbol referenced by |host_defined_option_symbol| should
 * be alive, which in term would keep the settings object alive through the
 * WeakMap, and in turn that keeps the referrer object alive, which would be
 * passed into the callbacks.
 * The reference goes like this:
 * [v8::internal::Script] (via host defined options) ----1--> [idSymbol]
 * [callbackReferrer] (via host_defined_option_symbol) ------2------^  |
 *                                 ^----------3---- (via WeakMap)------
 * 1+3 makes sure that as long as import() can still be initiated, the
 * referrer wrap is still around and can be passed into the callbacks.
 * 2 is only there so that we can get the id symbol to configure the
 * weak map.
 * @param {Referrer} referrer The referrer to
 *   get the id symbol from. This is different from callbackReferrer which
 *   could be set by the caller.
 * @param {ModuleRegistry} registry
 */
function registerModule(referrer, registry) {
  const idSymbol = referrer[host_defined_option_symbol];
  if (idSymbol === vm_dynamic_import_no_callback ||
      idSymbol === vm_dynamic_import_missing_flag ||
      idSymbol === vm_dynamic_import_main_context_default ||
      idSymbol === vm_dynamic_import_default_internal) {
    // The referrer is compiled without custom callbacks, so there is
    // no registry to hold on to. We'll throw
    // ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING when a callback is
    // needed.
    return;
  }
  // To prevent it from being GC'ed.
  registry.callbackReferrer ??= referrer;
  moduleRegistries.set(idSymbol, registry);
}

/**
 * Proxy the import meta handling to the default loader for source text modules.
 * @param {Record<string, string | Function>} meta - The import.meta object to initialize.
 * @param {ModuleWrap} wrap - The ModuleWrap of the SourceTextModule where `import.meta` is referenced.
 */
function defaultInitializeImportMetaForModule(meta, wrap) {
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  return cascadedLoader.importMetaInitialize(meta, { url: wrap.url });
}

/**
 * Defines the `import.meta` object for a given module.
 * @param {symbol} symbol - Reference to the module.
 * @param {Record<string, string | Function>} meta - The import.meta object to initialize.
 * @param {ModuleWrap} wrap - The ModuleWrap of the SourceTextModule where `import.meta` is referenced.
 */
function initializeImportMetaObject(symbol, meta, wrap) {
  if (symbol === source_text_module_default_hdo) {
    defaultInitializeImportMetaForModule(meta, wrap);
    return;
  }
  const data = moduleRegistries.get(symbol);
  assert(data, `import.meta registry not found for ${wrap.url}`);
  const { initializeImportMeta, callbackReferrer } = data;
  if (initializeImportMeta !== undefined) {
    meta = initializeImportMeta(meta, callbackReferrer);
  }
}

/**
 * Proxy the dynamic import handling to the default loader for source text modules.
 * @param {string} specifier - The module specifier string.
 * @param {Record<string, string>} attributes - The import attributes object.
 * @param {string|null|undefined} referrerName - name of the referrer.
 * @returns {Promise<import('internal/modules/esm/loader.js').ModuleExports>} - The imported module object.
 */
function defaultImportModuleDynamicallyForModule(specifier, attributes, referrerName) {
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  return cascadedLoader.import(specifier, referrerName, attributes);
}

/**
 * Proxy the dynamic import to the default loader for classic scripts.
 * @param {string} specifier - The module specifier string.
 * @param {Record<string, string>} attributes - The import attributes object.
 * @param {string|null|undefined} referrerName - name of the referrer.
 * @returns {Promise<import('internal/modules/esm/loader.js').ModuleExports>} - The imported module object.
 */
function defaultImportModuleDynamicallyForScript(specifier, attributes, referrerName) {
  const parentURL = normalizeReferrerURL(referrerName);
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  return cascadedLoader.import(specifier, parentURL, attributes);
}

/**
 * Asynchronously imports a module dynamically using a callback function. The native callback.
 * @param {symbol} referrerSymbol - Referrer symbol of the registered script, function, module, or contextified object.
 * @param {string} specifier - The module specifier string.
 * @param {Record<string, string>} attributes - The import attributes object.
 * @param {string|null|undefined} referrerName - name of the referrer.
 * @returns {Promise<import('internal/modules/esm/loader.js').ModuleExports>} - The imported module object.
 * @throws {ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING} - If the callback function is missing.
 */
async function importModuleDynamicallyCallback(referrerSymbol, specifier, attributes, referrerName) {
  // For user-provided vm.constants.USE_MAIN_CONTEXT_DEFAULT_LOADER, emit the warning
  // and fall back to the default loader.
  if (referrerSymbol === vm_dynamic_import_main_context_default) {
    emitExperimentalWarning('vm.USE_MAIN_CONTEXT_DEFAULT_LOADER');
    return defaultImportModuleDynamicallyForScript(specifier, attributes, referrerName);
  }
  // For script compiled internally that should use the default loader to handle dynamic
  // import, proxy the request to the default loader without the warning.
  if (referrerSymbol === vm_dynamic_import_default_internal) {
    return defaultImportModuleDynamicallyForScript(specifier, attributes, referrerName);
  }
  // For SourceTextModules compiled internally, proxy the request to the default loader.
  if (referrerSymbol === source_text_module_default_hdo) {
    return defaultImportModuleDynamicallyForModule(specifier, attributes, referrerName);
  }

  if (moduleRegistries.has(referrerSymbol)) {
    const { importModuleDynamically, callbackReferrer } = moduleRegistries.get(referrerSymbol);
    if (importModuleDynamically !== undefined) {
      return importModuleDynamically(specifier, callbackReferrer, attributes);
    }
  }
  if (referrerSymbol === vm_dynamic_import_missing_flag) {
    throw new ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING_FLAG();
  }
  throw new ERR_VM_DYNAMIC_IMPORT_CALLBACK_MISSING();
}

let _forceDefaultLoader = false;
/**
 * Initializes handling of ES modules.
 * This is configured during pre-execution. Specifically it's set to true for
 * the loader worker in internal/main/worker_thread.js.
 * @param {boolean} [forceDefaultLoader=false] - A boolean indicating disabling custom loaders.
 */
function initializeESM(forceDefaultLoader = false) {
  _forceDefaultLoader = forceDefaultLoader;
  initializeDefaultConditions();
  // Setup per-realm callbacks that locate data or callbacks that we keep
  // track of for different ESM modules.
  setInitializeImportMetaObjectCallback(initializeImportMetaObject);
  setImportModuleDynamicallyCallback(importModuleDynamicallyCallback);
}

/**
 * Determine whether custom loaders are disabled and it is forced to use the
 * default loader.
 * @returns {boolean}
 */
function forceDefaultLoader() {
  return _forceDefaultLoader;
}

/**
 * Register module customization hooks.
 */
async function initializeHooks() {
  const customLoaderURLs = getOptionValue('--experimental-loader');

  const { Hooks } = require('internal/modules/esm/hooks');
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();

  const hooks = new Hooks();
  cascadedLoader.setCustomizations(hooks);

  // We need the loader customizations to be set _before_ we start invoking
  // `--require`, otherwise loops can happen because a `--require` script
  // might call `register(...)` before we've installed ourselves. These
  // global values are magically set in `setupUserModules` just for us and
  // we call them in the correct order.
  // N.B.  This block appears here specifically in order to ensure that
  // `--require` calls occur before `--loader` ones do.
  loadPreloadModules();
  initializeFrozenIntrinsics();

  const parentURL = getCWDURL().href;
  for (let i = 0; i < customLoaderURLs.length; i++) {
    await hooks.register(
      customLoaderURLs[i],
      parentURL,
    );
  }

  return hooks;
}

/**
 * Compile a SourceTextModule for the built-in ESM loader. Register it for default
 * source map and import.meta and dynamic import() handling if cascadedLoader is provided.
 * @param {string} url URL of the module.
 * @param {string} source Source code of the module.
 * @param {typeof import('./loader.js').ModuleLoader|undefined} cascadedLoader If provided,
 *        register the module for default handling.
 * @returns {ModuleWrap}
 */
function compileSourceTextModule(url, source, cascadedLoader) {
  const hostDefinedOption = cascadedLoader ? source_text_module_default_hdo : undefined;
  const wrap = new ModuleWrap(url, undefined, source, 0, 0, hostDefinedOption);

  if (!cascadedLoader) {
    return wrap;
  }
  // Cache the source map for the module if present.
  if (wrap.sourceMapURL) {
    maybeCacheSourceMap(url, source, wrap, false, wrap.sourceURL, wrap.sourceMapURL);
  }
  return wrap;
}

module.exports = {
  registerModule,
  initializeESM,
  initializeHooks,
  getDefaultConditions,
  getConditionsSet,
  loaderWorkerId: 'internal/modules/esm/worker',
  forceDefaultLoader,
  compileSourceTextModule,
};
