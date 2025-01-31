'use strict';

const {
  ArrayIsArray,
  JSONParse,
  ObjectDefineProperty,
  RegExpPrototypeExec,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
} = primordials;
const {
  fileURLToPath,
  isURL,
  pathToFileURL,
  URL,
} = require('internal/url');
const { canParse: URLCanParse } = internalBinding('url');
const {
  codes: {
    ERR_INVALID_MODULE_SPECIFIER,
    ERR_MISSING_ARGS,
    ERR_MODULE_NOT_FOUND,
  },
} = require('internal/errors');
const { kEmptyObject } = require('internal/util');
const modulesBinding = internalBinding('modules');
const path = require('path');
const { validateString } = require('internal/validators');
const internalFsBinding = internalBinding('fs');

/**
 * @typedef {import('typings/internalBinding/modules').DeserializedPackageConfig} DeserializedPackageConfig
 * @typedef {import('typings/internalBinding/modules').PackageConfig} PackageConfig
 * @typedef {import('typings/internalBinding/modules').SerializedPackageConfig} SerializedPackageConfig
 */

/**
 * @param {URL['pathname']} path
 * @param {SerializedPackageConfig} contents
 * @returns {DeserializedPackageConfig}
 */
function deserializePackageJSON(path, contents) {
  if (contents === undefined) {
    return {
      data: {
        __proto__: null,
        type: 'none', // Ignore unknown types for forwards compatibility
      },
      exists: false,
      path,
    };
  }

  const {
    0: name,
    1: main,
    2: type,
    3: plainImports,
    4: plainExports,
    5: optionalFilePath,
  } = contents;

  const pjsonPath = optionalFilePath ?? path;

  return {
    data: {
      __proto__: null,
      ...(name != null && { name }),
      ...(main != null && { main }),
      ...(type != null && { type }),
      ...(plainImports != null && {
        // This getters are used to lazily parse the imports and exports fields.
        get imports() {
          const value = requiresJSONParse(plainImports) ? JSONParse(plainImports) : plainImports;
          ObjectDefineProperty(this, 'imports', { __proto__: null, value });
          return this.imports;
        },
      }),
      ...(plainExports != null && {
        get exports() {
          const value = requiresJSONParse(plainExports) ? JSONParse(plainExports) : plainExports;
          ObjectDefineProperty(this, 'exports', { __proto__: null, value });
          return this.exports;
        },
      }),
    },
    exists: true,
    path: pjsonPath,
  };
}

// The imports and exports fields can be either undefined or a string.
// - If it's a string, it's either plain string or a stringified JSON string.
// - If it's a stringified JSON string, it starts with either '[' or '{'.
const requiresJSONParse = (value) => (value !== undefined && (value[0] === '[' || value[0] === '{'));

/**
 * Reads a package.json file and returns the parsed contents.
 * @param {string} jsonPath
 * @param {{
 *   base?: URL | string,
 *   specifier?: URL | string,
 *   isESM?: boolean,
 * }} options
 * @returns {PackageConfig}
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

  const result = deserializePackageJSON(jsonPath, parsed);

  return {
    __proto__: null,
    ...result.data,
    exists: result.exists,
    pjsonPath: result.path,
  };
}

/**
 * @deprecated Expected to be removed in favor of `read` in the future.
 * Behaves the same was as `read`, but appends package.json to the path.
 * @param {string} requestPath
 * @return {PackageConfig}
 */
function readPackage(requestPath) {
  // TODO(@anonrig): Remove this function.
  return read(path.resolve(requestPath, 'package.json'));
}

/**
 * Get the nearest parent package.json file from a given path.
 * Return the package.json data and the path to the package.json file, or undefined.
 * @param {string} checkPath The path to start searching from.
 * @returns {undefined | DeserializedPackageConfig}
 */
function getNearestParentPackageJSON(checkPath) {
  const result = modulesBinding.getNearestParentPackageJSON(checkPath);

  if (result === undefined) {
    return undefined;
  }

  return deserializePackageJSON(checkPath, result);
}

/**
 * Returns the package configuration for the given resolved URL.
 * @param {URL | string} resolved - The resolved URL.
 * @returns {import('typings/internalBinding/modules').PackageConfig} - The package configuration.
 */
