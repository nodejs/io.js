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

'use strict';

const {
  ArrayIsArray,
  ArrayPrototypeFilter,
  ArrayPrototypeIncludes,
  ArrayPrototypeIndexOf,
  ArrayPrototypeJoin,
  ArrayPrototypeMap,
  ArrayPrototypePush,
  ArrayPrototypePushApply,
  ArrayPrototypeSlice,
  ArrayPrototypeSplice,
  ArrayPrototypeUnshift,
  ArrayPrototypeUnshiftApply,
  Boolean,
  Error,
  JSONParse,
  ObjectDefineProperty,
  ObjectFreeze,
  ObjectGetOwnPropertyDescriptor,
  ObjectGetPrototypeOf,
  ObjectHasOwn,
  ObjectKeys,
  ObjectPrototype,
  ObjectPrototypeHasOwnProperty,
  ObjectSetPrototypeOf,
  Proxy,
  ReflectApply,
  ReflectSet,
  RegExpPrototypeExec,
  SafeMap,
  String,
  StringPrototypeCharAt,
  StringPrototypeCharCodeAt,
  StringPrototypeEndsWith,
  StringPrototypeIndexOf,
  StringPrototypeRepeat,
  StringPrototypeSlice,
  StringPrototypeSplit,
  StringPrototypeStartsWith,
  Symbol,
} = primordials;
const {
  privateSymbols: {
    module_source_private_symbol,
    module_export_names_private_symbol,
    module_circular_visited_private_symbol,
    module_export_private_symbol,
    module_parent_private_symbol,
  },
} = internalBinding('util');

const { kEvaluated, createRequiredModuleFacade } = internalBinding('module_wrap');

// Internal properties for Module instances.
/**
 * Cached {@link Module} source string.
 */
const kModuleSource = module_source_private_symbol;
/**
 * Cached {@link Module} export names for ESM loader.
 */
const kModuleExportNames = module_export_names_private_symbol;
/**
 * {@link Module} circular dependency visited flag.
 */
const kModuleCircularVisited = module_circular_visited_private_symbol;
/**
 * {@link Module} export object snapshot for ESM loader.
 */
const kModuleExport = module_export_private_symbol;
/**
 * {@link Module} parent module.
 */
const kModuleParent = module_parent_private_symbol;

const kIsMainSymbol = Symbol('kIsMainSymbol');
const kIsCachedByESMLoader = Symbol('kIsCachedByESMLoader');
const kRequiredModuleSymbol = Symbol('kRequiredModuleSymbol');
const kIsExecuting = Symbol('kIsExecuting');
// Set first due to cycle with ESM loader functions.
module.exports = {
  kModuleSource,
  kModuleExport,
  kModuleExportNames,
  kModuleCircularVisited,
  initializeCJS,
  Module,
  wrapSafe,
  wrapModuleLoad,
  kIsMainSymbol,
  kIsCachedByESMLoader,
  kRequiredModuleSymbol,
  kIsExecuting,
};

const { BuiltinModule } = require('internal/bootstrap/realm');
const {
  maybeCacheSourceMap,
} = require('internal/source_map/source_map_cache');
const { pathToFileURL, fileURLToPath, isURL } = require('internal/url');
const {
  pendingDeprecate,
  emitExperimentalWarning,
  kEmptyObject,
  setOwnProperty,
  getLazy,
  isWindows,
} = require('internal/util');
const {
  makeContextifyScript,
  runScriptInThisContext,
} = require('internal/vm');
const {
  containsModuleSyntax,
  compileFunctionForCJSLoader,
} = internalBinding('contextify');

const assert = require('internal/assert');
const fs = require('fs');
const path = require('path');
const internalFsBinding = internalBinding('fs');
const { safeGetenv } = internalBinding('credentials');
const {
  getCjsConditions,
  initializeCjsConditions,
  isUnderNodeModules,
  loadBuiltinModule,
  makeRequireFunction,
  setHasStartedUserCJSExecution,
  stripBOM,
  toRealPath,
} = require('internal/modules/helpers');
const packageJsonReader = require('internal/modules/package_json_reader');
const { getOptionValue, getEmbedderOptions } = require('internal/options');
const shouldReportRequiredModules = getLazy(() => process.env.WATCH_REPORT_DEPENDENCIES);

const permission = require('internal/process/permission');
const {
  vm_dynamic_import_default_internal,
} = internalBinding('symbols');

const {
  codes: {
    ERR_INVALID_ARG_VALUE,
    ERR_INVALID_MODULE_SPECIFIER,
    ERR_REQUIRE_CYCLE_MODULE,
    ERR_REQUIRE_ESM,
    ERR_UNKNOWN_BUILTIN_MODULE,
    ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING,
  },
  setArrowMessage,
} = require('internal/errors');
const { validateString } = require('internal/validators');

const {
  CHAR_BACKWARD_SLASH,
  CHAR_COLON,
  CHAR_DOT,
  CHAR_FORWARD_SLASH,
} = require('internal/constants');

const {
  isProxy,
} = require('internal/util/types');

const { debuglog, debugWithTimer } = require('internal/util/debuglog');

let { startTimer, endTimer } = debugWithTimer('module_timer', (start, end) => {
  startTimer = start;
  endTimer = end;
});

const { tracingChannel } = require('diagnostics_channel');
const onRequire = getLazy(() => tracingChannel('module.require'));

const relativeResolveCache = { __proto__: null };

let requireDepth = 0;
let isPreloading = false;
let statCache = null;

/**
 * Internal method to add tracing capabilities for Module._load.
 *
 * See more {@link Module._load}
 */
function wrapModuleLoad(request, parent, isMain) {
  const logLabel = `[${parent?.id || ''}] [${request}]`;
  const traceLabel = `require('${request}')`;

  startTimer(logLabel, traceLabel);

  try {
    return onRequire().traceSync(Module._load, {
      __proto__: null,
      parentFilename: parent?.filename,
      id: request,
    }, Module, request, parent, isMain);
  } finally {
    endTimer(logLabel, traceLabel);
  }
}

/**
 * Get a path's properties, using an in-memory cache to minimize lookups.
 * @param {string} filename Absolute path to the file
 */
function stat(filename) {
  filename = path.toNamespacedPath(filename);
  if (statCache !== null) {
    const result = statCache.get(filename);
    if (result !== undefined) { return result; }
  }
  const result = internalFsBinding.internalModuleStat(filename);
  if (statCache !== null && result >= 0) {
    // Only set cache when `internalModuleStat(filename)` succeeds.
    statCache.set(filename, result);
  }
  return result;
}

let _stat = stat;
ObjectDefineProperty(Module, '_stat', {
  __proto__: null,
  get() { return _stat; },
  set(stat) {
    emitExperimentalWarning('Module._stat');
    _stat = stat;
    return true;
  },
  configurable: true,
});

/**
 * Update the parent's children array with the child module.
 * @param {Module} parent Module requiring the children
 * @param {Module} child Module being required
 * @param {boolean} scan Add the child to the parent's children if not already present
 */
function updateChildren(parent, child, scan) {
  const children = parent?.children;
  if (children && !(scan && ArrayPrototypeIncludes(children, child))) {
    ArrayPrototypePush(children, child);
  }
}

/**
 * Tell the watch mode that a module was required.
 * @param {string} filename Absolute path of the module
 */
function reportModuleToWatchMode(filename) {
  if (shouldReportRequiredModules() && process.send) {
    process.send({ 'watch:require': [filename] });
  }
}

