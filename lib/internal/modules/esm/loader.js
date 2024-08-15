'use strict';

const {
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypeReduce,
  FunctionPrototypeCall,
  JSONStringify,
  ObjectSetPrototypeOf,
  RegExpPrototypeSymbolReplace,
  encodeURIComponent,
  hardenRegExp,
} = primordials;


// This is needed to avoid cycles in esm/resolve <-> cjs/loader
const {
  kIsExecuting,
  kRequiredModuleSymbol,
} = require('internal/modules/cjs/loader');

const { imported_cjs_symbol } = internalBinding('symbols');

const assert = require('internal/assert');
const {
  ERR_REQUIRE_CYCLE_MODULE,
  ERR_REQUIRE_ESM,
  ERR_NETWORK_IMPORT_DISALLOWED,
  ERR_UNKNOWN_MODULE_FORMAT,
} = require('internal/errors').codes;
const { getOptionValue } = require('internal/options');
const { isURL, pathToFileURL, URLParse } = require('internal/url');
const { kEmptyObject } = require('internal/util');
const {
  compileSourceTextModule,
  getDefaultConditions,
} = require('internal/modules/esm/utils');
const { kImplicitTypeAttribute } = require('internal/modules/esm/assert');
const { ModuleWrap, kEvaluating, kEvaluated } = internalBinding('module_wrap');
const {
  urlToFilename,
} = require('internal/modules/helpers');
let defaultResolve, defaultLoad, defaultLoadSync, importMetaInitializer;

const { tracingChannel } = require('diagnostics_channel');
const onImport = tracingChannel('module.import');

/**
 * @typedef {import('url').URL} URL
 */

/**
 * Lazy loads the module_map module and returns a new instance of ResolveCache.
 * @returns {import('./module_map.js').ResolveCache}
 */
function newResolveCache() {
  const { ResolveCache } = require('internal/modules/esm/module_map');
  return new ResolveCache();
}

/**
 * Generate a load cache (to store the final result of a load-chain for a particular module).
 * @returns {import('./module_map.js').LoadCache}
 */
function newLoadCache() {
  const { LoadCache } = require('internal/modules/esm/module_map');
  return new LoadCache();
}

/**
 * Lazy-load translators to avoid potentially unnecessary work at startup (ex if ESM is not used).
 * @returns {import('./translators.js').Translators}
 */
function getTranslators() {
  const { translators } = require('internal/modules/esm/translators');
  return translators;
}

/**
 * @type {HooksProxy}
 * Multiple loader instances exist for various, specific reasons (see code comments at site).
 * In order to maintain consistency, we use a single worker (sandbox), which must sit apart of an
 * individual loader instance.
 */
let hooksProxy;

/**
 * @typedef {import('../cjs/loader.js').Module} CJSModule
 */

/**
 * @typedef {Record<string, any>} ModuleExports
 */

/**
 * @typedef {'builtin'|'commonjs'|'json'|'module'|'wasm'} ModuleFormat
 */

/**
 * @typedef {ArrayBuffer|TypedArray|string} ModuleSource
 */

/**
 * This class covers the base machinery of module loading. To add custom
 * behavior you can pass a customizations object and this object will be
 * used to do the loading/resolving/registration process.
 */
class ModuleLoader {
  /**
   * The conditions for resolving packages if `--conditions` is not used.
   */
  #defaultConditions = getDefaultConditions();

  /**
   * Registry of resolved specifiers
   */
  #resolveCache = newResolveCache();

  /**
   * Registry of loaded modules, akin to `require.cache`
   */
  loadCache = newLoadCache();

  /**
   * Methods which translate input code or other information into ES modules
   */
  translators = getTranslators();

  /**
   * Truthy to allow the use of `import.meta.resolve`. This is needed
   * currently because the `Hooks` class does not have `resolveSync`
   * implemented and `import.meta.resolve` requires it.
   */
  allowImportMetaResolve;

  /**
   * Customizations to pass requests to.
   *
   * Note that this value _MUST_ be set with `setCustomizations`
   * because it needs to copy `customizations.allowImportMetaResolve`
   *  to this property and failure to do so will cause undefined
   * behavior when invoking `import.meta.resolve`.
   * @see {ModuleLoader.setCustomizations}
   */
  #customizations;

