'use strict';

const {
  StringPrototypeEndsWith,
  globalThis,
} = primordials;

const { getNearestParentPackageJSONType } = internalBinding('modules');
const { getOptionValue } = require('internal/options');
const path = require('path');
const { pathToFileURL } = require('internal/url');
const { kEmptyObject, getCWDURL } = require('internal/util');
const {
  hasUncaughtExceptionCaptureCallback,
} = require('internal/process/execution');
const {
  triggerUncaughtException,
} = internalBinding('errors');
const {
  privateSymbols: {
    entry_point_promise_private_symbol,
  },
} = internalBinding('util');
/**
 * Get the absolute path to the main entry point.
 * @param {string} main - Entry point path
 */
function resolveMainPath(main) {
  const defaultType = getOptionValue('--experimental-default-type');
  /** @type {string} */
  let mainPath;
  if (defaultType === 'module') {
    if (getOptionValue('--preserve-symlinks-main')) { return; }
    mainPath = path.resolve(main);
  } else {
    // Extension searching for the main entry point is supported only in legacy mode.
    // Module._findPath is monkey-patchable here.
    const { Module } = require('internal/modules/cjs/loader');
    mainPath = Module._findPath(path.resolve(main), null, true);
  }
  if (!mainPath) { return; }

  const preserveSymlinksMain = getOptionValue('--preserve-symlinks-main');
  if (!preserveSymlinksMain) {
    const { toRealPath } = require('internal/modules/helpers');
    try {
      mainPath = toRealPath(mainPath);
    } catch (err) {
      if (defaultType === 'module' && err?.code === 'ENOENT') {
        const { decorateErrorWithCommonJSHints } = require('internal/modules/esm/resolve');
        const { getCWDURL } = require('internal/util');
        decorateErrorWithCommonJSHints(err, mainPath, getCWDURL());
      }
      throw err;
    }
  }

  return mainPath;
}

/**
 * Determine whether the main entry point should be loaded through the ESM Loader.
 * @param {string} mainPath - Absolute path to the main entry point
 */
function shouldUseESMLoader(mainPath) {
  if (getOptionValue('--experimental-default-type') === 'module') { return true; }

  /**
   * @type {string[]} userLoaders A list of custom loaders registered by the user
   * (or an empty list when none have been registered).
   */
  const userLoaders = getOptionValue('--experimental-loader');
  /**
   * @type {string[]} userImports A list of preloaded modules registered by the user
   * (or an empty list when none have been registered).
   */
  const userImports = getOptionValue('--import');
  if (userLoaders.length > 0 || userImports.length > 0) { return true; }

  // Determine the module format of the entry point.
  if (mainPath && StringPrototypeEndsWith(mainPath, '.mjs')) { return true; }
  if (!mainPath || StringPrototypeEndsWith(mainPath, '.cjs')) { return false; }

  if (getOptionValue('--experimental-strip-types')) {
    // This ensures that --experimental-default-type=commonjs and .mts files are treated as commonjs
    if (getOptionValue('--experimental-default-type') === 'commonjs') { return false; }
    if (mainPath && StringPrototypeEndsWith(mainPath, '.cts')) { return false; }
    // This will likely change in the future to start with commonjs loader by default
    if (mainPath && StringPrototypeEndsWith(mainPath, '.mts')) { return true; }
  }

  const type = getNearestParentPackageJSONType(mainPath);

  // No package.json or no `type` field.
  if (type === undefined || type === 'none') {
    return false;
  }

  return type === 'module';
}

/**
 * @param {function(ModuleLoader):ModuleWrap|undefined} callback
 */
async function asyncRunEntryPointWithESMLoader(callback) {
  const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
  try {
    const userImports = getOptionValue('--import');
    if (userImports.length > 0) {
      const parentURL = getCWDURL().href;
      for (let i = 0; i < userImports.length; i++) {
        await cascadedLoader.import(userImports[i], parentURL, kEmptyObject);
      }
    } else {
      cascadedLoader.forceLoadHooks();
    }
    await callback(cascadedLoader);
  } catch (err) {
    if (hasUncaughtExceptionCaptureCallback()) {
      process._fatalException(err);
      return;
    }
    triggerUncaughtException(
      err,
      true, /* fromPromise */
    );
  }
}

/**
 * This initializes the ESM loader and runs --import (if any) before executing the
 * callback to run the entry point.
 * If the callback intends to evaluate a ESM module as entry point, it should return
 * the corresponding ModuleWrap so that stalled TLA can be checked a process exit.
 * @param {function(ModuleLoader):ModuleWrap|undefined} callback
 * @returns {Promise}
 */
function runEntryPointWithESMLoader(callback) {
  const promise = asyncRunEntryPointWithESMLoader(callback);
  // Register the promise - if by the time the event loop finishes running, this is
  // still unsettled, we'll search the graph from the entry point module and print
  // the location of any unsettled top-level await found.
  globalThis[entry_point_promise_private_symbol] = promise;
  return promise;
}

/**
 * Parse the CLI main entry point string and run it.
 * For backwards compatibility, we have to run a bunch of monkey-patchable code that belongs to the CJS loader (exposed
 * by `require('module')`) even when the entry point is ESM.
 * This monkey-patchable code is bypassed under `--experimental-default-type=module`.
 * Because of backwards compatibility, this function is exposed publicly via `import { runMain } from 'node:module'`.
 * Because of module detection, this function will attempt to run ambiguous (no explicit extension, no
 * `package.json` type field) entry points as CommonJS first; under certain conditions, it will retry running as ESM.
 * @param {string} main - First positional CLI argument, such as `'entry.js'` from `node entry.js`
 */
function executeUserEntryPoint(main = process.argv[1]) {
  const resolvedMain = resolveMainPath(main);
  const useESMLoader = shouldUseESMLoader(resolvedMain);
  let mainURL;
  // Unless we know we should use the ESM loader to handle the entry point per the checks in `shouldUseESMLoader`, first
  // try to run the entry point via the CommonJS loader; and if that fails under certain conditions, retry as ESM.
  if (!useESMLoader) {
    const cjsLoader = require('internal/modules/cjs/loader');
    const { wrapModuleLoad } = cjsLoader;
    wrapModuleLoad(main, null, true);
  } else {
    const mainPath = resolvedMain || main;
    if (mainURL === undefined) {
      mainURL = pathToFileURL(mainPath).href;
    }

    runEntryPointWithESMLoader((cascadedLoader) => {
      // Note that if the graph contains unsettled TLA, this may never resolve
      // even after the event loop stops running.
      return cascadedLoader.import(mainURL, undefined, { __proto__: null }, true);
    });
  }
}

module.exports = {
  executeUserEntryPoint,
  runEntryPointWithESMLoader,
};