/**
 * Tell the watch mode that a module was not found.
 * @param {string} basePath The absolute path that errored
 * @param {string[]} extensions The extensions that were tried
 */
function reportModuleNotFoundToWatchMode(basePath, extensions) {
  if (shouldReportRequiredModules() && process.send) {
    process.send({ 'watch:require': ArrayPrototypeMap(extensions, (ext) => path.resolve(`${basePath}${ext}`)) });
  }
}

/**
 * Create a new module instance.
 * @param {string} id
 * @param {Module} parent
 */
function Module(id = '', parent) {
  this.id = id;
  this.path = path.dirname(id);
  setOwnProperty(this, 'exports', {});
  this[kModuleParent] = parent;
  updateChildren(parent, this, false);
  this.filename = null;
  this.loaded = false;
  this.children = [];
}

/** @type {Record<string, Module>} */
Module._cache = { __proto__: null };
/** @type {Record<string, string>} */
Module._pathCache = { __proto__: null };
/** @type {Record<string, (module: Module, filename: string) => void>} */
Module._extensions = { __proto__: null };
/** @type {string[]} */
let modulePaths = [];
/** @type {string[]} */
Module.globalPaths = [];

let patched = false;

/**
 * Add the CommonJS wrapper around a module's source code.
 * @param {string} script Module source code
 */
let wrap = function(script) { // eslint-disable-line func-style
  return Module.wrapper[0] + script + Module.wrapper[1];
};

const wrapper = [
  '(function (exports, require, module, __filename, __dirname) { ',
  '\n});',
];

let wrapperProxy = new Proxy(wrapper, {
  __proto__: null,

  set(target, property, value, receiver) {
    patched = true;
    return ReflectSet(target, property, value, receiver);
  },

  defineProperty(target, property, descriptor) {
    patched = true;
    return ObjectDefineProperty(target, property, descriptor);
  },
});

ObjectDefineProperty(Module, 'wrap', {
  __proto__: null,
  get() {
    return wrap;
  },

  set(value) {
    patched = true;
    wrap = value;
  },
});

ObjectDefineProperty(Module, 'wrapper', {
  __proto__: null,
  get() {
    return wrapperProxy;
  },

  set(value) {
    patched = true;
    wrapperProxy = value;
  },
});

const isPreloadingDesc = { get() { return isPreloading; } };
ObjectDefineProperty(Module.prototype, 'isPreloading', isPreloadingDesc);
ObjectDefineProperty(BuiltinModule.prototype, 'isPreloading', isPreloadingDesc);

/**
 * Get the parent of the current module from our cache.
 * @this {Module}
 */
function getModuleParent() {
  return this[kModuleParent];
}

/**
 * Set the parent of the current module in our cache.
 * @this {Module}
 * @param {Module} value
 */
function setModuleParent(value) {
  this[kModuleParent] = value;
}

let debug = debuglog('module', (fn) => {
  debug = fn;
});

ObjectDefineProperty(Module.prototype, 'parent', {
  __proto__: null,
  get: pendingDeprecate(
    getModuleParent,
    'module.parent is deprecated due to accuracy issues. Please use ' +
      'require.main to find program entry point instead.',
    'DEP0144',
  ),
  set: pendingDeprecate(
    setModuleParent,
    'module.parent is deprecated due to accuracy issues. Please use ' +
      'require.main to find program entry point instead.',
    'DEP0144',
  ),
});
Module._debug = pendingDeprecate(debug, 'Module._debug is deprecated.', 'DEP0077');
Module.isBuiltin = BuiltinModule.isBuiltin;

/**
 * Prepare to run CommonJS code.
 * This function is called during pre-execution, before any user code is run.
 */
function initializeCJS() {
  // This need to be done at runtime in case --expose-internals is set.
  const builtinModules = BuiltinModule.getCanBeRequiredByUsersWithoutSchemeList();
  Module.builtinModules = ObjectFreeze(builtinModules);

  initializeCjsConditions();

  if (!getEmbedderOptions().noGlobalSearchPaths) {
    Module._initPaths();
  }

  // TODO(joyeecheung): deprecate this in favor of a proper hook?
  Module.runMain =
    require('internal/modules/run_main').executeUserEntryPoint;

  const tsEnabled = getOptionValue('--experimental-strip-types');
  if (tsEnabled) {
    emitExperimentalWarning('Type Stripping');
    Module._extensions['.cts'] = loadCTS;
    Module._extensions['.ts'] = loadTS;
  }
  if (getOptionValue('--experimental-require-module')) {
    emitExperimentalWarning('Support for loading ES Module in require()');
    Module._extensions['.mjs'] = loadESMFromCJS;
    if (tsEnabled) {
      Module._extensions['.mts'] = loadESMFromCJS;
    }
  }
}

// Given a module name, and a list of paths to test, returns the first
// matching file in the following precedence.
//
// require("a.<ext>")
//   -> a.<ext>
//
// require("a")
//   -> a
//   -> a.<ext>
//   -> a/index.<ext>

let _readPackage = packageJsonReader.readPackage;
ObjectDefineProperty(Module, '_readPackage', {
  __proto__: null,
  get() { return _readPackage; },
  set(readPackage) {
    emitExperimentalWarning('Module._readPackage');
    _readPackage = readPackage;
    return true;
  },
  configurable: true,
});

/**
 * Try to load a specifier as a package.
 * @param {string} requestPath The path to what we are trying to load
 * @param {string[]} exts File extensions to try appending in order to resolve the file
 * @param {boolean} isMain Whether the file is the main entry point of the app
 * @param {string} originalPath The specifier passed to `require`
 */
function tryPackage(requestPath, exts, isMain, originalPath) {
  const { main: pkg, pjsonPath } = _readPackage(requestPath);

  if (!pkg) {
    return tryExtensions(path.resolve(requestPath, 'index'), exts, isMain);
  }

  const filename = path.resolve(requestPath, pkg);
  let actual = tryFile(filename, isMain) ||
    tryExtensions(filename, exts, isMain) ||
    tryExtensions(path.resolve(filename, 'index'), exts, isMain);
  if (actual === false) {
    actual = tryExtensions(path.resolve(requestPath, 'index'), exts, isMain);
    if (!actual) {
      // eslint-disable-next-line no-restricted-syntax
      const err = new Error(
        `Cannot find module '${filename}'. ` +
        'Please verify that the package.json has a valid "main" entry',
      );
      err.code = 'MODULE_NOT_FOUND';
      err.path = pjsonPath;
      err.requestPath = originalPath;
      // TODO(BridgeAR): Add the requireStack as well.
      throw err;
    } else {
      process.emitWarning(
        `Invalid 'main' field in '${pjsonPath}' of '${pkg}'. ` +
          'Please either fix that or report it to the module author',
        'DeprecationWarning',
        'DEP0128',
      );
    }
  }
  return actual;
}

/**
 * Check if the file exists and is not a directory if using `--preserve-symlinks` and `isMain` is false or
 * `--preserve-symlinks-main` and `isMain` is true , keep symlinks intact, otherwise resolve to the absolute realpath.
 * @param {string} requestPath The path to the file to load.
 * @param {boolean} isMain Whether the file is the main module.
 */
function tryFile(requestPath, isMain) {
  const rc = _stat(requestPath);
  if (rc !== 0) { return; }
  if (getOptionValue(isMain ? '--preserve-symlinks-main' : '--preserve-symlinks')) {
    return path.resolve(requestPath);
  }
  return toRealPath(requestPath);
}

