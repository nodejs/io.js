'use strict';

const {
  ArrayPrototypeFindIndex,
  ArrayPrototypePush,
  ArrayPrototypeSplice,
  ObjectFreeze,
  StringPrototypeStartsWith,
  Symbol,
} = primordials;
const {
  isAnyArrayBuffer,
  isArrayBufferView,
} = require('internal/util/types');

const { BuiltinModule } = require('internal/bootstrap/realm');
const {
  ERR_INVALID_RETURN_PROPERTY_VALUE,
} = require('internal/errors').codes;
const { validateFunction } = require('internal/validators');
const { isAbsolute } = require('path');
const { pathToFileURL, fileURLToPath } = require('internal/url');

let debug = require('internal/util/debuglog').debuglog('module_hooks', (fn) => {
  debug = fn;
});

/**
 * @typedef {import('internal/modules/cjs/loader.js').Module} Module
 */
/**
 * @typedef {(
 *   specifier: string,
 *   context: Partial<ModuleResolveContext>,
 * ) => ModuleResolveResult
 * } NextResolve
 * @typedef {(
 *   specifier: string,
 *   context: ModuleResolveContext,
 *   nextResolve: NextResolve,
 * ) => ModuleResolveResult
 * } ResolveHook
 * @typedef {(
 *   url: string,
 *   context: Partial<ModuleLoadContext>,
 * ) => ModuleLoadResult
 * } NextLoad
 * @typedef {(
 *   url: string,
 *   context: ModuleLoadContext,
 *   nextLoad: NextLoad,
 * ) => ModuleLoadResult
 * } LoadHook
 */

// Use arrays for better insertion and iteration performance, we don't care
// about deletion performance as much.

/** @type {ResolveHook[]} */
const resolveHooks = [];
/** @type {LoadHook[]} */
const loadHooks = [];
const hookId = Symbol('kModuleHooksIdKey');
let nextHookId = 0;

class ModuleHooks {
  /**
   * @param {ResolveHook|undefined} resolve User-provided hook.
   * @param {LoadHook|undefined} load User-provided hook.
   */
  constructor(resolve, load) {
    this[hookId] = Symbol(`module-hook-${nextHookId++}`);
    // Always initialize all hooks, if it's unspecified it'll be an owned undefined.
    this.resolve = resolve;
    this.load = load;

    if (resolve) {
      ArrayPrototypePush(resolveHooks, this);
    }
    if (load) {
      ArrayPrototypePush(loadHooks, this);
    }

    ObjectFreeze(this);
  }
  // TODO(joyeecheung): we may want methods that allow disabling/enabling temporarily
  // which just sets the item in the array to undefined temporarily.
  // TODO(joyeecheung): this can be the [Symbol.dispose] implementation to pair with
  // `using` when the explicit resource management proposal is shipped by V8.
  /**
   * Deregister the hook instance.
   */
  deregister() {
    const id = this[hookId];
    let index = ArrayPrototypeFindIndex(resolveHooks, (hook) => hook[hookId] === id);
    if (index !== -1) {
      ArrayPrototypeSplice(resolveHooks, index, 1);
    }
    index = ArrayPrototypeFindIndex(loadHooks, (hook) => hook[hookId] === id);
    if (index !== -1) {
      ArrayPrototypeSplice(loadHooks, index, 1);
    }
  }
};

/**
 * TODO(joyeecheung): taken an optional description?
 * @param {{ resolve?: ResolveHook, load?: LoadHook }} hooks User-provided hooks
 * @returns {ModuleHooks}
 */
function registerHooks(hooks) {
  const { resolve, load } = hooks;
  if (resolve) {
    validateFunction(resolve, 'hooks.resolve');
  }
  if (load) {
    validateFunction(load, 'hooks.load');
  }
  return new ModuleHooks(resolve, load);
}

/**
 * @param {string} filename
 * @returns {string}
 */
