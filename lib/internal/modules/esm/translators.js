'use strict';

const {
  ArrayPrototypeMap,
  ArrayPrototypePush,
  FunctionPrototypeCall,
  JSONParse,
  ObjectKeys,
  ObjectPrototypeHasOwnProperty,
  ReflectApply,
  SafeArrayIterator,
  SafeMap,
  SafeSet,
  StringPrototypeIncludes,
  StringPrototypeReplaceAll,
  StringPrototypeSlice,
  StringPrototypeStartsWith,
  globalThis: { WebAssembly },
} = primordials;

const {
  compileFunctionForCJSLoader,
} = internalBinding('contextify');

const { BuiltinModule } = require('internal/bootstrap/realm');
const assert = require('internal/assert');
const { readFileSync } = require('fs');
const { dirname, extname } = require('path');
const {
  assertBufferSource,
  loadBuiltinModule,
  stringify,
  stripBOM,
  urlToFilename,
} = require('internal/modules/helpers');
const { stripTypeScriptModuleTypes } = require('internal/modules/typescript');
const {
  kIsCachedByESMLoader,
  Module: CJSModule,
  wrapModuleLoad,
  kModuleSource,
  kModuleExport,
  kModuleExportNames,
  findLongestRegisteredExtension,
  resolveForCJSWithHooks,
  loadSourceForCJSWithHooks,
} = require('internal/modules/cjs/loader');
const { fileURLToPath, pathToFileURL, URL } = require('internal/url');
let debug = require('internal/util/debuglog').debuglog('esm', (fn) => {
  debug = fn;
});
const { emitExperimentalWarning, kEmptyObject, setOwnProperty, isWindows } = require('internal/util');
const {
  ERR_INVALID_RETURN_PROPERTY_VALUE,
  ERR_UNKNOWN_BUILTIN_MODULE,
} = require('internal/errors').codes;
const { maybeCacheSourceMap } = require('internal/source_map/source_map_cache');
const moduleWrap = internalBinding('module_wrap');
const { ModuleWrap } = moduleWrap;

// Lazy-loading to avoid circular dependencies.
let getSourceSync;
/**
 * @param {Parameters<typeof import('./load').getSourceSync>[0]} url
 * @returns {ReturnType<typeof import('./load').getSourceSync>}
 */
function getSource(url) {
  getSourceSync ??= require('internal/modules/esm/load').getSourceSync;
  return getSourceSync(url);
}

/** @type {import('deps/cjs-module-lexer/lexer.js').parse} */
let cjsParse;
/**
 * Initializes the CommonJS module lexer parser using the JavaScript version.
 * TODO(joyeecheung): Use `require('internal/deps/cjs-module-lexer/dist/lexer').initSync()`
 * when cjs-module-lexer 1.4.0 is rolled in.
 */
function initCJSParseSync() {
  if (cjsParse === undefined) {
    cjsParse = require('internal/deps/cjs-module-lexer/lexer').parse;
  }
}

const translators = new SafeMap();
exports.translators = translators;

/**
 * Converts a URL to a file path if the URL protocol is 'file:'.
 * @param {string} url - The URL to convert.
 */
function errPath(url) {
  const parsed = new URL(url);
  if (parsed.protocol === 'file:') {
    return fileURLToPath(parsed);
  }
  return url;
}

// Strategy for loading a standard JavaScript module.
translators.set('module', function moduleStrategy(url, source, isMain) {
  assertBufferSource(source, true, 'load');
  source = stringify(source);
  debug(`Translating StandardModule ${url}`);
  const { compileSourceTextModule } = require('internal/modules/esm/utils');
  const module = compileSourceTextModule(url, source, this);
  return module;
});