function getPackageScopeConfig(resolved) {
  const result = modulesBinding.getPackageScopeConfig(`${resolved}`);

  if (ArrayIsArray(result)) {
    const { data, exists, path } = deserializePackageJSON(`${resolved}`, result);

    return {
      __proto__: null,
      ...data,
      exists,
      pjsonPath: path,
    };
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

const invalidPackageNameRegEx = /^\.|%|\\/;
/**
 * Parse a package name from a specifier.
 * @param {string} specifier - The import specifier.
 * @param {string | URL | undefined} base - The parent URL.
 */
function parsePackageName(specifier, base) {
  let separatorIndex = StringPrototypeIndexOf(specifier, '/');
  let validPackageName = true;
  let isScoped = false;
  if (specifier[0] === '@') {
    isScoped = true;
    if (separatorIndex === -1 || specifier.length === 0) {
      validPackageName = false;
    } else {
      separatorIndex = StringPrototypeIndexOf(
        specifier, '/', separatorIndex + 1);
    }
  }

  const packageName = separatorIndex === -1 ?
    specifier : StringPrototypeSlice(specifier, 0, separatorIndex);

  // Package name cannot have leading . and cannot have percent-encoding or
  // \\ separators.
  if (RegExpPrototypeExec(invalidPackageNameRegEx, packageName) !== null) {
    validPackageName = false;
  }

  if (!validPackageName) {
    throw new ERR_INVALID_MODULE_SPECIFIER(
      specifier, 'is not a valid package name', fileURLToPath(base));
  }

  const packageSubpath = '.' + (separatorIndex === -1 ? '' :
    StringPrototypeSlice(specifier, separatorIndex));

  return { packageName, packageSubpath, isScoped };
}

function getPackageJSONURL(specifier, base) {
  const { packageName, packageSubpath, isScoped } = parsePackageName(specifier, base);

  // ResolveSelf
  const packageConfig = getPackageScopeConfig(base);
  if (packageConfig.exists) {
    if (packageConfig.exports != null && packageConfig.name === packageName) {
      const packageJSONPath = packageConfig.pjsonPath;
      return { packageJSONUrl: pathToFileURL(packageJSONPath), packageJSONPath, packageSubpath };
    }
  }

  let packageJSONUrl = new URL(`./node_modules/${packageName}/package.json`, base);
  let packageJSONPath = fileURLToPath(packageJSONUrl);
  let lastPath;
  do {
    const stat = internalFsBinding.internalModuleStat(
      internalFsBinding,
      StringPrototypeSlice(packageJSONPath, 0, packageJSONPath.length - 13),
    );
    // Check for !stat.isDirectory()
    if (stat !== 1) {
      lastPath = packageJSONPath;
      packageJSONUrl = new URL(
        `${isScoped ? '../' : ''}../../../node_modules/${packageName}/package.json`,
        packageJSONUrl,
      );
      packageJSONPath = fileURLToPath(packageJSONUrl);
      continue;
    }

    // Package match.
    return { packageJSONUrl, packageJSONPath, packageSubpath };
  } while (packageJSONPath.length !== lastPath.length);

  throw new ERR_MODULE_NOT_FOUND(packageName, fileURLToPath(base), null);
}

/** @type {import('./esm/resolve.js').defaultResolve} */
let defaultResolve;
/**
 * @param {URL['href'] | string | URL} specifier The location for which to get the "root" package.json
 * @param {URL['href'] | string | URL} [base] The location of the current module (ex file://tmp/foo.js).
 */
function findPackageJSON(specifier, base = 'data:') {
  if (arguments.length === 0) {
    throw new ERR_MISSING_ARGS('specifier');
  }
  try {
    specifier = `${specifier}`;
  } catch {
    validateString(specifier, 'specifier');
  }

  let parentURL = base;
  if (!isURL(base)) {
    validateString(base, 'base');
    parentURL = path.isAbsolute(base) ? pathToFileURL(base) : new URL(base);
  }

  if (specifier && specifier[0] !== '.' && specifier[0] !== '/' && !URLCanParse(specifier)) {
    // If `specifier` is a bare specifier.
    const { packageJSONPath } = getPackageJSONURL(specifier, parentURL);
    return packageJSONPath;
  }

  let resolvedTarget;
  defaultResolve ??= require('internal/modules/esm/resolve').defaultResolve;

  try {
    // TODO(@JakobJingleheimer): Detect whether findPackageJSON is being used within a loader
    // (possibly piggyback on `allowImportMetaResolve`)
    // - When inside, use the default resolve
    //   - (I think it's impossible to use the chain because of re-entry & a deadlock from atomics).
    // - When outside, use cascadedLoader.resolveSync (not implemented yet, but the pieces exist).
    resolvedTarget = defaultResolve(specifier, { parentURL: `${parentURL}` }).url;
  } catch (err) {
    if (err.code === 'ERR_UNSUPPORTED_DIR_IMPORT') {
      resolvedTarget = err.url;
    } else {
      throw err;
    }
  }

  const pkg = getNearestParentPackageJSON(fileURLToPath(resolvedTarget));

  return pkg?.path;
}

module.exports = {
  read,
  readPackage,
  getNearestParentPackageJSON,
  getPackageScopeConfig,
  getPackageType,
  getPackageJSONURL,
  findPackageJSON,
};