function convertCJSFilenameToURL(filename) {
  if (!filename) { return filename; }
  const builtinId = BuiltinModule.normalizeRequirableId(filename);
  if (builtinId) {
    return `node:${builtinId}`;
  }
  // Handle the case where filename is neither a path, nor a built-in id,
  // which is possible via monkey-patching.
  if (isAbsolute(filename)) {
    return pathToFileURL(filename).href;
  }
  return filename;
}

/**
 * @param {string} url
 * @returns {string}
 */
function convertURLToCJSFilename(url) {
  if (!url) { return url; }
  const builtinId = BuiltinModule.normalizeRequirableId(url);
  if (builtinId) {
    return builtinId;
  }
  if (StringPrototypeStartsWith(url, 'file://')) {
    return fileURLToPath(url);
  }
  return url;
}

/**
 * Convert a list of hooks into a function that can be used to do an operation through
 * a chain of hooks. If any of the hook returns without calling the next hook, it
 * must return shortCircuit: true to stop the chain from continuing to avoid
 * forgetting to invoke the next hook by mistake.
 * @param {ModuleHooks[]} hooks A list of hooks whose last argument is `nextHook`.
 * @param {'load'|'resolve'} name Name of the hook in ModuleHooks.
 * @param {Function} defaultStep The default step in the chain.
 * @param {Function} validate A function that validates and sanitize the result returned by the chain.
 * @returns {Function}
 */
function buildHooks(hooks, name, defaultStep, validate) {
  let lastRunIndex = hooks.length;
  function wrapHook(index, userHook, next) {
    return function wrappedHook(...args) {
      lastRunIndex = index;
      const hookResult = userHook(...args, next);
      if (lastRunIndex > 0 && lastRunIndex === index && !hookResult.shortCircuit) {
        throw new ERR_INVALID_RETURN_PROPERTY_VALUE('true', name, 'shortCircuit',
                                                    hookResult.shortCircuit);
      }
      return validate(...args, hookResult);
    };
  }
  const chain = [wrapHook(0, defaultStep)];
  for (let i = 0; i < hooks.length; ++i) {
    const wrappedHook = wrapHook(i + 1, hooks[i][name], chain[i]);
    ArrayPrototypePush(chain, wrappedHook);
  }
  return chain[chain.length - 1];
}

/**
 * @typedef {object} ModuleResolveResult
 * @property {string} url Resolved URL of the module.
 * @property {string|undefined} format Format of the module.
 * @property {ImportAttributes|undefined} importAttributes Import attributes for the request.
 * @property {boolean|undefined} shortCircuit Whether the next hook has been skipped.
 */

/**
 * Validate the result returned by a chain of resolve hook.
 * @param {string} specifier Specifier passed into the hooks.
 * @param {ModuleResolveContext} context Context passed into the hooks.
 * @param {ModuleResolveResult} result Result produced by resolve hooks.
 * @returns {ModuleResolveResult}
 */
function validateResolve(specifier, context, result) {
  const { url, format, importAttributes } = result;
  if (typeof url !== 'string') {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'a URL string',
      'resolve',
      'url',
      url,
    );
  }

  if (format && typeof format !== 'string') {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'a string',
      'resolve',
      'format',
      format,
    );
  }

  if (importAttributes && typeof importAttributes !== 'object') {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'an object',
      'resolve',
      'importAttributes',
      importAttributes,
    );
  }

  return {
    __proto__: null,
    url,
    format,
    importAttributes,
  };
}

/**
 * @typedef {object} ModuleLoadResult
 * @property {string|undefined} format Format of the loaded module.
 * @property {string|ArrayBuffer|TypedArray} source Source code of the module.
 * @property {boolean|undefined} shortCircuit Whether the next hook has been skipped.
 */

/**
 * Validate the result returned by a chain of resolve hook.
 * @param {string} url URL passed into the hooks.
 * @param {ModuleLoadContext} context Context passed into the hooks.
 * @param {ModuleLoadResult} result Result produced by load hooks.
 * @returns {ModuleLoadResult}
 */