/**
 * Loads a CommonJS module via the ESM Loader sync CommonJS translator.
 * This translator creates its own version of the `require` function passed into CommonJS modules.
 * Any monkey patches applied to the CommonJS Loader will not affect this module.
 * Any `require` calls in this module will load all children in the same way.
 * @param {import('internal/modules/cjs/loader').Module} module - The module to load.
 * @param {string} source - The source code of the module.
 * @param {string} url - The URL of the module.
 * @param {string} filename - The filename of the module.
 * @param {boolean} isMain - Whether the module is the entrypoint
 */
function loadCJSModule(module, source, url, filename, isMain) {
  const compileResult = compileFunctionForCJSLoader(source, filename, false /* is_sea_main */, false);

  const { function: compiledWrapper, sourceMapURL } = compileResult;
  // Cache the source map for the cjs module if present.
  if (sourceMapURL) {
    maybeCacheSourceMap(url, source, module, false, undefined, sourceMapURL);
  }

  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  const __dirname = dirname(filename);
  // eslint-disable-next-line func-name-matching,func-style
  const requireFn = function require(specifier) {
    let importAttributes = kEmptyObject;
    if (!StringPrototypeStartsWith(specifier, 'node:') && !BuiltinModule.normalizeRequirableId(specifier)) {
      // TODO: do not depend on the monkey-patchable CJS loader here.
      const path = CJSModule._resolveFilename(specifier, module);
      switch (extname(path)) {
        case '.json':
          importAttributes = { __proto__: null, type: 'json' };
          break;
        case '.node':
          return wrapModuleLoad(specifier, module);
        default:
            // fall through
      }
      specifier = `${pathToFileURL(path)}`;
    }
    const job = cascadedLoader.getModuleJobForRequireInImportedCJS(specifier, url, importAttributes);
    job.runSync();
    return cjsCache.get(job.url).exports;
  };
  setOwnProperty(requireFn, 'resolve', function resolve(specifier) {
    if (!StringPrototypeStartsWith(specifier, 'node:')) {
      const path = CJSModule._resolveFilename(specifier, module);
      if (specifier !== path) {
        specifier = `${pathToFileURL(path)}`;
      }
    }
    const { url: resolvedURL } = cascadedLoader.resolveSync(specifier, url, kEmptyObject);
    return urlToFilename(resolvedURL);
  });
  setOwnProperty(requireFn, 'main', process.mainModule);

  ReflectApply(compiledWrapper, module.exports,
               [module.exports, requireFn, module, filename, __dirname]);
  setOwnProperty(module, 'loaded', true);
}

// TODO: can we use a weak map instead?
const cjsCache = new SafeMap();
/**
 * Creates a ModuleWrap object for a CommonJS module.
 * @param {string} url - The URL of the module.
 * @param {string} source - The source code of the module.
 * @param {boolean} isMain - Whether the module is the main module.
 * @param {string} format - Format of the module.
 * @param {typeof loadCJSModule} [loadCJS=loadCJSModule] - The function to load the CommonJS module.
 * @returns {ModuleWrap} The ModuleWrap object for the CommonJS module.
 */
function createCJSModuleWrap(url, source, isMain, format, loadCJS = loadCJSModule) {
  debug(`Translating CJSModule ${url}`);

  const filename = urlToFilename(url);
  // In case the source was not provided by the `load` step, we need fetch it now.
  source = stringify(source ?? getSource(new URL(url)).source);

  const { exportNames, module } = cjsPreparseModuleExports(filename, source, format);
  cjsCache.set(url, module);

  const wrapperNames = [...exportNames, 'module.exports'];
  if (!exportNames.has('default')) {
    ArrayPrototypePush(wrapperNames, 'default');
  }

  if (isMain) {
    setOwnProperty(process, 'mainModule', module);
  }

  return new ModuleWrap(url, undefined, wrapperNames, function() {
    debug(`Loading CJSModule ${url}`);

    if (!module.loaded) {
      loadCJS(module, source, url, filename, !!isMain);
    }

    let exports;
    if (module[kModuleExport] !== undefined) {
      exports = module[kModuleExport];
      module[kModuleExport] = undefined;
    } else {
      ({ exports } = module);
    }
    for (const exportName of exportNames) {
      if (!ObjectPrototypeHasOwnProperty(exports, exportName) || exportName === 'default') {
        continue;
      }
      // We might trigger a getter -> dont fail.
      let value;
      try {
        value = exports[exportName];
      } catch {
        // Continue regardless of error.
      }
      this.setExport(exportName, value);
    }
    this.setExport('default', exports);
    this.setExport('module.exports', exports);
  }, module);
}

