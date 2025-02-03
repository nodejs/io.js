'use strict';

const {
  ObjectPrototypeHasOwnProperty,
} = primordials;
const {
  validateBoolean,
  validateOneOf,
  validateObject,
  validateString,
} = require('internal/validators');
const { assertTypeScript,
        emitExperimentalWarning,
        getLazy,
        isUnderNodeModules,
        kEmptyObject } = require('internal/util');
const {
  ERR_INVALID_TYPESCRIPT_SYNTAX,
  ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING,
  ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX,
} = require('internal/errors').codes;
const { getOptionValue } = require('internal/options');
const assert = require('internal/assert');
const { Buffer } = require('buffer');
const {
  getCompileCacheEntry,
  saveCompileCacheEntry,
  cachedCodeTypes: { kStrippedTypeScript, kTransformedTypeScript, kTransformedTypeScriptWithSourceMaps },
} = internalBinding('modules');

/**
 * The TypeScript parsing mode, either 'strip-only' or 'transform'.
 * @type {string}
 */
const getTypeScriptParsingMode = getLazy(() =>
  (getOptionValue('--experimental-transform-types') ? 'transform' : 'strip-only'),
);

/**
 * Load the TypeScript parser.
 * and returns an object with a `code` property.
 * @returns {Function} The TypeScript parser function.
 */
const loadTypeScriptParser = getLazy(() => {
  assertTypeScript();
  const amaro = require('internal/deps/amaro/dist/index');
  return amaro.transformSync;
});

/**
 *
 * @param {string} source the source code
 * @param {object} options the options to pass to the parser
 * @returns {TransformOutput} an object with a `code` property.
 */
function parseTypeScript(source, options) {
  const parse = loadTypeScriptParser();
  try {
    return parse(source, options);
  } catch (error) {
    /**
     * Amaro v0.3.0 (from SWC v1.10.7) throws an object with `message` and `code` properties.
     * It allows us to distinguish between invalid syntax and unsupported syntax.
     */
    switch (error?.code) {
      case 'UnsupportedSyntax':
        throw new ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX(error.message);
      case 'InvalidSyntax':
        throw new ERR_INVALID_TYPESCRIPT_SYNTAX(error.message);
      default:
        // SWC may throw strings when something goes wrong.
        if (typeof error === 'string') { assert.fail(error); }
        assert(error != null && ObjectPrototypeHasOwnProperty(error, 'message'));
        assert.fail(error.message);
    }
  }
}

/**
 * Performs type-stripping to TypeScript source code.
 * @param {string} code TypeScript code to parse.
 * @param {TransformOptions} options The configuration for type stripping.
 * @returns {string} The stripped TypeScript code.
 */
function stripTypeScriptTypes(code, options = kEmptyObject) {
  emitExperimentalWarning('stripTypeScriptTypes');
  validateString(code, 'code');
  validateObject(options, 'options');

  const {
    sourceMap = false,
    sourceUrl = '',
  } = options;
  let { mode = 'strip' } = options;
  validateOneOf(mode, 'options.mode', ['strip', 'transform']);
  validateBoolean(sourceMap, 'options.sourceMap');
  validateString(sourceUrl, 'options.sourceUrl');
  if (mode === 'strip') {
    validateOneOf(sourceMap, 'options.sourceMap', [false, undefined]);
    // Rename mode from 'strip' to 'strip-only'.
    // The reason is to match `process.features.typescript` which returns `strip`,
    // but the parser expects `strip-only`.
    mode = 'strip-only';
  }

  return processTypeScriptCode(code, {
    mode,
    sourceMap,
    filename: sourceUrl,
  });
}

/**
 * @typedef {'strip-only' | 'transform'} TypeScriptMode
 * @typedef {object} TypeScriptOptions
 * @property {TypeScriptMode} mode Mode.
 * @property {boolean} sourceMap Whether to generate source maps.
 * @property {string|undefined} filename Filename.
 */

/**
 * Processes TypeScript code by stripping types or transforming.
 * Handles source maps if needed.
 * @param {string} code TypeScript code to process.
 * @param {TypeScriptOptions} options The configuration object.
 * @returns {string} The processed code.
 */
function processTypeScriptCode(code, options) {
  const { code: transformedCode, map } = parseTypeScript(code, options);

  if (map) {
    return addSourceMap(transformedCode, map);
  }

  if (options.filename) {
    return `${transformedCode}\n\n//# sourceURL=${options.filename}`;
  }

  return transformedCode;
}

/**
 * Get the type enum used for compile cache.
 * @param {TypeScriptMode} mode Mode of transpilation.
 * @param {boolean} sourceMap Whether source maps are enabled.
 * @returns {number}
 */
function getCachedCodeType(mode, sourceMap) {
  if (mode === 'transform') {
    if (sourceMap) { return kTransformedTypeScriptWithSourceMaps; }
    return kTransformedTypeScript;
  }
  return kStrippedTypeScript;
}

/**
 * Performs type-stripping to TypeScript source code internally.
 * It is used by internal loaders.
 * @param {string} source TypeScript code to parse.
 * @param {string} filename The filename of the source code.
 * @param {boolean} emitWarning Whether to emit a warning.
 * @returns {TransformOutput} The stripped TypeScript code.
 */
function stripTypeScriptModuleTypes(source, filename, emitWarning = true) {
  if (emitWarning) {
    emitExperimentalWarning('Type Stripping');
  }
  assert(typeof source === 'string');
  if (isUnderNodeModules(filename)) {
    throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(filename);
  }
  const sourceMap = getOptionValue('--enable-source-maps');

  const mode = getTypeScriptParsingMode();

  // Instead of caching the compile cache status, just go into C++ to fetch it,
  // as checking process.env equally involves calling into C++ anyway, and
  // the compile cache can be enabled dynamically.
  const type = getCachedCodeType(mode, sourceMap);
  // Get a compile cache entry into the native compile cache store,
  // keyed by the filename. If the cache can already be loaded on disk,
  // cached.transpiled contains the cached string. Otherwise we should do
  // the transpilation and save it in the native store later using
  // saveCompileCacheEntry().
  const cached = (filename ? getCompileCacheEntry(source, filename, type) : undefined);
  if (cached?.transpiled) {  // TODO(joyeecheung): return Buffer here.
    return cached.transpiled;
  }

  const options = {
    mode,
    sourceMap,
    filename,
  };

  const transpiled = processTypeScriptCode(source, options);
  if (cached) {
    // cached.external contains a pointer to the native cache entry.
    // The cached object would be unreachable once it's out of scope,
    // but the pointer inside cached.external would stay around for reuse until
    // environment shutdown or when the cache is manually flushed
    // to disk. Unwrap it in JS before passing into C++ since it's faster.
    saveCompileCacheEntry(cached.external, transpiled);
  }
  return transpiled;
}

/**
 *
 * @param {string} code The compiled code.
 * @param {string} sourceMap The source map.
 * @returns {string} The code with the source map attached.
 */
function addSourceMap(code, sourceMap) {
  // The base64 encoding should be https://datatracker.ietf.org/doc/html/rfc4648#section-4,
  // not base64url https://datatracker.ietf.org/doc/html/rfc4648#section-5. See data url
  // spec https://tools.ietf.org/html/rfc2397#section-2.
  const base64SourceMap = Buffer.from(sourceMap).toString('base64');
  return `${code}\n\n//# sourceMappingURL=data:application/json;base64,${base64SourceMap}`;
}

module.exports = {
  stripTypeScriptModuleTypes,
  stripTypeScriptTypes,
};
