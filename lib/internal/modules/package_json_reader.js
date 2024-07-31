'use strict';

const {
  ArrayIsArray,
  JSONParse,
  ObjectDefineProperty,
  StringPrototypeLastIndexOf,
  StringPrototypeSlice,
} = primordials;
const modulesBinding = internalBinding('modules');
const { resolve, sep } = require('path');
const { kEmptyObject } = require('internal/util');

/**
 * @param {string} path
 * @param {import('typings/internalBinding/modules').SerializedPackageConfig} contents
 * @returns {import('typings/internalBinding/modules').PackageConfig}
 */
function deserializePackageJSON(path, contents) {
  if (contents === undefined) {
    return {
      __proto__: null,
      exists: false,
      pjsonPath: path,
      type: 'none', // Ignore unknown types for forwards compatibility
    };
  }

  let pjsonPath = path;
  const {
    0: name,
    1: main,
    2: type,
    3: plainImports,
    4: plainExports,
    5: optionalFilePath,
  } = contents;

  // This is required to be used in getPackageScopeConfig.
  if (optionalFilePath) {
    pjsonPath = optionalFilePath;
  }

  // The imports and exports fields can be either undefined or a string.
  // - If it's a string, it's either plain string or a stringified JSON string.
  // - If it's a stringified JSON string, it starts with either '[' or '{'.
  const requiresJSONParse = (value) => (value !== undefined && (value[0] === '[' || value[0] === '{'));

  return {
    __proto__: null,
    exists: true,
    pjsonPath,
    name,
    main,
    type,
    // This getters are used to lazily parse the imports and exports fields.
    get imports() {
      const value = requiresJSONParse(plainImports) ? JSONParse(plainImports) : plainImports;
      ObjectDefineProperty(this, 'imports', { __proto__: null, value });
      return this.imports;
    },
    get exports() {
      const value = requiresJSONParse(plainExports) ? JSONParse(plainExports) : plainExports;
      ObjectDefineProperty(this, 'exports', { __proto__: null, value });
      return this.exports;
    },
  };
}

/**
 * Reads a package.json file and returns the parsed contents.
 * @param {string} jsonPath
 * @param {{
 *   base?: URL | string,
 *   specifier?: URL | string,
 *   isESM?: boolean,
 * }} options
 * @returns {import('typings/internalBinding/modules').PackageConfig}
 */
function read(jsonPath, { base, specifier, isESM } = kEmptyObject) {
  // This function will be called by both CJS and ESM, so we need to make sure
  // non-null attributes are converted to strings.
  const parsed = modulesBinding.readPackageJSON(
    jsonPath,
    isESM,
    base == null ? undefined : `${base}`,
    specifier == null ? undefined : `${specifier}`,
  );

  return deserializePackageJSON(jsonPath, parsed);
}

/**
 * @deprecated Expected to be removed in favor of `read` in the future.
 * Behaves the same was as `read`, but appends package.json to the path.
 * @param {string} requestPath
 * @return {PackageConfig}
 */
function readPackage(requestPath) {
  // TODO(@anonrig): Remove this function.
  return read(resolve(requestPath, 'package.json'));
}

/**
 * Get the nearest parent package.json file from a given path.
 * Return the package.json data and the path to the package.json file, or undefined.
 * @param {string} checkPath The path to start searching from.
 * @returns {undefined | {data: import('typings/internalBinding/modules').PackageConfig, path: string}}
 */
function getNearestParentPackageJSON(checkPath) {
  const result = modulesBinding.getNearestParentPackageJSON(checkPath);

  if (result === undefined) {
    return undefined;
  }

  const data = deserializePackageJSON(checkPath, result);

  // Path should be the root folder of the matched package.json
  // For example for ~/path/package.json, it should be ~/path
  const path = StringPrototypeSlice(data.pjsonPath, 0, StringPrototypeLastIndexOf(data.pjsonPath, sep));

  return { data, path };
}

/**
 * Returns the package configuration for the given resolved URL.
 * @param {URL | string} resolved - The resolved URL.
 * @returns {import('typings/internalBinding/modules').PackageConfig} - The package configuration.
 */
function getPackageScopeConfig(resolved) {
  const result = modulesBinding.getPackageScopeConfig(`${resolved}`);

  if (ArrayIsArray(result)) {
    return deserializePackageJSON(`${resolved}`, result);
  }

  // This means that the response is a string
  // and it is the path to the package.json file
  return {
    __proto__: null,
    pjsonPath: result,
    exists: false,
    type: 'none',
  };
}

/**
 * Returns the package type for a given URL.
 * @param {URL} url - The URL to get the package type for.
 */
function getPackageType(url) {
  // TODO(@anonrig): Write a C++ function that returns only "type".
  return getPackageScopeConfig(url).type;
}

module.exports = {
  read,
  readPackage,
  getNearestParentPackageJSON,
  getPackageScopeConfig,
  getPackageType,
};