/**
 * Creates a ModuleWrap object for a CommonJS module without source texts.
 * @param {string} url - The URL of the module.
 * @param {boolean} isMain - Whether the module is the main module.
 * @returns {ModuleWrap} The ModuleWrap object for the CommonJS module.
 */
function createCJSNoSourceModuleWrap(url, isMain) {
  debug(`Translating CJSModule without source ${url}`);

  const filename = urlToFilename(url);

  const module = cjsEmplaceModuleCacheEntry(filename);
  cjsCache.set(url, module);

  if (isMain) {
    setOwnProperty(process, 'mainModule', module);
  }

  // Addon export names are not known until the addon is loaded.
  const exportNames = ['default', 'module.exports'];
  return new ModuleWrap(url, undefined, exportNames, function evaluationCallback() {
    debug(`Loading CJSModule ${url}`);

    if (!module.loaded) {
      wrapModuleLoad(filename, null, isMain);
    }

    /** @type {import('./loader').ModuleExports} */
    let exports;
    if (module[kModuleExport] !== undefined) {
      exports = module[kModuleExport];
      module[kModuleExport] = undefined;
    } else {
      ({ exports } = module);
    }

    this.setExport('default', exports);
    this.setExport('module.exports', exports);
  }, module);
}

translators.set('commonjs-sync', function requireCommonJS(url, source, isMain) {
  initCJSParseSync();

  return createCJSModuleWrap(url, source, isMain, 'commonjs', (module, source, url, filename, isMain) => {
    assert(module === CJSModule._cache[filename]);
    wrapModuleLoad(filename, null, isMain);
  });
});

// Handle CommonJS modules referenced by `require` calls.
// This translator function must be sync, as `require` is sync.
translators.set('require-commonjs', (url, source, isMain) => {
  initCJSParseSync();
  assert(cjsParse);

  return createCJSModuleWrap(url, source, isMain, 'commonjs');
});

// Handle CommonJS modules referenced by `require` calls.
// This translator function must be sync, as `require` is sync.
translators.set('require-commonjs-typescript', (url, source, isMain) => {
  assert(cjsParse);
  const code = stripTypeScriptModuleTypes(stringify(source), url);
  return createCJSModuleWrap(url, code, isMain, 'commonjs-typescript');
});

// Handle CommonJS modules referenced by `import` statements or expressions,
// or as the initial entry point when the ESM loader handles a CommonJS entry.
translators.set('commonjs', function commonjsStrategy(url, source, isMain) {
  if (!cjsParse) {
    initCJSParseSync();
  }

  // For backward-compatibility, it's possible to return a nullish value for
  // CJS source associated with a file: URL. In this case, the source is
  // obtained by calling the monkey-patchable CJS loader.
  const cjsLoader = source == null ? (module, source, url, filename, isMain) => {
    assert(module === CJSModule._cache[filename]);
    wrapModuleLoad(filename, undefined, isMain);
  } : loadCJSModule;

  try {
    // We still need to read the FS to detect the exports.
    source ??= readFileSync(new URL(url), 'utf8');
  } catch {
    // Continue regardless of error.
  }
  return createCJSModuleWrap(url, source, isMain, 'commonjs', cjsLoader);
});

/**
 * Get or create an entry in the CJS module cache for the given filename.
 * @param {string} filename CJS module filename
 * @returns {CJSModule} the cached CJS module entry
 */