  constructor(customizations) {
    this.setCustomizations(customizations);
  }

  /**
   * Change the currently activate customizations for this module
   * loader to be the provided `customizations`.
   *
   * If present, this class customizes its core functionality to the
   * `customizations` object, including registration, loading, and resolving.
   * There are some responsibilities that this class _always_ takes
   * care of, like validating outputs, so that the customizations object
   * does not have to do so.
   *
   * The customizations object has the shape:
   *
   * ```ts
   * interface LoadResult {
   *   format: ModuleFormat;
   *   source: ModuleSource;
   * }
   *
   * interface ResolveResult {
   *   format: string;
   *   url: URL['href'];
   * }
   *
   * interface Customizations {
   *   allowImportMetaResolve: boolean;
   *   load(url: string, context: object): Promise<LoadResult>
   *   resolve(
   *     originalSpecifier:
   *     string, parentURL: string,
   *     importAttributes: Record<string, string>
   *   ): Promise<ResolveResult>
   *   resolveSync(
   *     originalSpecifier:
   *     string, parentURL: string,
   *     importAttributes: Record<string, string>
   *   ) ResolveResult;
   *   register(specifier: string, parentURL: string): any;
   *   forceLoadHooks(): void;
   * }
   * ```
   *
   * Note that this class _also_ implements the `Customizations`
   * interface, as does `CustomizedModuleLoader` and `Hooks`.
   *
   * Calling this function alters how modules are loaded and should be
   * invoked with care.
   * @param {object} customizations
   */
  setCustomizations(customizations) {
    this.#customizations = customizations;
    if (customizations) {
      this.allowImportMetaResolve = customizations.allowImportMetaResolve;
    } else {
      this.allowImportMetaResolve = true;
    }
  }

  async eval(source, url, isEntryPoint = false) {
    const evalInstance = (url) => {
      return compileSourceTextModule(url, source, this);
    };
    const { ModuleJob } = require('internal/modules/esm/module_job');
    const module = await onImport.tracePromise(async () => {
      const job = new ModuleJob(
        this, url, undefined, evalInstance, false, false);
      this.loadCache.set(url, undefined, job);
      const { module } = await job.run(isEntryPoint);
      return module;
    }, {
      __proto__: null,
      parentURL: '<eval>',
      url,
    });

    return {
      __proto__: null,
      namespace: module.getNamespace(),
      module,
    };
  }

  /**
   * Get a (possibly still pending) module job from the cache,
   * or create one and return its Promise.
   * @param {string} specifier The string after `from` in an `import` statement,
   *                           or the first parameter of an `import()`
   *                           expression
   * @param {string | undefined} parentURL The URL of the module importing this
   *                                     one, unless this is the Node.js entry
   *                                     point.
   * @param {Record<string, string>} importAttributes Validations for the
   *                                                  module import.
   * @returns {Promise<ModuleJob>} The (possibly pending) module job
   */
  async getModuleJob(specifier, parentURL, importAttributes) {
    const resolveResult = await this.resolve(specifier, parentURL, importAttributes);
    return this.getJobFromResolveResult(resolveResult, parentURL, importAttributes);
  }

  getModuleJobSync(specifier, parentURL, importAttributes) {
    const resolveResult = this.resolveSync(specifier, parentURL, importAttributes);
    return this.getJobFromResolveResult(resolveResult, parentURL, importAttributes, true);
  }

  getJobFromResolveResult(resolveResult, parentURL, importAttributes, sync) {
    const { url, format } = resolveResult;
    const resolvedImportAttributes = resolveResult.importAttributes ?? importAttributes;
    let job = this.loadCache.get(url, resolvedImportAttributes.type);

    // CommonJS will set functions for lazy job evaluation.
    if (typeof job === 'function') {
      this.loadCache.set(url, undefined, job = job());
    }

    if (job === undefined) {
      job = this.#createModuleJob(url, resolvedImportAttributes, parentURL, format, sync);
    }

    return job;
  }