/**
 * Given a path, check if the file exists with any of the set extensions.
 * @param {string} basePath The path and filename without extension
 * @param {string[]} exts The extensions to try
 * @param {boolean} isMain Whether the module is the main module
 */
function tryExtensions(basePath, exts, isMain) {
  for (let i = 0; i < exts.length; i++) {
    const filename = tryFile(basePath + exts[i], isMain);

    if (filename) {
      return filename;
    }
  }
  return false;
}

/**
 * Find the longest (possibly multi-dot) extension registered in `Module._extensions`.
 * @param {string} filename The filename to find the longest registered extension for.
 */
function findLongestRegisteredExtension(filename) {
  const name = path.basename(filename);
  let currentExtension;
  let index;
  let startIndex = 0;
  while ((index = StringPrototypeIndexOf(name, '.', startIndex)) !== -1) {
    startIndex = index + 1;
    if (index === 0) { continue; } // Skip dotfiles like .gitignore
    currentExtension = StringPrototypeSlice(name, index);
    if (Module._extensions[currentExtension]) { return currentExtension; }
  }
  return '.js';
}

/**
 * Tries to get the absolute file path of the parent module.
 * @param {Module} parent The parent module object.
 */
function trySelfParentPath(parent) {
  if (!parent) { return false; }

  if (parent.filename) {
    return parent.filename;
  } else if (parent.id === '<repl>' || parent.id === 'internal/preload') {
    try {
      return process.cwd() + path.sep;
    } catch {
      return false;
    }
  }
}

/**
 * Attempt to resolve a module request using the parent module package metadata.
 * @param {string} parentPath The path of the parent module
 * @param {string} request The module request to resolve
 */
function trySelf(parentPath, request) {
  if (!parentPath) { return false; }

  const pkg = packageJsonReader.getNearestParentPackageJSON(parentPath);
  if (pkg?.data.exports === undefined || pkg.data.name === undefined) {
    return false;
  }

  let expansion;
  if (request === pkg.data.name) {
    expansion = '.';
  } else if (StringPrototypeStartsWith(request, `${pkg.data.name}/`)) {
    expansion = '.' + StringPrototypeSlice(request, pkg.data.name.length);
  } else {
    return false;
  }

  try {
    const { packageExportsResolve } = require('internal/modules/esm/resolve');
    return finalizeEsmResolution(packageExportsResolve(
      pathToFileURL(pkg.path + '/package.json'), expansion, pkg.data,
      pathToFileURL(parentPath), getCjsConditions()), parentPath, pkg.path);
  } catch (e) {
    if (e.code === 'ERR_MODULE_NOT_FOUND') {
      throw createEsmNotFoundErr(request, pkg.path + '/package.json');
    }
    throw e;
  }
}

/**
 * This only applies to requests of a specific form:
 * 1. `name/.*`
 * 2. `@scope/name/.*`
 */
const EXPORTS_PATTERN = /^((?:@[^/\\%]+\/)?[^./\\%][^/\\%]*)(\/.*)?$/;

/**
 * Resolves the exports for a given module path and request.
 * @param {string} nmPath The path to the module.
 * @param {string} request The request for the module.
 */
function resolveExports(nmPath, request) {
  // The implementation's behavior is meant to mirror resolution in ESM.
  const { 1: name, 2: expansion = '' } =
    RegExpPrototypeExec(EXPORTS_PATTERN, request) || kEmptyObject;
  if (!name) { return; }
  const pkgPath = path.resolve(nmPath, name);
  const pkg = _readPackage(pkgPath);
  if (pkg.exists && pkg.exports != null) {
    try {
      const { packageExportsResolve } = require('internal/modules/esm/resolve');
      return finalizeEsmResolution(packageExportsResolve(
        pathToFileURL(pkgPath + '/package.json'), '.' + expansion, pkg, null,
        getCjsConditions()), null, pkgPath);
    } catch (e) {
      if (e.code === 'ERR_MODULE_NOT_FOUND') {
        throw createEsmNotFoundErr(request, pkgPath + '/package.json');
      }
      throw e;
    }
  }
}

// We don't cache this in case user extends the extensions.
function getDefaultExtensions() {
  let extensions = ObjectKeys(Module._extensions);
  const tsEnabled = getOptionValue('--experimental-strip-types');
  if (tsEnabled) {
    // remove .ts and .cts from the default extensions
    // to avoid extensionless require of .ts and .cts files.
    // it behaves similarly to how .mjs is handled when --experimental-require-module
    // is enabled.
    extensions = ArrayPrototypeFilter(extensions, (ext) =>
      (ext !== '.ts' || Module._extensions['.ts'] !== loadTS) &&
      (ext !== '.cts' || Module._extensions['.cts'] !== loadCTS),
    );
  }

  if (!getOptionValue('--experimental-require-module')) {
    return extensions;
  }

  if (tsEnabled) {
    extensions = ArrayPrototypeFilter(extensions, (ext) =>
      ext !== '.mts' || Module._extensions['.mts'] !== loadESMFromCJS,
    );
  }
  // If the .mjs extension is added by --experimental-require-module,
  // remove it from the supported default extensions to maintain
  // compatibility.
  // TODO(joyeecheung): allow both .mjs and .cjs?
  return ArrayPrototypeFilter(extensions, (ext) => ext !== '.mjs' || Module._extensions['.mjs'] !== loadESMFromCJS);
}

/**
 * Get the absolute path to a module.
 * @param {string} request Relative or absolute file path
 * @param {Array<string>} paths Folders to search as file paths
 * @param {boolean} isMain Whether the request is the main app entry point
 * @returns {string | false}
 */
