'use strict';

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
} = require('internal/errors').codes;
const { getOptionValue } = require('internal/options');
const assert = require('internal/assert');

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
    throw new ERR_INVALID_TYPESCRIPT_SYNTAX(error);
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
 * Processes TypeScript code by stripping types or transforming.
 * Handles source maps if needed.
 * @param {string} code TypeScript code to process.
 * @param {object} options The configuration object.
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
 * Performs type-stripping to TypeScript source code internally.
 * It is used by internal loaders.
 * @param {string} source TypeScript code to parse.
 * @param {string} filename The filename of the source code.
 * @returns {TransformOutput} The stripped TypeScript code.
 */
function stripTypeScriptModuleTypes(source, filename) {
  assert(typeof source === 'string');
  if (isUnderNodeModules(filename)) {
    throw new ERR_UNSUPPORTED_NODE_MODULES_TYPE_STRIPPING(filename);
  }
  const options = {
    mode: getTypeScriptParsingMode(),
    sourceMap: getOptionValue('--enable-source-maps'),
    filename,
  };
  return processTypeScriptCode(source, options);
}

/**
 *
 * @param {string} code The compiled code.
 * @param {string} sourceMap The source map.
 * @returns {string} The code with the source map attached.
 */
function addSourceMap(code, sourceMap) {
  // TODO(@marco-ippolito) When Buffer.transcode supports utf8 to
  // base64 transformation, we should change this line.
  const base64SourceMap = internalBinding('buffer').btoa(sourceMap);
  return `${code}\n\n//# sourceMappingURL=data:application/json;base64,${base64SourceMap}`;
}

module.exports = {
  stripTypeScriptModuleTypes,
  stripTypeScriptTypes,
};