function cjsEmplaceModuleCacheEntry(filename, exportNames) {
  // TODO: Do we want to keep hitting the user mutable CJS loader here?
  let cjsMod = CJSModule._cache[filename];
  if (cjsMod) {
    return cjsMod;
  }

  cjsMod = new CJSModule(filename);
  cjsMod.filename = filename;
  cjsMod.paths = CJSModule._nodeModulePaths(cjsMod.path);
  cjsMod[kIsCachedByESMLoader] = true;
  CJSModule._cache[filename] = cjsMod;

  return cjsMod;
}

/**
 * Pre-parses a CommonJS module's exports and re-exports.
 * @param {string} filename - The filename of the module.
 * @param {string} [source] - The source code of the module.
 * @param {string} [format]
 */
function cjsPreparseModuleExports(filename, source, format) {
  const module = cjsEmplaceModuleCacheEntry(filename);
  if (module[kModuleExportNames] !== undefined) {
    return { module, exportNames: module[kModuleExportNames] };
  }

  if (source === undefined) {
    ({ source } = loadSourceForCJSWithHooks(module, filename, format));
  }
  module[kModuleSource] = source;

  debug(`Preparsing exports of ${filename}`);
  let exports, reexports;
  try {
    ({ exports, reexports } = cjsParse(source || ''));
  } catch {
    exports = [];
    reexports = [];
  }

  const exportNames = new SafeSet(new SafeArrayIterator(exports));

  // Set first for cycles.
  module[kModuleExportNames] = exportNames;

  // If there are any re-exports e.g. `module.exports = { ...require(...) }`,
  // pre-parse the dependencies to find transitively exported names.
  if (reexports.length) {
    module.filename ??= filename;
    module.paths ??= CJSModule._nodeModulePaths(dirname(filename));

    for (let i = 0; i < reexports.length; i++) {
      debug(`Preparsing re-exports of '${filename}'`);
      const reexport = reexports[i];
      let resolved;
      let format;
      try {
        ({ format, filename: resolved } = resolveForCJSWithHooks(reexport, module, false));
      } catch (e) {
        debug(`Failed to resolve '${reexport}', skipping`, e);
        continue;
      }

      if (format === 'commonjs' ||
        (!BuiltinModule.normalizeRequirableId(resolved) && findLongestRegisteredExtension(resolved) === '.js')) {
        const { exportNames: reexportNames } = cjsPreparseModuleExports(resolved, undefined, format);
        for (const name of reexportNames) {
          exportNames.add(name);
        }
      }
    }
  }

  return { module, exportNames };
}

// Strategy for loading a node builtin CommonJS module that isn't
// through normal resolution
translators.set('builtin', function builtinStrategy(url) {
  debug(`Translating BuiltinModule ${url}`);
  // Slice 'node:' scheme
  const id = StringPrototypeSlice(url, 5);
  const module = loadBuiltinModule(id, url);
  cjsCache.set(url, module);
  if (!StringPrototypeStartsWith(url, 'node:') || !module) {
    throw new ERR_UNKNOWN_BUILTIN_MODULE(url);
  }
  debug(`Loading BuiltinModule ${url}`);
  return module.getESMFacade();
});