  /**
   * This constructs (creates, instantiates and evaluates) a module graph that
   * is require()'d.
   * @param {CJSModule} mod CJS module wrapper of the ESM.
   * @param {string} filename Resolved filename of the module being require()'d
   * @param {string} source Source code. TODO(joyeecheung): pass the raw buffer.
   * @param {string} isMain Whether this module is a main module.
   * @param {CJSModule|undefined} parent Parent module, if any.
   * @returns {{wrap: ModuleWrap, namespace: ModuleNamespaceObject}}
   */
  importSyncForRequire(mod, filename, source, isMain, parent) {
    const url = pathToFileURL(filename).href;
    let job = this.loadCache.get(url, kImplicitTypeAttribute);
    // This module job is already created:
    // 1. If it was loaded by `require()` before, at this point the instantiation
    //    is already completed and we can check the whether it is in a cycle
    //    (in that case the module status is kEvaluaing), and whether the
    //    required graph is synchronous.
    // 2. If it was loaded by `import` before, only allow it if it's already evaluated
    //    to forbid cycles.
    //    TODO(joyeecheung): ensure that imported synchronous graphs are evaluated
    //    synchronously so that any previously imported synchronous graph is already
    //    evaluated at this point.
    if (job !== undefined) {
      mod[kRequiredModuleSymbol] = job.module;
      if (job.module.getStatus() !== kEvaluated) {
        const parentFilename = urlToFilename(parent?.filename);
        let message = `Cannot require() ES Module ${filename} in a cycle.`;
        if (parentFilename) {
          message += ` (from ${parentFilename})`;
        }
        throw new ERR_REQUIRE_CYCLE_MODULE(message);
      }
      return { wrap: job.module, namespace: job.module.getNamespaceSync() };
    }
    // TODO(joyeecheung): refactor this so that we pre-parse in C++ and hit the
    // cache here, or use a carrier object to carry the compiled module script
    // into the constructor to ensure cache hit.
    const wrap = compileSourceTextModule(url, source, this);
    const inspectBrk = (isMain && getOptionValue('--inspect-brk'));

    const { ModuleJobSync } = require('internal/modules/esm/module_job');
    job = new ModuleJobSync(this, url, kEmptyObject, wrap, isMain, inspectBrk);
    this.loadCache.set(url, kImplicitTypeAttribute, job);
    mod[kRequiredModuleSymbol] = job.module;
    return { wrap: job.module, namespace: job.runSync().namespace };
  }