Module._findPath = function(request, paths, isMain) {
  const absoluteRequest = path.isAbsolute(request);
  if (absoluteRequest) {
    paths = [''];
  } else if (!paths || paths.length === 0) {
    return false;
  }

  const cacheKey = request + '\x00' + ArrayPrototypeJoin(paths, '\x00');
  const entry = Module._pathCache[cacheKey];
  if (entry) {
    return entry;
  }

  let exts;
  const trailingSlash = request.length > 0 &&
    (StringPrototypeCharCodeAt(request, request.length - 1) === CHAR_FORWARD_SLASH || (
      StringPrototypeCharCodeAt(request, request.length - 1) === CHAR_DOT &&
      (
        request.length === 1 ||
        StringPrototypeCharCodeAt(request, request.length - 2) === CHAR_FORWARD_SLASH ||
        (StringPrototypeCharCodeAt(request, request.length - 2) === CHAR_DOT && (
          request.length === 2 ||
          StringPrototypeCharCodeAt(request, request.length - 3) === CHAR_FORWARD_SLASH
        ))
      )
    ));

  const isRelative = StringPrototypeCharCodeAt(request, 0) === CHAR_DOT &&
    (
      request.length === 1 ||
      StringPrototypeCharCodeAt(request, 1) === CHAR_FORWARD_SLASH ||
      (isWindows && StringPrototypeCharCodeAt(request, 1) === CHAR_BACKWARD_SLASH) ||
      (StringPrototypeCharCodeAt(request, 1) === CHAR_DOT && ((
        request.length === 2 ||
        StringPrototypeCharCodeAt(request, 2) === CHAR_FORWARD_SLASH) ||
        (isWindows && StringPrototypeCharCodeAt(request, 2) === CHAR_BACKWARD_SLASH)))
    );
  let insidePath = true;
  if (isRelative) {
    const normalizedRequest = path.normalize(request);
    if (StringPrototypeStartsWith(normalizedRequest, '..')) {
      insidePath = false;
    }
  }

  // For each path
  for (let i = 0; i < paths.length; i++) {
    // Don't search further if path doesn't exist
    // or doesn't have permission to it
    const curPath = paths[i];
    if (insidePath && curPath &&
      ((permission.isEnabled() && !permission.has('fs.read', curPath)) || _stat(curPath) < 1)
    ) {
      continue;
    }

    if (!absoluteRequest) {
      const exportsResolved = resolveExports(curPath, request);
      if (exportsResolved) {
        return exportsResolved;
      }
    }

    const basePath = path.resolve(curPath, request);
    let filename;

    const rc = _stat(basePath);
    if (!trailingSlash) {
      if (rc === 0) {  // File.
        if (!isMain) {
          if (getOptionValue('--preserve-symlinks')) {
            filename = path.resolve(basePath);
          } else {
            filename = toRealPath(basePath);
          }
        } else if (getOptionValue('--preserve-symlinks-main')) {
          // For the main module, we use the --preserve-symlinks-main flag instead
          // mainly for backward compatibility, as the preserveSymlinks flag
          // historically has not applied to the main module.  Most likely this
          // was intended to keep .bin/ binaries working, as following those
          // symlinks is usually required for the imports in the corresponding
          // files to resolve; that said, in some use cases following symlinks
          // causes bigger problems which is why the --preserve-symlinks-main option
          // is needed.
          filename = path.resolve(basePath);
        } else {
          filename = toRealPath(basePath);
        }
      }

      if (!filename) {
        // Try it with each of the extensions
        if (exts === undefined) {
          exts = getDefaultExtensions();
        }
        filename = tryExtensions(basePath, exts, isMain);
      }
    }

    if (!filename && rc === 1) {  // Directory.
      // try it with each of the extensions at "index"
      if (exts === undefined) {
        exts = getDefaultExtensions();
      }
      filename = tryPackage(basePath, exts, isMain, request);
    }

    if (filename) {
      Module._pathCache[cacheKey] = filename;
      return filename;
    }

    const extensions = [''];
    if (exts !== undefined) {
      ArrayPrototypePushApply(extensions, exts);
    }
    reportModuleNotFoundToWatchMode(basePath, extensions);
  }

  return false;
};

/** `node_modules` character codes reversed */
const nmChars = [ 115, 101, 108, 117, 100, 111, 109, 95, 101, 100, 111, 110 ];
const nmLen = nmChars.length;
if (isWindows) {
  /**
   * Get the paths to the `node_modules` folder for a given path.
   * @param {string} from `__dirname` of the module
   */
  Module._nodeModulePaths = function(from) {
    // Guarantee that 'from' is absolute.
    from = path.resolve(from);

    // note: this approach *only* works when the path is guaranteed
    // to be absolute.  Doing a fully-edge-case-correct path.split
    // that works on both Windows and Posix is non-trivial.

    // return root node_modules when path is 'D:\\'.
    // path.resolve will make sure from.length >=3 in Windows.
    if (StringPrototypeCharCodeAt(from, from.length - 1) ===
          CHAR_BACKWARD_SLASH &&
        StringPrototypeCharCodeAt(from, from.length - 2) === CHAR_COLON) {
      return [from + 'node_modules'];
    }

    /** @type {string[]} */
    const paths = [];
    for (let i = from.length - 1, p = 0, last = from.length; i >= 0; --i) {
      const code = StringPrototypeCharCodeAt(from, i);
      // The path segment separator check ('\' and '/') was used to get
      // node_modules path for every path segment.
      // Use colon as an extra condition since we can get node_modules
      // path for drive root like 'C:\node_modules' and don't need to
      // parse drive name.
      if (code === CHAR_BACKWARD_SLASH ||
          code === CHAR_FORWARD_SLASH ||
          code === CHAR_COLON) {
        if (p !== nmLen) {
          ArrayPrototypePush(
            paths,
            StringPrototypeSlice(from, 0, last) + '\\node_modules',
          );
        }
        last = i;
        p = 0;
      } else if (p !== -1) {
        if (nmChars[p] === code) {
          ++p;
        } else {
          p = -1;
        }
      }
    }

    return paths;
  };
} else { // posix
  /**
   * Get the paths to the `node_modules` folder for a given path.
   * @param {string} from `__dirname` of the module
   */
  Module._nodeModulePaths = function(from) {
    // Guarantee that 'from' is absolute.
    from = path.resolve(from);
    // Return early not only to avoid unnecessary work, but to *avoid* returning
    // an array of two items for a root: [ '//node_modules', '/node_modules' ]
    if (from === '/') {
      return ['/node_modules'];
    }

    // note: this approach *only* works when the path is guaranteed
    // to be absolute.  Doing a fully-edge-case-correct path.split
    // that works on both Windows and Posix is non-trivial.
    /** @type {string[]} */
    const paths = [];
    for (let i = from.length - 1, p = 0, last = from.length; i >= 0; --i) {
      const code = StringPrototypeCharCodeAt(from, i);
      if (code === CHAR_FORWARD_SLASH) {
        if (p !== nmLen) {
          ArrayPrototypePush(
            paths,
            StringPrototypeSlice(from, 0, last) + '/node_modules',
          );
        }
        last = i;
        p = 0;
      } else if (p !== -1) {
        if (nmChars[p] === code) {
          ++p;
        } else {
          p = -1;
        }
      }
    }

    // Append /node_modules to handle root paths.
    ArrayPrototypePush(paths, '/node_modules');

    return paths;
  };
}

/**
 * Get the paths for module resolution.
 * @param {string} request
 * @param {Module} parent
 */
Module._resolveLookupPaths = function(request, parent) {
  if (BuiltinModule.normalizeRequirableId(request)) {
    debug('looking for %j in []', request);
    return null;
  }

  // Check for node modules paths.
  if (StringPrototypeCharAt(request, 0) !== '.' ||
      (request.length > 1 &&
      StringPrototypeCharAt(request, 1) !== '.' &&
      StringPrototypeCharAt(request, 1) !== '/' &&
      (!isWindows || StringPrototypeCharAt(request, 1) !== '\\'))) {

    /** @type {string[]} */
    let paths;
    if (parent?.paths?.length) {
      paths = ArrayPrototypeSlice(modulePaths);
      ArrayPrototypeUnshiftApply(paths, parent.paths);
    } else {
      paths = modulePaths;
    }

    debug('looking for %j in %j', request, paths);
    return paths.length > 0 ? paths : null;
  }

  // In REPL, parent.filename is null.
  if (!parent || !parent.id || !parent.filename) {
    // Make require('./path/to/foo') work - normally the path is taken
    // from realpath(__filename) but in REPL there is no filename
    const mainPaths = ['.'];

    debug('looking for %j in %j', request, mainPaths);
    return mainPaths;
  }

  debug('RELATIVE: requested: %s from parent.id %s', request, parent.id);

  const parentDir = [path.dirname(parent.filename)];
  debug('looking for %j', parentDir);
  return parentDir;
};

/**
 * Emits a warning when a non-existent property of module exports is accessed inside a circular dependency.
 * @param {string} prop The name of the non-existent property.
 */
function emitCircularRequireWarning(prop) {
  process.emitWarning(
    `Accessing non-existent property '${String(prop)}' of module exports ` +
    'inside circular dependency',
  );
}

