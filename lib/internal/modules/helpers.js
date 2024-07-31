'use strict';

const {
  ArrayPrototypeForEach,
  ArrayPrototypeIncludes,
  ObjectDefineProperty,
  ObjectPrototypeHasOwnProperty,
  SafeMap,
  SafeSet,
  StringPrototypeCharCodeAt,
  StringPrototypeIncludes,
  StringPrototypeSlice,
  StringPrototypeSplit,
  StringPrototypeStartsWith,
} = primordials;
const {
  ERR_INVALID_ARG_TYPE,
} = require('internal/errors').codes;
const { BuiltinModule } = require('internal/bootstrap/realm');

const { validateString } = require('internal/validators');
const fs = require('fs'); // Import all of `fs` so that it can be monkey-patched.
const internalFS = require('internal/fs/utils');
const path = require('path');
const { pathToFileURL, fileURLToPath } = require('internal/url');
const assert = require('internal/assert');

const { getOptionValue } = require('internal/options');
const { setOwnProperty } = require('internal/util');
const { inspect } = require('internal/util/inspect');

const { canParse: URLCanParse } = internalBinding('url');

let debug = require('internal/util/debuglog').debuglog('module', (fn) => {
  debug = fn;
});

/** @typedef {import('internal/modules/cjs/loader.js').Module} Module */

/**
 * Cache for storing resolved real paths of modules.
 * In order to minimize unnecessary lstat() calls, this cache is a list of known-real paths.
 * Set to an empty Map to reset.
 * @type {Map<string, string>}
 */
const realpathCache = new SafeMap();
/**
 * Resolves the path of a given `require` specifier, following symlinks.
 * @param {string} requestPath The `require` specifier
 */
function toRealPath(requestPath) {
  return fs.realpathSync(requestPath, {
    [internalFS.realpathCacheKey]: realpathCache,
  });
}

/** @type {Set<string>} */
let cjsConditions;
/**
 * Define the conditions that apply to the CommonJS loader.
 */
function initializeCjsConditions() {
  const userConditions = getOptionValue('--conditions');
  const noAddons = getOptionValue('--no-addons');
  const addonConditions = noAddons ? [] : ['node-addons'];
  // TODO: Use this set when resolving pkg#exports conditions in loader.js.
  cjsConditions = new SafeSet([
    'require',
    'node',
    ...addonConditions,
    ...userConditions,
  ]);
}

/**
 * Get the conditions that apply to the CommonJS loader.
 */
function getCjsConditions() {
  if (cjsConditions === undefined) {
    initializeCjsConditions();
  }
  return cjsConditions;
}

/**
 * Provide one of Node.js' public modules to user code.
 * @param {string} id - The identifier/specifier of the builtin module to load
 * @param {string} request - The module requiring or importing the builtin module
 */
function loadBuiltinModule(id, request) {
  if (!BuiltinModule.canBeRequiredByUsers(id)) {
    return;
  }
  /** @type {import('internal/bootstrap/realm.js').BuiltinModule} */
  const mod = BuiltinModule.map.get(id);
  debug('load built-in module %s', request);
  // compileForPublicLoader() throws if canBeRequiredByUsers is false:
  mod.compileForPublicLoader();
  return mod;
}

/** @type {Module} */
let $Module = null;
/**
 * Import the Module class on first use.
 */
function lazyModule() {
  $Module = $Module || require('internal/modules/cjs/loader').Module;
  return $Module;
}

/**
 * Create the module-scoped `require` function to pass into CommonJS modules.
 * @param {Module} mod - The module to create the `require` function for.
 * @typedef {(specifier: string) => unknown} RequireFunction
 */
function makeRequireFunction(mod) {
  // lazy due to cycle
  const Module = lazyModule();
  if (mod instanceof Module !== true) {
    throw new ERR_INVALID_ARG_TYPE('mod', 'Module', mod);
  }

  function require(path) {
    return mod.require(path);
  }

  /**
   * The `resolve` method that gets attached to module-scope `require`.
   * @param {string} request
   * @param {Parameters<Module['_resolveFilename']>[3]} options
   */
  function resolve(request, options) {
    validateString(request, 'request');
    return Module._resolveFilename(request, mod, false, options);
  }

  require.resolve = resolve;

  /**
   * The `paths` method that gets attached to module-scope `require`.
   * @param {string} request
   */
  function paths(request) {
    validateString(request, 'request');
    return Module._resolveLookupPaths(request, mod);
  }

  resolve.paths = paths;

  setOwnProperty(require, 'main', process.mainModule);

  // Enable support to add extra extension types.
  require.extensions = Module._extensions;

  require.cache = Module._cache;

  return require;
}

/**
 * Remove byte order marker. This catches EF BB BF (the UTF-8 BOM)
 * because the buffer-to-string conversion in `fs.readFileSync()`
 * translates it to FEFF, the UTF-16 BOM.
 * @param {string} content
 */
function stripBOM(content) {
  if (StringPrototypeCharCodeAt(content) === 0xFEFF) {
    content = StringPrototypeSlice(content, 1);
  }
  return content;
}

/**
 * Add built-in modules to a global or REPL scope object.
 * @param {Record<string, unknown>} object - The object such as `globalThis` to add the built-in modules to.
 * @param {string} dummyModuleName - The label representing the set of built-in modules to add.
 */