  /**
   * Resolve individual module requests and create or get the cached ModuleWraps for
   * each of them. This is only used to create a module graph being require()'d.
   * @param {string} specifier Specifier of the the imported module.
   * @param {string} parentURL Where the import comes from.
   * @param {object} importAttributes import attributes from the import statement.
   * @returns {ModuleJobBase}
   */
  getModuleJobForRequire(specifier, parentURL, importAttributes) {
    assert(getOptionValue('--experimental-require-module'));

    const parsed = URLParse(specifier);
    if (parsed != null) {
      const protocol = parsed.protocol;
      if (protocol === 'https:' || protocol === 'http:') {
        throw new ERR_NETWORK_IMPORT_DISALLOWED(specifier, parentURL,
                                                'ES modules cannot be loaded by require() from the network');
      }
      assert(protocol === 'file:' || protocol === 'node:' || protocol === 'data:');
    }

    const requestKey = this.#resolveCache.serializeKey(specifier, importAttributes);
    let resolveResult = this.#resolveCache.get(requestKey, parentURL);
    if (resolveResult == null) {
      resolveResult = this.defaultResolve(specifier, parentURL, importAttributes);
      this.#resolveCache.set(requestKey, parentURL, resolveResult);
    }

    const { url, format } = resolveResult;
    const resolvedImportAttributes = resolveResult.importAttributes ?? importAttributes;
    let job = this.loadCache.get(url, resolvedImportAttributes.type);
    if (job !== undefined) {
      // This module is being evaluated, which means it's imported in a previous link
      // in a cycle.
      if (job.module.getStatus() === kEvaluating) {
        const parentFilename = urlToFilename(parentURL);
        let message = `Cannot import Module ${specifier} in a cycle.`;
        if (parentFilename) {
          message += ` (from ${parentFilename})`;
        }
        throw new ERR_REQUIRE_CYCLE_MODULE(message);
      }
      // Otherwise the module could be imported before but the evaluation may be already
      // completed (e.g. the require call is lazy) so it's okay. We will return the
      // module now and check asynchronicity of the entire graph later, after the
      // graph is instantiated.
      return job;
    }

    defaultLoadSync ??= require('internal/modules/esm/load').defaultLoadSync;
    const loadResult = defaultLoadSync(url, { format, importAttributes });
    const {
      format: finalFormat,
      responseURL,
      source,
    } = loadResult;

    this.validateLoadResult(url, finalFormat);
    if (finalFormat === 'wasm') {
      assert.fail('WASM is currently unsupported by require(esm)');
    }

    const translator = getTranslators().get(finalFormat);
    if (!translator) {
      throw new ERR_UNKNOWN_MODULE_FORMAT(finalFormat, responseURL);
    }

    const isMain = (parentURL === undefined);
    const wrap = FunctionPrototypeCall(translator, this, responseURL, source, isMain);
    assert(wrap instanceof ModuleWrap);  // No asynchronous translators should be called.

    if (process.env.WATCH_REPORT_DEPENDENCIES && process.send) {
      process.send({ 'watch:import': [url] });
    }

    const cjsModule = wrap[imported_cjs_symbol];
    if (cjsModule) {
      assert(finalFormat === 'commonjs-sync');
      // Check if the ESM initiating import CJS is being required by the same CJS module.
      if (cjsModule && cjsModule[kIsExecuting]) {
        const parentFilename = urlToFilename(parentURL);
        let message = `Cannot import CommonJS Module ${specifier} in a cycle.`;
        if (parentFilename) {
          message += ` (from ${parentFilename})`;
        }
        throw new ERR_REQUIRE_CYCLE_MODULE(message);
      }
    }

    const inspectBrk = (isMain && getOptionValue('--inspect-brk'));
    const { ModuleJobSync } = require('internal/modules/esm/module_job');
    job = new ModuleJobSync(this, url, importAttributes, wrap, isMain, inspectBrk);

    this.loadCache.set(url, importAttributes.type, job);
    return job;
  }

  /**
   * Create and cache an object representing a loaded module.
   * @param {string} url The absolute URL that was resolved for this module
   * @param {Record<string, string>} importAttributes Validations for the
   *                                                  module import.
   * @param {string} [parentURL] The absolute URL of the module importing this
   *                             one, unless this is the Node.js entry point
   * @param {string} [format] The format hint possibly returned by the
   *                          `resolve` hook
   * @returns {Promise<ModuleJob>} The (possibly pending) module job
   */
  #createModuleJob(url, importAttributes, parentURL, format, sync) {
    const callTranslator = ({ format: finalFormat, responseURL, source }, isMain) => {
      const translator = getTranslators().get(finalFormat);

      if (!translator) {
        throw new ERR_UNKNOWN_MODULE_FORMAT(finalFormat, responseURL);
      }

      return FunctionPrototypeCall(translator, this, responseURL, source, isMain);
    };
    const context = { format, importAttributes };

    const moduleProvider = sync ?
      (url, isMain) => callTranslator(this.loadSync(url, context), isMain) :
      async (url, isMain) => callTranslator(await this.load(url, context), isMain);

    const isMain = parentURL === undefined;
    const inspectBrk = (
      isMain &&
      getOptionValue('--inspect-brk')
    );

    if (process.env.WATCH_REPORT_DEPENDENCIES && process.send) {
      process.send({ 'watch:import': [url] });
    }

    const { ModuleJob } = require('internal/modules/esm/module_job');
    const job = new ModuleJob(
      this,
      url,
      importAttributes,
      moduleProvider,
      isMain,
      inspectBrk,
      sync,
    );

    this.loadCache.set(url, importAttributes.type, job);