// A Proxy that can be used as the prototype of a module.exports object and
// warns when non-existent properties are accessed.
const CircularRequirePrototypeWarningProxy = new Proxy({}, {
  __proto__: null,

  get(target, prop) {
    // Allow __esModule access in any case because it is used in the output
    // of transpiled code to determine whether something comes from an
    // ES module, and is not used as a regular key of `module.exports`.
    if (prop in target || prop === '__esModule') { return target[prop]; }
    emitCircularRequireWarning(prop);
    return undefined;
  },

  getOwnPropertyDescriptor(target, prop) {
    if (ObjectPrototypeHasOwnProperty(target, prop) || prop === '__esModule') {
      return ObjectGetOwnPropertyDescriptor(target, prop);
    }
    emitCircularRequireWarning(prop);
    return undefined;
  },
});

/**
 * Returns the exports object for a module that has a circular `require`.
 * If the exports object is a plain object, it is wrapped in a proxy that warns
 * about circular dependencies.
 * @param {Module} module The module instance
 */
function getExportsForCircularRequire(module) {
  const requiredESM = module[kRequiredModuleSymbol];
  if (requiredESM && requiredESM.getStatus() !== kEvaluated) {
    let message = `Cannot require() ES Module ${module.id} in a cycle.`;
    const parent = module[kModuleParent];
    if (parent) {
      message += ` (from ${parent.filename})`;
    }
    throw new ERR_REQUIRE_CYCLE_MODULE(message);
  }

  if (module.exports &&
      !isProxy(module.exports) &&
      ObjectGetPrototypeOf(module.exports) === ObjectPrototype &&
      // Exclude transpiled ES6 modules / TypeScript code because those may
      // employ unusual patterns for accessing 'module.exports'. That should
      // be okay because ES6 modules have a different approach to circular
      // dependencies anyway.
      !module.exports.__esModule) {
    // This is later unset once the module is done loading.
    ObjectSetPrototypeOf(
      module.exports, CircularRequirePrototypeWarningProxy);
  }

  return module.exports;
}

/**
 * Load a module from cache if it exists, otherwise create a new module instance.
 * 1. If a module already exists in the cache: return its exports object.
 * 2. If the module is native: call
 *    `BuiltinModule.prototype.compileForPublicLoader()` and return the exports.
 * 3. Otherwise, create a new module for the file and save it to the cache.
 *    Then have it load the file contents before returning its exports object.
 * @param {string} request Specifier of module to load via `require`
 * @param {Module} parent Absolute path of the module importing the child
 * @param {boolean} isMain Whether the module is the main entry point
 */
Module._load = function(request, parent, isMain) {
  let relResolveCacheIdentifier;
  if (parent) {
    debug('Module._load REQUEST %s parent: %s', request, parent.id);
    // Fast path for (lazy loaded) modules in the same directory. The indirect
    // caching is required to allow cache invalidation without changing the old
    // cache key names.
    relResolveCacheIdentifier = `${parent.path}\x00${request}`;
    const filename = relativeResolveCache[relResolveCacheIdentifier];
    reportModuleToWatchMode(filename);
    if (filename !== undefined) {
      const cachedModule = Module._cache[filename];
      if (cachedModule !== undefined) {
        updateChildren(parent, cachedModule, true);
        if (!cachedModule.loaded) {
          return getExportsForCircularRequire(cachedModule);
        }
        return cachedModule.exports;
      }
      delete relativeResolveCache[relResolveCacheIdentifier];
    }
  }

  if (StringPrototypeStartsWith(request, 'node:')) {
    // Slice 'node:' prefix
    const id = StringPrototypeSlice(request, 5);

    if (!BuiltinModule.canBeRequiredByUsers(id)) {
      throw new ERR_UNKNOWN_BUILTIN_MODULE(request);
    }

    const module = loadBuiltinModule(id, request);
    return module.exports;
  }

  const filename = Module._resolveFilename(request, parent, isMain);
  const cachedModule = Module._cache[filename];
  if (cachedModule !== undefined) {
    updateChildren(parent, cachedModule, true);
    if (cachedModule.loaded) {
      return cachedModule.exports;
    }
    // If it's not cached by the ESM loader, the loading request
    // comes from required CJS, and we can consider it a circular
    // dependency when it's cached.
    if (!cachedModule[kIsCachedByESMLoader]) {
      return getExportsForCircularRequire(cachedModule);
    }
    // If it's cached by the ESM loader as a way to indirectly pass
    // the module in to avoid creating it twice, the loading request
    // came from imported CJS. In that case use the kModuleCircularVisited
    // to determine if it's loading or not.
    if (cachedModule[kModuleCircularVisited]) {
      return getExportsForCircularRequire(cachedModule);
    }
    // This is an ESM loader created cache entry, mark it as visited and fallthrough to loading the module.
    cachedModule[kModuleCircularVisited] = true;
  }

  if (BuiltinModule.canBeRequiredWithoutScheme(filename)) {
    const mod = loadBuiltinModule(filename, request);
    return mod.exports;
  }

  // Don't call updateChildren(), Module constructor already does.
  const module = cachedModule || new Module(filename, parent);

  if (!cachedModule) {
    if (isMain) {
      setOwnProperty(process, 'mainModule', module);
      setOwnProperty(module.require, 'main', process.mainModule);
      module.id = '.';
      module[kIsMainSymbol] = true;
    } else {
      module[kIsMainSymbol] = false;
    }

    reportModuleToWatchMode(filename);
    Module._cache[filename] = module;
    module[kIsCachedByESMLoader] = false;
  }

  if (parent !== undefined) {
    relativeResolveCache[relResolveCacheIdentifier] = filename;
  }

  let threw = true;
  try {
    module.load(filename);
    threw = false;
  } finally {
    if (threw) {
      delete Module._cache[filename];
      if (parent !== undefined) {
        delete relativeResolveCache[relResolveCacheIdentifier];
        const children = parent?.children;
        if (ArrayIsArray(children)) {
          const index = ArrayPrototypeIndexOf(children, module);
          if (index !== -1) {
            ArrayPrototypeSplice(children, index, 1);
          }
        }
      }
    } else if (module.exports &&
               !isProxy(module.exports) &&
               ObjectGetPrototypeOf(module.exports) ===
                 CircularRequirePrototypeWarningProxy) {
      ObjectSetPrototypeOf(module.exports, ObjectPrototype);
    }
  }

  return module.exports;
};

/**
 * Given a `require` string and its context, get its absolute file path.
 * @param {string} request The specifier to resolve
 * @param {Module} parent The module containing the `require` call
 * @param {boolean} isMain Whether the module is the main entry point
 * @param {ResolveFilenameOptions} options Options object
 * @typedef {object} ResolveFilenameOptions
 * @property {string[]} paths Paths to search for modules in
 */