function addBuiltinLibsToObject(object, dummyModuleName) {
  // Make built-in modules available directly (loaded lazily).
  const Module = require('internal/modules/cjs/loader').Module;
  const { builtinModules } = Module;

  // To require built-in modules in user-land and ignore modules whose
  // `canBeRequiredByUsers` is false. So we create a dummy module object and not
  // use `require()` directly.
  const dummyModule = new Module(dummyModuleName);

  ArrayPrototypeForEach(builtinModules, (name) => {
    // Neither add underscored modules, nor ones that contain slashes (e.g.,
    // 'fs/promises') or ones that are already defined.
    if (StringPrototypeStartsWith(name, '_') ||
        StringPrototypeIncludes(name, '/') ||
        ObjectPrototypeHasOwnProperty(object, name)) {
      return;
    }
    // Goals of this mechanism are:
    // - Lazy loading of built-in modules
    // - Having all built-in modules available as non-enumerable properties
    // - Allowing the user to re-assign these variables as if there were no
    //   pre-existing globals with the same name.

    const setReal = (val) => {
      // Deleting the property before re-assigning it disables the
      // getter/setter mechanism.
      delete object[name];
      object[name] = val;
    };

    ObjectDefineProperty(object, name, {
      __proto__: null,
      get: () => {
        const lib = dummyModule.require(name);

        try {
          // Override the current getter/setter and set up a new
          // non-enumerable property.
          ObjectDefineProperty(object, name, {
            __proto__: null,
            get: () => lib,
            set: setReal,
            configurable: true,
            enumerable: false,
          });
        } catch {
          // If the property is no longer configurable, ignore the error.
        }

        return lib;
      },
      set: setReal,
      configurable: true,
      enumerable: false,
    });
  });
}

/**
 * Normalize the referrer name as a URL.
 * If it's a string containing an absolute path or a URL it's normalized as
 * a URL string.
 * Otherwise it's returned as undefined.
 * @param {string | null | undefined} referrerName
 * @returns {string | undefined}
 */
function normalizeReferrerURL(referrerName) {
  if (referrerName === null || referrerName === undefined) {
    return undefined;
  }

  if (typeof referrerName === 'string') {
    if (path.isAbsolute(referrerName)) {
      return pathToFileURL(referrerName).href;
    }

    if (StringPrototypeStartsWith(referrerName, 'file://') ||
        URLCanParse(referrerName)) {
      return referrerName;
    }

    return undefined;
  }

  assert.fail('Unreachable code reached by ' + inspect(referrerName));
}


/**
 * @param {string|undefined} url URL to convert to filename
 */
function urlToFilename(url) {
  if (url && StringPrototypeStartsWith(url, 'file://')) {
    return fileURLToPath(url);
  }
  return url;
}

// Whether we have started executing any user-provided CJS code.
// This is set right before we call the wrapped CJS code (not after,
// in case we are half-way in the execution when internals check this).
// Used for internal assertions.
let _hasStartedUserCJSExecution = false;
// Similar to _hasStartedUserCJSExecution but for ESM. This is set
// right before ESM evaluation in the default ESM loader. We do not
// update this during vm SourceTextModule execution because at that point
// some user code must already have been run to execute code via vm
// there is little value checking whether any user JS code is run anyway.
let _hasStartedUserESMExecution = false;

/**
 * Load a public built-in module. ID may or may not be prefixed by `node:` and
 * will be normalized.
 * @param {string} id ID of the built-in to be loaded.
 * @returns {object|undefined} exports of the built-in. Undefined if the built-in
 * does not exist.
 */
function getBuiltinModule(id) {
  validateString(id, 'id');
  const normalizedId = BuiltinModule.normalizeRequirableId(id);
  return normalizedId ? require(normalizedId) : undefined;
}

let parseTS;

function lazyLoadTSParser() {
  parseTS ??= require('internal/deps/amaro/dist/index').transformSync;
  return parseTS;
}

function tsParse(source) {
  if (!source || typeof source !== 'string') { return; }
  const transformSync = lazyLoadTSParser();
  const { code } = transformSync(source);
  return code;
}

function isUnderNodeModules(filename) {
  const resolvedPath = path.resolve(filename);
  const normalizedPath = path.normalize(resolvedPath);
  const splitPath = StringPrototypeSplit(normalizedPath, path.sep);
  return ArrayPrototypeIncludes(splitPath, 'node_modules');
}

module.exports = {
  addBuiltinLibsToObject,
  getBuiltinModule,
  getCjsConditions,
  initializeCjsConditions,
  isUnderNodeModules,
  loadBuiltinModule,
  makeRequireFunction,
  normalizeReferrerURL,
  tsParse,
  stripBOM,
  toRealPath,
  hasStartedUserCJSExecution() {
    return _hasStartedUserCJSExecution;
  },
  setHasStartedUserCJSExecution() {
    _hasStartedUserCJSExecution = true;
  },
  hasStartedUserESMExecution() {
    return _hasStartedUserESMExecution;
  },
  setHasStartedUserESMExecution() {
    _hasStartedUserESMExecution = true;
  },
  urlToFilename,
};