    return job;
  }

  /**
   * This method is usually called indirectly as part of the loading processes.
   * Use directly with caution.
   * @param {string} specifier The first parameter of an `import()` expression.
   * @param {string} parentURL Path of the parent importing the module.
   * @param {Record<string, string>} importAttributes Validations for the
   *                                                  module import.
   * @returns {Promise<ModuleExports>}
   */
  async import(specifier, parentURL, importAttributes, isEntryPoint = false) {
    return onImport.tracePromise(async () => {
      const moduleJob = await this.getModuleJob(specifier, parentURL, importAttributes);
      const { module } = await moduleJob.run(isEntryPoint);
      return module.getNamespace();
    }, {
      __proto__: null,
      parentURL,
      url: specifier,
    });
  }

  /**
   * @see {@link CustomizedModuleLoader.register}
   */
  register(specifier, parentURL, data, transferList, isInternal) {
    if (!this.#customizations) {
      // `CustomizedModuleLoader` is defined at the bottom of this file and
      // available well before this line is ever invoked. This is here in
      // order to preserve the git diff instead of moving the class.
      // eslint-disable-next-line no-use-before-define
      this.setCustomizations(new CustomizedModuleLoader());
    }
    return this.#customizations.register(`${specifier}`, `${parentURL}`, data, transferList, isInternal);
  }

  /**
   * Resolve the location of the module.
   * @param {string} originalSpecifier The specified URL path of the module to
   *                                   be resolved.
   * @param {string} [parentURL] The URL path of the module's parent.
   * @param {ImportAttributes} importAttributes Attributes from the import
   *                                            statement or expression.
   * @returns {{ format: string, url: URL['href'] }}
   */
  resolve(originalSpecifier, parentURL, importAttributes) {
    if (this.#customizations) {
      return this.#customizations.resolve(originalSpecifier, parentURL, importAttributes);
    }
    const requestKey = this.#resolveCache.serializeKey(originalSpecifier, importAttributes);
    const cachedResult = this.#resolveCache.get(requestKey, parentURL);
    if (cachedResult != null) {
      return cachedResult;
    }
    const result = this.defaultResolve(originalSpecifier, parentURL, importAttributes);
    this.#resolveCache.set(requestKey, parentURL, result);
    return result;
  }

  /**
   * Just like `resolve` except synchronous. This is here specifically to support
   * `import.meta.resolve` which must happen synchronously.
   */
  resolveSync(originalSpecifier, parentURL, importAttributes) {
    if (this.#customizations) {
      return this.#customizations.resolveSync(originalSpecifier, parentURL, importAttributes);
    }
    return this.defaultResolve(originalSpecifier, parentURL, importAttributes);
  }

  /**
   * Our `defaultResolve` is synchronous and can be used in both
   * `resolve` and `resolveSync`. This function is here just to avoid
   * repeating the same code block twice in those functions.
   */
  defaultResolve(originalSpecifier, parentURL, importAttributes) {
    defaultResolve ??= require('internal/modules/esm/resolve').defaultResolve;

    const context = {
      __proto__: null,
      conditions: this.#defaultConditions,
      importAttributes,
      parentURL,
    };

    return defaultResolve(originalSpecifier, context);
  }

  /**
   * Provide source that is understood by one of Node's translators.
   * @param {URL['href']} url The URL/path of the module to be loaded
   * @param {object} [context] Metadata about the module
   * @returns {Promise<{ format: ModuleFormat, source: ModuleSource }>}
   */
  async load(url, context) {
    defaultLoad ??= require('internal/modules/esm/load').defaultLoad;
    const result = this.#customizations ?
      await this.#customizations.load(url, context) :
      await defaultLoad(url, context);
    this.validateLoadResult(url, result?.format);
    return result;
  }

  loadSync(url, context) {
    defaultLoadSync ??= require('internal/modules/esm/load').defaultLoadSync;

    let result = this.#customizations ?
      this.#customizations.loadSync(url, context) :
      defaultLoadSync(url, context);
    let format = result?.format;
    if (format === 'module' || format === 'module-typescript') {
      throw new ERR_REQUIRE_ESM(url, true);
    }
    if (format === 'commonjs') {
      format = 'require-commonjs';
      result = { __proto__: result, format };
    }
    if (format === 'commonjs-typescript') {
      format = 'require-commonjs-typescript';
      result = { __proto__: result, format };
    }

    this.validateLoadResult(url, format);
    return result;
  }

  validateLoadResult(url, format) {
    if (format == null) {
      require('internal/modules/esm/load').throwUnknownModuleFormat(url, format);
    }
  }

  importMetaInitialize(meta, context) {
    if (this.#customizations) {
      return this.#customizations.importMetaInitialize(meta, context, this);
    }
    importMetaInitializer ??= require('internal/modules/esm/initialize_import_meta').initializeImportMeta;
    meta = importMetaInitializer(meta, context, this);
    return meta;
  }

  /**
   * No-op when no hooks have been supplied.
   */
  forceLoadHooks() {
    this.#customizations?.forceLoadHooks();
  }
}
ObjectSetPrototypeOf(ModuleLoader.prototype, null);