Module._resolveFilename = function(request, parent, isMain, options) {
  if (BuiltinModule.normalizeRequirableId(request)) {
    return request;
  }

  let paths;

  if (typeof options === 'object' && options !== null) {
    if (ArrayIsArray(options.paths)) {
      const isRelative = StringPrototypeStartsWith(request, './') ||
          StringPrototypeStartsWith(request, '../') ||
          ((isWindows && StringPrototypeStartsWith(request, '.\\')) ||
          StringPrototypeStartsWith(request, '..\\'));

      if (isRelative) {
        paths = options.paths;
      } else {
        const fakeParent = new Module('', null);

        paths = [];

        for (let i = 0; i < options.paths.length; i++) {
          const path = options.paths[i];
          fakeParent.paths = Module._nodeModulePaths(path);
          const lookupPaths = Module._resolveLookupPaths(request, fakeParent);

          for (let j = 0; j < lookupPaths.length; j++) {
            if (!ArrayPrototypeIncludes(paths, lookupPaths[j])) {
              ArrayPrototypePush(paths, lookupPaths[j]);
            }
          }
        }
      }
    } else if (options.paths === undefined) {
      paths = Module._resolveLookupPaths(request, parent);
    } else {
      throw new ERR_INVALID_ARG_VALUE('options.paths', options.paths);
    }
  } else {
    paths = Module._resolveLookupPaths(request, parent);
  }

  if (request[0] === '#' && (parent?.filename || parent?.id === '<repl>')) {
    const parentPath = parent?.filename ?? process.cwd() + path.sep;
    const pkg = packageJsonReader.getNearestParentPackageJSON(parentPath) || { __proto__: null };
    if (pkg.data?.imports != null) {
      try {
        const { packageImportsResolve } = require('internal/modules/esm/resolve');
        return finalizeEsmResolution(
          packageImportsResolve(request, pathToFileURL(parentPath),
                                getCjsConditions()), parentPath,
          pkg.path);
      } catch (e) {
        if (e.code === 'ERR_MODULE_NOT_FOUND') {
          throw createEsmNotFoundErr(request);
        }
        throw e;
      }
    }
  }

  // Try module self resolution first
  const parentPath = trySelfParentPath(parent);
  const selfResolved = trySelf(parentPath, request);
  if (selfResolved) {
    const cacheKey = request + '\x00' +
         (paths.length === 1 ? paths[0] : ArrayPrototypeJoin(paths, '\x00'));
    Module._pathCache[cacheKey] = selfResolved;
    return selfResolved;
  }

  // Look up the filename first, since that's the cache key.
  const filename = Module._findPath(request, paths, isMain);
  if (filename) { return filename; }
  const requireStack = [];
  for (let cursor = parent;
    cursor;
    cursor = cursor[kModuleParent]) {
    ArrayPrototypePush(requireStack, cursor.filename || cursor.id);
  }
  let message = `Cannot find module '${request}'`;
  if (requireStack.length > 0) {
    message = message + '\nRequire stack:\n- ' +
              ArrayPrototypeJoin(requireStack, '\n- ');
  }
  // eslint-disable-next-line no-restricted-syntax
  const err = new Error(message);
  err.code = 'MODULE_NOT_FOUND';
  err.requireStack = requireStack;
  throw err;
};

/**
 * Finishes resolving an ES module specifier into an absolute file path.
 * @param {string} resolved The resolved module specifier
 * @param {string} parentPath The path of the parent module
 * @param {string} pkgPath The path of the package.json file
 * @throws {ERR_INVALID_MODULE_SPECIFIER} If the resolved module specifier contains encoded `/` or `\\` characters
 * @throws {Error} If the module cannot be found
 */
function finalizeEsmResolution(resolved, parentPath, pkgPath) {
  const { encodedSepRegEx } = require('internal/modules/esm/resolve');
  if (RegExpPrototypeExec(encodedSepRegEx, resolved) !== null) {
    throw new ERR_INVALID_MODULE_SPECIFIER(
      resolved, 'must not include encoded "/" or "\\" characters', parentPath);
  }
  const filename = fileURLToPath(resolved);
  const actual = tryFile(filename);
  if (actual) {
    return actual;
  }
  const err = createEsmNotFoundErr(filename,
                                   path.resolve(pkgPath, 'package.json'));
  throw err;
}

/**
 * Creates an error object for when a requested ES module cannot be found.
 * @param {string} request The name of the requested module
 * @param {string} [path] The path to the requested module
 */
function createEsmNotFoundErr(request, path) {
  // eslint-disable-next-line no-restricted-syntax
  const err = new Error(`Cannot find module '${request}'`);
  err.code = 'MODULE_NOT_FOUND';
  if (path) {
    err.path = path;
  }
  // TODO(BridgeAR): Add the requireStack as well.
  return err;
}

/**
 * Given a file name, pass it to the proper extension handler.
 * @param {string} filename The `require` specifier
 */
Module.prototype.load = function(filename) {
  debug('load %j for module %j', filename, this.id);

  assert(!this.loaded);
  this.filename = filename;
  this.paths = Module._nodeModulePaths(path.dirname(filename));

  const extension = findLongestRegisteredExtension(filename);
  // allow .mjs to be overridden
  if (StringPrototypeEndsWith(filename, '.mjs') && !Module._extensions['.mjs']) {
    throw new ERR_REQUIRE_ESM(filename, true);
  }

  if (getOptionValue('--experimental-strip-types')) {
    if (StringPrototypeEndsWith(filename, '.mts') && !Module._extensions['.mts']) {
      throw new ERR_REQUIRE_ESM(filename, true);
    }
  }

  Module._extensions[extension](this, filename);
  this.loaded = true;

  // Create module entry at load time to snapshot exports correctly
  const exports = this.exports;
  // Preemptively cache for ESM loader.
  this[kModuleExport] = exports;
};

/**
 * Loads a module at the given file path. Returns that module's `exports` property.
 * @param {string} id
 * @throws {ERR_INVALID_ARG_TYPE} When `id` is not a string
 */
Module.prototype.require = function(id) {
  validateString(id, 'id');
  if (id === '') {
    throw new ERR_INVALID_ARG_VALUE('id', id,
                                    'must be a non-empty string');
  }
  requireDepth++;
  try {
    return wrapModuleLoad(id, this, /* isMain */ false);
  } finally {
    requireDepth--;
  }
};

/**
 * Resolved path to `process.argv[1]` will be lazily placed here
 * (needed for setting breakpoint when called with `--inspect-brk`).
 * @type {string | undefined}
 */
let resolvedArgv;
let hasPausedEntry = false;
/** @type {import('vm').Script} */

/**
 * Resolve and evaluate it synchronously as ESM if it's ESM.
 * @param {Module} mod CJS module instance
 * @param {string} filename Absolute path of the file.
 */
function loadESMFromCJS(mod, filename) {
  let source = getMaybeCachedSource(mod, filename);
  if (getOptionValue('--experimental-strip-types') && path.extname(filename) === '.mts') {
    if (isUnderNodeModules(filename)) {
      throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(filename);
    }
    const { tsParse } = require('internal/modules/helpers');
    source = tsParse(source);
  }
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  const isMain = mod[kIsMainSymbol];
  if (isMain) {
    require('internal/modules/run_main').runEntryPointWithESMLoader((cascadedLoader) => {
      const mainURL = pathToFileURL(filename).href;
      cascadedLoader.import(mainURL, undefined, { __proto__: null }, true);
    });
    // ESM won't be accessible via process.mainModule.
    setOwnProperty(process, 'mainModule', undefined);
  } else {
    const {
      wrap,
      namespace,
    } = cascadedLoader.importSyncForRequire(mod, filename, source, isMain, mod[kModuleParent]);
    // Tooling in the ecosystem have been using the __esModule property to recognize
    // transpiled ESM in consuming code. For example, a 'log' package written in ESM:
    //
    // export default function log(val) { console.log(val); }
    //
    // Can be transpiled as:
    //
    // exports.__esModule = true;
    // exports.default = function log(val) { console.log(val); }
    //
    // The consuming code may be written like this in ESM:
    //
    // import log from 'log'
    //
    // Which gets transpiled to:
    //
    // const _mod = require('log');
    // const log = _mod.__esModule ? _mod.default : _mod;
    //
    // So to allow transpiled consuming code to recognize require()'d real ESM
    // as ESM and pick up the default exports, we add a __esModule property by
    // building a source text module facade for any module that has a default
    // export and add .__esModule = true to the exports. This maintains the
    // enumerability of the re-exported names and the live binding of the exports,
    // without incurring a non-trivial per-access overhead on the exports.
    //
    // The source of the facade is defined as a constant per-isolate property
    // required_module_default_facade_source_string, which looks like this
    //
    // export * from 'original';
    // export { default } from 'original';
    // export const __esModule = true;
    //
    // And the 'original' module request is always resolved by
    // createRequiredModuleFacade() to `wrap` which is a ModuleWrap wrapping
    // over the original module.

    // We don't do this to modules that don't have default exports to avoid
    // the unnecessary overhead. If __esModule is already defined, we will
    // also skip the extension to allow users to override it.
    if (!ObjectHasOwn(namespace, 'default') || ObjectHasOwn(namespace, '__esModule')) {
      mod.exports = namespace;
    } else {
      mod.exports = createRequiredModuleFacade(wrap);
    }
  }
}