function validateLoad(url, context, result) {
  const { source, format } = result;
  // To align with module.register(), the load hooks are still invoked for
  // the builtins even though the default load step only provides null as source,
  // and any source content for builtins provided by the user hooks are ignored.
  if (!StringPrototypeStartsWith(url, 'node:') &&
    typeof result.source !== 'string' &&
      !isAnyArrayBuffer(source) &&
      !isArrayBufferView(source)) {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'a string, an ArrayBuffer, or a TypedArray',
      'load',
      'source',
      source,
    );
  }

  if (typeof format !== 'string' && format !== undefined) {
    throw new ERR_INVALID_RETURN_PROPERTY_VALUE(
      'a string',
      'load',
      'format',
      format,
    );
  }

  return {
    __proto__: null,
    format,
    source,
  };
}

class ModuleResolveContext {
  /**
   * Context for the resolve hook.
   * @param {string|undefined} parentURL Parent URL.
   * @param {ImportAttributes|undefined} importAttributes Import attributes.
   * @param {string[]} conditions Conditions.
   */
  constructor(parentURL, importAttributes, conditions) {
    this.parentURL = parentURL;
    this.importAttributes = importAttributes;
    this.conditions = conditions;
    // TODO(joyeecheung): a field to differentiate between require and import?
  }
};

class ModuleLoadContext {
  /**
   * Context for the load hook.
   * @param {string|undefined} format URL.
   * @param {ImportAttributes|undefined} importAttributes Import attributes.
   * @param {string[]} conditions Conditions.
   */
  constructor(format, importAttributes, conditions) {
    this.format = format;
    this.importAttributes = importAttributes;
    this.conditions = conditions;
  }
};

let decoder;
/**
 * Load module source for a url, through a hooks chain if it exists.
 * @param {string} url
 * @param {string|undefined} originalFormat
 * @param {ImportAttributes|undefined} importAttributes
 * @param {string[]} conditions
 * @param {(url: string, context: ModuleLoadContext) => ModuleLoadResult} defaultLoad
 * @returns {ModuleLoadResult}
 */
function loadWithHooks(url, originalFormat, importAttributes, conditions, defaultLoad) {
  debug('loadWithHooks', url, originalFormat);
  const context = new ModuleLoadContext(originalFormat, importAttributes, conditions);
  if (loadHooks.length === 0) {
    return defaultLoad(url, context);
  }

  const runner = buildHooks(loadHooks, 'load', defaultLoad, validateLoad);

  const result = runner(url, context);
  const { source, format } = result;
  if (!isAnyArrayBuffer(source) && !isArrayBufferView(source)) {
    return result;
  }

  switch (format) {
    // Text formats:
    case undefined:
    case 'module':
    case 'commonjs':
    case 'json':
    case 'module-typescript':
    case 'commonjs-typescript':
    case 'typescript': {
      decoder ??= new (require('internal/encoding').TextDecoder)();
      result.source = decoder.decode(source);
      break;
    }
    default:
      break;
  }
  return result;
}

/**
 * Resolve module request to a url, through a hooks chain if it exists.
 * @param {string} specifier
 * @param {string|undefined} parentURL
 * @param {ImportAttributes|undefined} importAttributes
 * @param {string[]} conditions
 * @param {(specifier: string, context: ModuleResolveContext) => ModuleResolveResult} defaultResolve
 * @returns {ModuleResolveResult}
 */
function resolveWithHooks(specifier, parentURL, importAttributes, conditions, defaultResolve) {
  debug('resolveWithHooks', specifier, parentURL, importAttributes);
  const context = new ModuleResolveContext(parentURL, importAttributes, conditions);
  if (resolveHooks.length === 0) {
    return defaultResolve(specifier, context);
  }

  const runner = buildHooks(resolveHooks, 'resolve', defaultResolve, validateResolve);

  return runner(specifier, context);
}

module.exports = {
  convertCJSFilenameToURL,
  convertURLToCJSFilename,
  loadHooks,
  loadWithHooks,
  registerHooks,
  resolveHooks,
  resolveWithHooks,
};
