'use strict';

const {
  ArrayPrototypeMap,
  Boolean,
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

/** @type {import('internal/util/types')} */
let _TYPES = null;
/**
 * Lazily loads and returns the internal/util/types module.
 */
function lazyTypes() {
  if (_TYPES !== null) { return _TYPES; }
  return _TYPES = require('internal/util/types');
}

const {
  compileFunctionForCJSLoader,
} = internalBinding('contextify');

const { BuiltinModule } = require('internal/bootstrap/realm');
const assert = require('internal/assert');
const { readFileSync } = require('fs');
const { dirname, extname, isAbsolute } = require('path');
const {
  loadBuiltinModule,
  tsParse,
  stripBOM,
  urlToFilename,
} = require('internal/modules/helpers');
const {
  kIsCachedByESMLoader,
  Module: CJSModule,
  wrapModuleLoad,
  kModuleSource,
  kModuleExport,
  kModuleExportNames,
} = require('internal/modules/cjs/loader');
const { fileURLToPath, pathToFileURL, URL } = require('internal/url');
let debug = require('internal/util/debuglog').debuglog('esm', (fn) => {
  debug = fn;
});
const { emitExperimentalWarning, kEmptyObject, setOwnProperty, isWindows } = require('internal/util');
const {
  ERR_UNKNOWN_BUILTIN_MODULE,
  ERR_INVALID_RETURN_PROPERTY_VALUE,
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
 * Initializes the CommonJS module lexer parser.
 * If WebAssembly is available, it uses the optimized version from the dist folder.
 * Otherwise, it falls back to the JavaScript version from the lexer folder.
 */
async function initCJSParse() {
  if (typeof WebAssembly === 'undefined') {
    initCJSParseSync();
  } else {
    const { parse, init } =
        require('internal/deps/cjs-module-lexer/dist/lexer');
    try {
      await init();
      cjsParse = parse;
    } catch {
      initCJSParseSync();
    }
  }
}

function initCJSParseSync() {
  // TODO(joyeecheung): implement a binding that directly compiles using
  // v8::WasmModuleObject::Compile() synchronously.
  if (cjsParse === undefined) {
    cjsParse = require('internal/deps/cjs-module-lexer/lexer').parse;
  }
}

const translators = new SafeMap();
exports.translators = translators;

let DECODER = null;
/**
 * Asserts that the given body is a buffer source (either a string, array buffer, or typed array).
 * Throws an error if the body is not a buffer source.
 * @param {string | ArrayBufferView | ArrayBuffer} body - The body to check.
 * @param {boolean} allowString - Whether or not to allow a string as a valid buffer source.
 * @param {string} hookName - The name of the hook being called.
 * @throws {ERR_INVALID_RETURN_PROPERTY_VALUE} If the body is not a buffer source.
 */
function assertBufferSource(body, allowString, hookName) {
  if (allowString && typeof body === 'string') {
    return;
  }
  const { isArrayBufferView, isAnyArrayBuffer } = lazyTypes();
  if (isArrayBufferView(body) || isAnyArrayBuffer(body)) {
    return;
  }
  throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
    `${allowString ? 'string, ' : ''}array buffer, or typed array`,
    hookName,
    'source',
    body,
  );
}

/**
 * Converts a buffer or buffer-like object to a string.
 * @param {string | ArrayBuffer | ArrayBufferView} body - The buffer or buffer-like object to convert to a string.
 * @returns {string} The resulting string.
 */
function stringify(body) {
  if (typeof body === 'string') { return body; }
  assertBufferSource(body, false, 'load');
  const { TextDecoder } = require('internal/encoding');
  DECODER = DECODER === null ? new TextDecoder() : DECODER;
  return DECODER.decode(body);
}

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
    const job = cascadedLoader.getModuleJobSync(specifier, url, importAttributes);
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
 * @param {typeof loadCJSModule} [loadCJS=loadCJSModule] - The function to load the CommonJS module.
 * @returns {ModuleWrap} The ModuleWrap object for the CommonJS module.
 */
function createCJSModuleWrap(url, source, isMain, loadCJS = loadCJSModule) {
  debug(`Translating CJSModule ${url}`);

  const filename = urlToFilename(url);
  // In case the source was not provided by the `load` step, we need fetch it now.
  source = stringify(source ?? getSource(new URL(url)).source);

  const { exportNames, module } = cjsPreparseModuleExports(filename, source);
  cjsCache.set(url, module);
  const namesWithDefault = exportNames.has('default') ?
    [...exportNames] : ['default', ...exportNames];

  if (isMain) {
    setOwnProperty(process, 'mainModule', module);
  }

  return new ModuleWrap(url, undefined, namesWithDefault, function() {
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
      if (!ObjectPrototypeHasOwnProperty(exports, exportName) ||
          exportName === 'default') {
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
  }, module);
}

translators.set('commonjs-sync', function requireCommonJS(url, source, isMain) {
  initCJSParseSync();
  assert(!isMain);  // This is only used by imported CJS modules.

  return createCJSModuleWrap(url, source, isMain, (module, source, url, filename, isMain) => {
    assert(module === CJSModule._cache[filename]);
    assert(!isMain);
    wrapModuleLoad(filename, null, isMain);
  });
});

// Handle CommonJS modules referenced by `require` calls.
// This translator function must be sync, as `require` is sync.
translators.set('require-commonjs', (url, source, isMain) => {
  assert(cjsParse);

  return createCJSModuleWrap(url, source);
});

// Handle CommonJS modules referenced by `require` calls.
// This translator function must be sync, as `require` is sync.
translators.set('require-commonjs-typescript', (url, source, isMain) => {
  emitExperimentalWarning('Type Stripping');
  assert(cjsParse);
  const code = tsParse(stringify(source));
  return createCJSModuleWrap(url, code);
});

// Handle CommonJS modules referenced by `import` statements or expressions,
// or as the initial entry point when the ESM loader handles a CommonJS entry.
translators.set('commonjs', async function commonjsStrategy(url, source,
                                                            isMain) {
  if (!cjsParse) {
    await initCJSParse();
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
  return createCJSModuleWrap(url, source, isMain, cjsLoader);

});

/**
 * Pre-parses a CommonJS module's exports and re-exports.
 * @param {string} filename - The filename of the module.
 * @param {string} [source] - The source code of the module.
 */
function cjsPreparseModuleExports(filename, source) {
  // TODO: Do we want to keep hitting the user mutable CJS loader here?
  let module = CJSModule._cache[filename];
  if (module && module[kModuleExportNames] !== undefined) {
    return { module, exportNames: module[kModuleExportNames] };
  }
  const loaded = Boolean(module);
  if (!loaded) {
    module = new CJSModule(filename);
    module.filename = filename;
    module.paths = CJSModule._nodeModulePaths(module.path);
    module[kIsCachedByESMLoader] = true;
    module[kModuleSource] = source;
    CJSModule._cache[filename] = module;
  }

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

  if (reexports.length) {
    module.filename = filename;
    module.paths = CJSModule._nodeModulePaths(module.path);
    for (let i = 0; i < reexports.length; i++) {
      const reexport = reexports[i];
      let resolved;
      try {
        // TODO: this should be calling the `resolve` hook chain instead.
        // Doing so would mean dropping support for CJS in the loader thread, as
        // this call needs to be sync from the perspective of the main thread,
        // which we can do via HooksProxy and Atomics, but we can't do within
        // the loaders thread. Until this is done, the lexer will use the
        // monkey-patchable CJS loader to get the path to the module file to
        // load (which may or may not be aligned with the URL that the `resolve`
        // hook have returned).
        resolved = CJSModule._resolveFilename(reexport, module);
      } catch {
        continue;
      }
      // TODO: this should be calling the `load` hook chain and check if it returns
      // `format: 'commonjs'` instead of relying on file extensions.
      const ext = extname(resolved);
      if ((ext === '.js' || ext === '.cjs' || !CJSModule._extensions[ext]) &&
      isAbsolute(resolved)) {
        // TODO: this should be calling the `load` hook chain to get the source
        // (and fallback to reading the FS only if the source is nullish).
        const source = readFileSync(resolved, 'utf-8');
        const { exportNames: reexportNames } = cjsPreparseModuleExports(resolved, source);
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
  emitExperimentalWarning('Importing JSON modules');
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
    if (module && module.loaded) {
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
    if (module && module.loaded) {
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
    // TODO(joyeecheung): implement a binding that directly compiles using
    // v8::WasmModuleObject::Compile() synchronously.
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

// Strategy for loading a commonjs TypeScript module
translators.set('commonjs-typescript', function(url, source) {
  emitExperimentalWarning('Type Stripping');
  assertBufferSource(source, false, 'load');
  const code = tsParse(stringify(source));
  debug(`Translating TypeScript ${url}`);
  return FunctionPrototypeCall(translators.get('commonjs'), this, url, code, false);
});

// Strategy for loading an esm TypeScript module
translators.set('module-typescript', function(url, source) {
  emitExperimentalWarning('Type Stripping');
  assertBufferSource(source, false, 'load');
  const code = tsParse(stringify(source));
  debug(`Translating TypeScript ${url}`);
  return FunctionPrototypeCall(translators.get('module'), this, url, code, false);
});