/**
 * Wraps the given content in a script and runs it in a new context.
 * @param {string} filename The name of the file being loaded
 * @param {string} content The content of the file being loaded
 * @param {Module|undefined} cjsModuleInstance The CommonJS loader instance
 * @param {'commonjs'|undefined} format Intended format of the module.
 */
function wrapSafe(filename, content, cjsModuleInstance, format) {
  assert(format !== 'module');  // ESM should be handled in loadESMFromCJS().
  const hostDefinedOptionId = vm_dynamic_import_default_internal;
  const importModuleDynamically = vm_dynamic_import_default_internal;
  if (patched) {
    const wrapped = Module.wrap(content);
    const script = makeContextifyScript(
      wrapped,                 // code
      filename,                // filename
      0,                       // lineOffset
      0,                       // columnOffset
      undefined,               // cachedData
      false,                   // produceCachedData
      undefined,               // parsingContext
      hostDefinedOptionId,     // hostDefinedOptionId
      importModuleDynamically, // importModuleDynamically
    );

    // Cache the source map for the module if present.
    const { sourceMapURL } = script;
    if (sourceMapURL) {
      maybeCacheSourceMap(filename, content, cjsModuleInstance, false, undefined, sourceMapURL);
    }

    return {
      __proto__: null,
      function: runScriptInThisContext(script, true, false),
      sourceMapURL,
    };
  }

  const shouldDetectModule = (format !== 'commonjs' && getOptionValue('--experimental-detect-module'));
  const result = compileFunctionForCJSLoader(content, filename, false /* is_sea_main */, shouldDetectModule);

  // Cache the source map for the module if present.
  if (result.sourceMapURL) {
    maybeCacheSourceMap(filename, content, cjsModuleInstance, false, undefined, result.sourceMapURL);
  }

  return result;
}

/**
 * Run the file contents in the correct scope or sandbox. Expose the correct helper variables (`require`, `module`,
 * `exports`) to the file. Returns exception, if any.
 * @param {string} content The source code of the module
 * @param {string} filename The file path of the module
 * @param {'module'|'commonjs'|undefined} format Intended format of the module.
 */
Module.prototype._compile = function(content, filename, format) {
  let redirects;

  let compiledWrapper;
  if (format !== 'module') {
    const result = wrapSafe(filename, content, this, format);
    compiledWrapper = result.function;
    if (result.canParseAsESM) {
      format = 'module';
    }
  }

  // TODO(joyeecheung): when the module is the entry point, consider allowing TLA.
  // Only modules being require()'d really need to avoid TLA.
  if (format === 'module') {
    // Pass the source into the .mjs extension handler indirectly through the cache.
    this[kModuleSource] = content;
    loadESMFromCJS(this, filename);
    return;
  }

  // TODO(joyeecheung): the detection below is unnecessarily complex. Using the
  // kIsMainSymbol, or a kBreakOnStartSymbol that gets passed from
  // higher level instead of doing hacky detection here.
  let inspectorWrapper = null;
  if (getOptionValue('--inspect-brk') && process._eval == null) {
    if (!resolvedArgv) {
      // We enter the repl if we're not given a filename argument.
      if (process.argv[1]) {
        try {
          resolvedArgv = Module._resolveFilename(process.argv[1], null, false);
        } catch {
          // We only expect this codepath to be reached in the case of a
          // preloaded module (it will fail earlier with the main entry)
          assert(ArrayIsArray(getOptionValue('--require')));
        }
      } else {
        resolvedArgv = 'repl';
      }
    }

    // Set breakpoint on module start
    if (resolvedArgv && !hasPausedEntry && filename === resolvedArgv) {
      hasPausedEntry = true;
      inspectorWrapper = internalBinding('inspector').callAndPauseOnStart;
    }
  }
  const dirname = path.dirname(filename);
  const require = makeRequireFunction(this, redirects);
  let result;
  const exports = this.exports;
  const thisValue = exports;
  const module = this;
  if (requireDepth === 0) { statCache = new SafeMap(); }
  setHasStartedUserCJSExecution();
  this[kIsExecuting] = true;
  if (inspectorWrapper) {
    result = inspectorWrapper(compiledWrapper, thisValue, exports,
                              require, module, filename, dirname);
  } else {
    result = ReflectApply(compiledWrapper, thisValue,
                          [exports, require, module, filename, dirname]);
  }
  this[kIsExecuting] = false;
  if (requireDepth === 0) { statCache = null; }
  return result;
};

/**
 * Get the source code of a module, using cached ones if it's cached.
 * @param {Module} mod Module instance whose source is potentially already cached.
 * @param {string} filename Absolute path to the file of the module.
 * @returns {string}
 */
function getMaybeCachedSource(mod, filename) {
  // If already analyzed the source, then it will be cached.
  let content;
  if (mod[kModuleSource] !== undefined) {
    content = mod[kModuleSource];
    mod[kModuleSource] = undefined;
  } else {
    // TODO(joyeecheung): we can read a buffer instead to speed up
    // compilation.
    content = fs.readFileSync(filename, 'utf8');
  }
  return content;
}

function loadCTS(module, filename) {
  if (isUnderNodeModules(filename)) {
    throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(filename);
  }
  const source = getMaybeCachedSource(module, filename);
  const { tsParse } = require('internal/modules/helpers');
  const content = tsParse(source);
  module._compile(content, filename, 'commonjs');
}

/**
 * Built-in handler for `.ts` files.
 * @param {Module} module The module to compile
 * @param {string} filename The file path of the module
 */
function loadTS(module, filename) {
  if (isUnderNodeModules(filename)) {
    throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(filename);
  }
  // If already analyzed the source, then it will be cached.
  const source = getMaybeCachedSource(module, filename);
  const { tsParse } = require('internal/modules/helpers');
  const content = tsParse(source);
  let format;
  const pkg = packageJsonReader.getNearestParentPackageJSON(filename);
  // Function require shouldn't be used in ES modules.
  if (pkg?.data.type === 'module') {
    if (getOptionValue('--experimental-require-module')) {
      module._compile(content, filename, 'module');
      return;
    }

    const parent = module[kModuleParent];
    const parentPath = parent?.filename;
    const packageJsonPath = path.resolve(pkg.path, 'package.json');
    const usesEsm = containsModuleSyntax(content, filename);
    const err = new ERR_REQUIRE_ESM(filename, usesEsm, parentPath,
                                    packageJsonPath);
      // Attempt to reconstruct the parent require frame.
    if (Module._cache[parentPath]) {
      let parentSource;
      try {
        parentSource = tsParse(fs.readFileSync(parentPath, 'utf8'));
      } catch {
        // Continue regardless of error.
      }
      if (parentSource) {
        reconstructErrorStack(err, parentPath, parentSource);
      }
    }
    throw err;
  } else if (pkg?.data.type === 'commonjs') {
    format = 'commonjs';
  }

  module._compile(content, filename, format);
};