// Strategy for loading a JSON file
translators.set('json', function jsonStrategy(url, source) {
  assertBufferSource(source, true, 'load');
  debug(`Loading JSONModule ${url}`);
  const pathname = StringPrototypeStartsWith(url, 'file:') ?
    fileURLToPath(url) : null;
  const shouldCheckAndPopulateCJSModuleCache =
    // We want to involve the CJS loader cache only for `file:` URL with no search query and no hash.
    pathname && !StringPrototypeIncludes(url, '?') && !StringPrototypeIncludes(url, '#');
  let modulePath;
  let module;
  if (shouldCheckAndPopulateCJSModuleCache) {
    modulePath = isWindows ?
      StringPrototypeReplaceAll(pathname, '/', '\\') : pathname;
    module = CJSModule._cache[modulePath];
    if (module?.loaded) {
      const exports = module.exports;
      return new ModuleWrap(url, undefined, ['default'], function() {
        this.setExport('default', exports);
      });
    }
  }
  source = stringify(source);
  if (shouldCheckAndPopulateCJSModuleCache) {
    // A require call could have been called on the same file during loading and
    // that resolves synchronously. To make sure we always return the identical
    // export, we have to check again if the module already exists or not.
    // TODO: remove CJS loader from here as well.
    module = CJSModule._cache[modulePath];
    if (module?.loaded) {
      const exports = module.exports;
      return new ModuleWrap(url, undefined, ['default'], function() {
        this.setExport('default', exports);
      });
    }
  }
  try {
    const exports = JSONParse(stripBOM(source));
    module = {
      exports,
      loaded: true,
    };
  } catch (err) {
    // TODO (BridgeAR): We could add a NodeCore error that wraps the JSON
    // parse error instead of just manipulating the original error message.
    // That would allow to add further properties and maybe additional
    // debugging information.
    err.message = errPath(url) + ': ' + err.message;
    throw err;
  }
  if (shouldCheckAndPopulateCJSModuleCache) {
    CJSModule._cache[modulePath] = module;
  }
  cjsCache.set(url, module);
  return new ModuleWrap(url, undefined, ['default'], function() {
    debug(`Parsing JSONModule ${url}`);
    this.setExport('default', module.exports);
  });
});

// Strategy for loading a wasm module
translators.set('wasm', async function(url, source) {
  emitExperimentalWarning('Importing WebAssembly modules');

  assertBufferSource(source, false, 'load');

  debug(`Translating WASMModule ${url}`);

  let compiled;
  try {
    // TODO(joyeecheung): implement a translator that just uses
    // compiled = new WebAssembly.Module(source) to compile it
    // synchronously.
    compiled = await WebAssembly.compile(source);
  } catch (err) {
    err.message = errPath(url) + ': ' + err.message;
    throw err;
  }

  const imports =
      ArrayPrototypeMap(WebAssembly.Module.imports(compiled),
                        ({ module }) => module);
  const exports =
    ArrayPrototypeMap(WebAssembly.Module.exports(compiled),
                      ({ name }) => name);

  const createDynamicModule = require(
    'internal/modules/esm/create_dynamic_module');
  return createDynamicModule(imports, exports, url, (reflect) => {
    const { exports } = new WebAssembly.Instance(compiled, reflect.imports);
    for (const expt of ObjectKeys(exports)) {
      reflect.exports[expt].set(exports[expt]);
    }
  }).module;
});

// Strategy for loading a addon
translators.set('addon', function translateAddon(url, source, isMain) {
  emitExperimentalWarning('Importing addons');

  // The addon must be loaded from file system with dlopen. Assert
  // the source is null.
  if (source !== null) {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'null',
      'load',
      'source',
      source);
  }

  debug(`Translating addon ${url}`);

  return createCJSNoSourceModuleWrap(url, isMain);
});

// Strategy for loading a commonjs TypeScript module
translators.set('commonjs-typescript', function(url, source) {
  assertBufferSource(source, true, 'load');
  const code = stripTypeScriptModuleTypes(stringify(source), url);
  debug(`Translating TypeScript ${url}`);
  return FunctionPrototypeCall(translators.get('commonjs'), this, url, code, false);
});

// Strategy for loading an esm TypeScript module
translators.set('module-typescript', function(url, source) {
  assertBufferSource(source, true, 'load');
  const code = stripTypeScriptModuleTypes(stringify(source), url);
  debug(`Translating TypeScript ${url}`);
  return FunctionPrototypeCall(translators.get('module'), this, url, code, false);
});
