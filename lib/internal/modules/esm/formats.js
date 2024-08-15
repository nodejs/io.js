'use strict';

const {
  RegExpPrototypeExec,
} = primordials;

const { getOptionValue } = require('internal/options');
const { getValidatedPath } = require('internal/fs/utils');
const fsBindings = internalBinding('fs');
const { fs: fsConstants } = internalBinding('constants');

const experimentalWasmModules = getOptionValue('--experimental-wasm-modules');

const extensionFormatMap = {
  '__proto__': null,
  '.cjs': 'commonjs',
  '.js': 'module',
  '.json': 'json',
  '.mjs': 'module',
};

if (experimentalWasmModules) {
  extensionFormatMap['.wasm'] = 'wasm';
}

if (getOptionValue('--experimental-strip-types')) {
  extensionFormatMap['.ts'] = 'module-typescript';
  extensionFormatMap['.mts'] = 'module-typescript';
  extensionFormatMap['.cts'] = 'commonjs-typescript';
}

/**
 * @param {string} mime
 * @returns {string | null}
 */
function mimeToFormat(mime) {
  if (
    RegExpPrototypeExec(
      /^\s*(text|application)\/javascript\s*(;\s*charset=utf-?8\s*)?$/i,
      mime,
    ) !== null
  ) { return 'module'; }
  if (mime === 'application/json') { return 'json'; }
  if (experimentalWasmModules && mime === 'application/wasm') { return 'wasm'; }
  return null;
}

/**
 * For extensionless files in a `module` package scope, or a default `module` scope enabled by the
 * `--experimental-default-type` flag, we check the file contents to disambiguate between ES module JavaScript and Wasm.
 * We do this by taking advantage of the fact that all Wasm files start with the header `0x00 0x61 0x73 0x6d` (`_asm`).
 * @param {URL} url
 */
function getFormatOfExtensionlessFile(url) {
  if (!experimentalWasmModules) { return 'module'; }
  const path = getValidatedPath(url);
  switch (fsBindings.getFormatOfExtensionlessFile(path)) {
    case fsConstants.EXTENSIONLESS_FORMAT_WASM:
      return 'wasm';
    default:
      return 'module';
  }
}

module.exports = {
  extensionFormatMap,
  getFormatOfExtensionlessFile,
  mimeToFormat,
};