function reconstructErrorStack(err, parentPath, parentSource) {
  const errLine = StringPrototypeSplit(
    StringPrototypeSlice(err.stack, StringPrototypeIndexOf(
      err.stack, '    at ')), '\n', 1)[0];
  const { 1: line, 2: col } =
    RegExpPrototypeExec(/(\d+):(\d+)\)/, errLine) || [];
  if (line && col) {
    const srcLine = StringPrototypeSplit(parentSource, '\n')[line - 1];
    const frame = `${parentPath}:${line}\n${srcLine}\n${StringPrototypeRepeat(' ', col - 1)}^\n`;
    setArrowMessage(err, frame);
  }
}

/**
 * Built-in handler for `.js` files.
 * @param {Module} module The module to compile
 * @param {string} filename The file path of the module
 */
Module._extensions['.js'] = function(module, filename) {
  // If already analyzed the source, then it will be cached.
  const content = getMaybeCachedSource(module, filename);

  let format;
  if (StringPrototypeEndsWith(filename, '.js')) {
    const pkg = packageJsonReader.getNearestParentPackageJSON(filename);
    // Function require shouldn't be used in ES modules.
    if (pkg?.data.type === 'module') {
      if (getOptionValue('--experimental-require-module')) {
        module._compile(content, filename, 'module');
        return;
      }

      // This is an error path because `require` of a `.js` file in a `"type": "module"` scope is not allowed.
      const parent = module[kModuleParent];
      const parentPath = parent?.filename;
      const packageJsonPath = path.resolve(pkg.path, 'package.json');
      const usesEsm = containsModuleSyntax(content, filename);
      const err = new ERR_REQUIRE_ESM(filename, usesEsm, parentPath,
                                      packageJsonPath);
      // Attempt to reconstruct the parent require frame.
      if (Module._cache[parentPath]) {
        let parentSource;
        try {
          parentSource = fs.readFileSync(parentPath, 'utf8');
        } catch {
          // Continue regardless of error.
        }
        if (parentSource) {
          reconstructErrorStack(err, parentPath, parentSource);
        }
      }
      throw err;
    } else if (pkg?.data.type === 'commonjs') {
      format = 'commonjs';
    }
  } else if (StringPrototypeEndsWith(filename, '.cjs')) {
    format = 'commonjs';
  }

  module._compile(content, filename, format);
};

/**
 * Native handler for `.json` files.
 * @param {Module} module The module to compile
 * @param {string} filename The file path of the module
 */
Module._extensions['.json'] = function(module, filename) {
  const content = fs.readFileSync(filename, 'utf8');

  try {
    setOwnProperty(module, 'exports', JSONParse(stripBOM(content)));
  } catch (err) {
    err.message = filename + ': ' + err.message;
    throw err;
  }
};

/**
 * Native handler for `.node` files.
 * @param {Module} module The module to compile
 * @param {string} filename The file path of the module
 */
Module._extensions['.node'] = function(module, filename) {
  // Be aware this doesn't use `content`
  return process.dlopen(module, path.toNamespacedPath(filename));
};

/**
 * Creates a `require` function that can be used to load modules from the specified path.
 * @param {string} filename The path to the module
 */
function createRequireFromPath(filename) {
  // Allow a directory to be passed as the filename
  const trailingSlash =
    StringPrototypeEndsWith(filename, '/') ||
    (isWindows && StringPrototypeEndsWith(filename, '\\'));

  const proxyPath = trailingSlash ?
    path.join(filename, 'noop.js') :
    filename;

  const m = new Module(proxyPath);
  m.filename = proxyPath;

  m.paths = Module._nodeModulePaths(m.path);
  return makeRequireFunction(m, null);
}

const createRequireError = 'must be a file URL object, file URL string, or ' +
  'absolute path string';

/**
 * Creates a new `require` function that can be used to load modules.
 * @param {string | URL} filename The path or URL to the module context for this `require`
 * @throws {ERR_INVALID_ARG_VALUE} If `filename` is not a string or URL, or if it is a relative path that cannot be
 * resolved to an absolute path.
 */
function createRequire(filename) {
  let filepath;

  if (isURL(filename) ||
      (typeof filename === 'string' && !path.isAbsolute(filename))) {
    try {
      filepath = fileURLToPath(filename);
    } catch {
      throw new ERR_INVALID_ARG_VALUE('filename', filename,
                                      createRequireError);
    }
  } else if (typeof filename !== 'string') {
    throw new ERR_INVALID_ARG_VALUE('filename', filename, createRequireError);
  } else {
    filepath = filename;
  }
  return createRequireFromPath(filepath);
}

Module.createRequire = createRequire;

/**
 * Define the paths to use for resolving a module.
 */
Module._initPaths = function() {
  const homeDir = isWindows ? process.env.USERPROFILE : safeGetenv('HOME');
  const nodePath = isWindows ? process.env.NODE_PATH : safeGetenv('NODE_PATH');

  // process.execPath is $PREFIX/bin/node except on Windows where it is
  // $PREFIX\node.exe where $PREFIX is the root of the Node.js installation.
  const prefixDir = isWindows ?
    path.resolve(process.execPath, '..') :
    path.resolve(process.execPath, '..', '..');

  const paths = [path.resolve(prefixDir, 'lib', 'node')];

  if (homeDir) {
    ArrayPrototypeUnshift(paths, path.resolve(homeDir, '.node_libraries'));
    ArrayPrototypeUnshift(paths, path.resolve(homeDir, '.node_modules'));
  }

  if (nodePath) {
    ArrayPrototypeUnshiftApply(paths, ArrayPrototypeFilter(
      StringPrototypeSplit(nodePath, path.delimiter),
      Boolean,
    ));
  }

  modulePaths = paths;

  // Clone as a shallow copy, for introspection.
  Module.globalPaths = ArrayPrototypeSlice(modulePaths);
};

/**
 * Handle modules loaded via `--require`.
 * @param {string[]} requests The values of `--require`
 */
Module._preloadModules = function(requests) {
  if (!ArrayIsArray(requests)) { return; }

  isPreloading = true;

  // Preloaded modules have a dummy parent module which is deemed to exist
  // in the current working directory. This seeds the search path for
  // preloaded modules.
  const parent = new Module('internal/preload', null);
  try {
    parent.paths = Module._nodeModulePaths(process.cwd());
  } catch (e) {
    if (e.code !== 'ENOENT') {
      isPreloading = false;
      throw e;
    }
  }
  for (let n = 0; n < requests.length; n++) {
    parent.require(requests[n]);
  }
  isPreloading = false;
};

/**
 * If the user has overridden an export from a builtin module, this function can ensure that the override is used in
 * both CommonJS and ES module contexts.
 */
Module.syncBuiltinESMExports = function syncBuiltinESMExports() {
  for (const mod of BuiltinModule.map.values()) {
    if (BuiltinModule.canBeRequiredWithoutScheme(mod.id)) {
      mod.syncExports();
    }
  }
};

ObjectDefineProperty(Module.prototype, 'constructor', {
  __proto__: null,
  get: function() {
    return Module;
  },
  configurable: false,
  enumerable: false,
});

// Backwards compatibility
Module.Module = Module;