class CustomizedModuleLoader {

  allowImportMetaResolve = true;

  /**
   * Instantiate a module loader that uses user-provided custom loader hooks.
   */
  constructor() {
    getHooksProxy();
  }

  /**
   * Register some loader specifier.
   * @param {string} originalSpecifier The specified URL path of the loader to
   *                                   be registered.
   * @param {string} parentURL The parent URL from where the loader will be
   *                           registered if using it package name as specifier
   * @param {any} [data] Arbitrary data to be passed from the custom loader
   * (user-land) to the worker.
   * @param {any[]} [transferList] Objects in `data` that are changing ownership
   * @param {boolean} [isInternal] For internal loaders that should not be publicly exposed.
   * @returns {{ format: string, url: URL['href'] }}
   */
  register(originalSpecifier, parentURL, data, transferList, isInternal) {
    return hooksProxy.makeSyncRequest('register', transferList, originalSpecifier, parentURL, data, isInternal);
  }

  /**
   * Resolve the location of the module.
   * @param {string} originalSpecifier The specified URL path of the module to
   *                                   be resolved.
   * @param {string} [parentURL] The URL path of the module's parent.
   * @param {ImportAttributes} importAttributes Attributes from the import
   *                                            statement or expression.
   * @returns {{ format: string, url: URL['href'] }}
   */
  resolve(originalSpecifier, parentURL, importAttributes) {
    return hooksProxy.makeAsyncRequest('resolve', undefined, originalSpecifier, parentURL, importAttributes);
  }

  resolveSync(originalSpecifier, parentURL, importAttributes) {
    // This happens only as a result of `import.meta.resolve` calls, which must be sync per spec.
    return hooksProxy.makeSyncRequest('resolve', undefined, originalSpecifier, parentURL, importAttributes);
  }

  /**
   * Provide source that is understood by one of Node's translators.
   * @param {URL['href']} url The URL/path of the module to be loaded
   * @param {object} [context] Metadata about the module
   * @returns {Promise<{ format: ModuleFormat, source: ModuleSource }>}
   */
  load(url, context) {
    return hooksProxy.makeAsyncRequest('load', undefined, url, context);
  }
  loadSync(url, context) {
    return hooksProxy.makeSyncRequest('load', undefined, url, context);
  }

  importMetaInitialize(meta, context, loader) {
    hooksProxy.importMetaInitialize(meta, context, loader);
  }

  forceLoadHooks() {
    hooksProxy.waitForWorker();
  }
}

let emittedLoaderFlagWarning = false;
/**
 * A loader instance is used as the main entry point for loading ES modules. Currently, this is a singleton; there is
 * only one used for loading the main module and everything in its dependency graph, though separate instances of this
 * class might be instantiated as part of bootstrap for other purposes.
 * @returns {ModuleLoader}
 */
function createModuleLoader() {
  let customizations = null;
  // Don't spawn a new worker if custom loaders are disabled. For instance, if
  // we're already in a worker thread created by instantiating
  // CustomizedModuleLoader; doing so would cause an infinite loop.
  if (!require('internal/modules/esm/utils').forceDefaultLoader()) {
    const userLoaderPaths = getOptionValue('--experimental-loader');
    if (userLoaderPaths.length > 0) {
      if (!emittedLoaderFlagWarning) {
        const readableURIEncode = (string) => ArrayPrototypeReduce(
          [
            [/'/g, '%27'], // We need to URL-encode the single quote as it's the delimiter for the --import flag.
            [/%22/g, '"'], // We can decode the double quotes to improve readability.
            [/%2F/ig, '/'], // We can decode the slashes to improve readability.
          ],
          (str, { 0: regex, 1: replacement }) => RegExpPrototypeSymbolReplace(hardenRegExp(regex), str, replacement),
          encodeURIComponent(string));
        process.emitWarning(
          '`--experimental-loader` may be removed in the future; instead use `register()`:\n' +
          `--import 'data:text/javascript,import { register } from "node:module"; import { pathToFileURL } from "node:url"; ${ArrayPrototypeJoin(
            ArrayPrototypeMap(userLoaderPaths, (loader) => `register(${readableURIEncode(JSONStringify(loader))}, pathToFileURL("./"))`),
            '; ',
          )};'`,
          'ExperimentalWarning',
        );
        emittedLoaderFlagWarning = true;
      }
      customizations = new CustomizedModuleLoader();
    }
  }

  return new ModuleLoader(customizations);
}


/**
 * Get the HooksProxy instance. If it is not defined, then create a new one.
 * @returns {HooksProxy}
 */
function getHooksProxy() {
  if (!hooksProxy) {
    const { HooksProxy } = require('internal/modules/esm/hooks');
    hooksProxy = new HooksProxy();
  }

  return hooksProxy;
}

let cascadedLoader;

/**
 * This is a singleton ESM loader that integrates the loader hooks, if any.
 * It it used by other internal built-ins when they need to load ESM code
 * while also respecting hooks.
 * When built-ins need access to this loader, they should do
 * require('internal/module/esm/loader').getOrInitializeCascadedLoader()
 * lazily only right before the loader is actually needed, and don't do it
 * in the top-level, to avoid circular dependencies.
 * @returns {ModuleLoader}
 */
function getOrInitializeCascadedLoader() {
  cascadedLoader ??= createModuleLoader();
  return cascadedLoader;
}

/**
 * Register a single loader programmatically.
 * @param {string|URL} specifier
 * @param {string|URL} [parentURL] Base to use when resolving `specifier`; optional if
 * `specifier` is absolute. Same as `options.parentUrl`, just inline
 * @param {object} [options] Additional options to apply, described below.
 * @param {string|URL} [options.parentURL] Base to use when resolving `specifier`
 * @param {any} [options.data] Arbitrary data passed to the loader's `initialize` hook
 * @param {any[]} [options.transferList] Objects in `data` that are changing ownership
 * @returns {void} We want to reserve the return value for potential future extension of the API.
 * @example
 * ```js
 * register('./myLoader.js');
 * register('ts-node/esm', { parentURL: import.meta.url });
 * register('./myLoader.js', { parentURL: import.meta.url });
 * register('ts-node/esm', import.meta.url);
 * register('./myLoader.js', import.meta.url);
 * register(new URL('./myLoader.js', import.meta.url));
 * register('./myLoader.js', {
 *   parentURL: import.meta.url,
 *   data: { banana: 'tasty' },
 * });
 * register('./myLoader.js', {
 *   parentURL: import.meta.url,
 *   data: someArrayBuffer,
 *   transferList: [someArrayBuffer],
 * });
 * ```
 */
function register(specifier, parentURL = undefined, options) {
  if (parentURL != null && typeof parentURL === 'object' && !isURL(parentURL)) {
    options = parentURL;
    parentURL = options.parentURL;
  }
  getOrInitializeCascadedLoader().register(
    specifier,
    parentURL ?? 'data:',
    options?.data,
    options?.transferList,
  );
}

module.exports = {
  createModuleLoader,
  getHooksProxy,
  getOrInitializeCascadedLoader,
  register,
};
