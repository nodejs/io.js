'use strict';

const {
  ReflectApply,
  Symbol,
} = primordials;

const {
  ContextifyScript,
  compileFunction,
} = internalBinding('contextify');
const {
  runInContext,
} = ContextifyScript.prototype;
const {
  vm_dynamic_import_default_internal,
  vm_dynamic_import_main_context_default,
  vm_dynamic_import_no_callback,
  vm_dynamic_import_missing_flag,
} = internalBinding('symbols');
const {
  validateFunction,
} = require('internal/validators');

const {
  getOptionValue,
} = require('internal/options');
const {
  privateSymbols: {
    contextify_context_private_symbol,
  },
} = internalBinding('util');

/**
 * Checks if the given object is a context object.
 * @param {object} object - The object to check.
 * @returns {boolean} - Returns true if the object is a context object, else false.
 */
function isContext(object) {
  return object[contextify_context_private_symbol] !== undefined;
}

/**
 * Retrieves the host-defined option ID based on the provided importModuleDynamically and hint.
 * @param {import('internal/modules/esm/utils').ImportModuleDynamicallyCallback | undefined} importModuleDynamically -
 * The importModuleDynamically function or undefined.
 * @param {string} hint - The hint for the option ID.
 * @returns {symbol | import('internal/modules/esm/utils').ImportModuleDynamicallyCallback} - The host-defined option
 * ID.
 */
function getHostDefinedOptionId(importModuleDynamically, hint) {
  if (importModuleDynamically === vm_dynamic_import_main_context_default ||
      importModuleDynamically === vm_dynamic_import_default_internal) {
    return importModuleDynamically;
  }

  if (importModuleDynamically !== undefined) {
    // Check that it's either undefined or a function before we pass
    // it into the native constructor.
    validateFunction(importModuleDynamically,
                     'options.importModuleDynamically');
  }
  if (importModuleDynamically === undefined) {
    // We need a default host defined options that are the same for all
    // scripts not needing custom module callbacks so that the isolate
    // compilation cache can be hit.
    return vm_dynamic_import_no_callback;
  }
  // We should've thrown here immediately when we introduced
  // --experimental-vm-modules and importModuleDynamically, but since
  // users are already using this callback to throw a similar error,
  // we also defer the error to the time when an actual import() is called
  // to avoid breaking them. To ensure that the isolate compilation
  // cache can still be hit, use a constant sentinel symbol here.
  if (!getOptionValue('--experimental-vm-modules')) {
    return vm_dynamic_import_missing_flag;
  }

  return Symbol(hint);
}

/**
 * Registers a dynamically imported module for customization.
 * @param {string} referrer - The path of the referrer module.
 * @param {import('internal/modules/esm/utils').ImportModuleDynamicallyCallback} importModuleDynamically - The
 * dynamically imported module function to be registered.
 */
function registerImportModuleDynamically(referrer, importModuleDynamically) {
  // If it's undefined or certain known symbol, there's no customization so
  // no need to register anything.
  if (importModuleDynamically === undefined ||
      importModuleDynamically === vm_dynamic_import_main_context_default ||
      importModuleDynamically === vm_dynamic_import_default_internal) {
    return;
  }
  const { importModuleDynamicallyWrap } = require('internal/vm/module');
  const { registerModule } = require('internal/modules/esm/utils');
  registerModule(referrer, {
    __proto__: null,
    importModuleDynamically:
      importModuleDynamicallyWrap(importModuleDynamically),
  });
}

/**
 * Compiles a function from the given code string.
 * @param {string} code - The code string to compile.
 * @param {string} filename - The filename to use for the compiled function.
 * @param {number} lineOffset - The line offset to use for the compiled function.
 * @param {number} columnOffset - The column offset to use for the compiled function.
 * @param {Buffer} [cachedData=undefined] - The cached data to use for the compiled function.
 * @param {boolean} produceCachedData - Whether to produce cached data for the compiled function.
 * @param {ReturnType<import('vm').createContext} [parsingContext=undefined] - The parsing context to use for the
 * compiled function.
 * @param {object[]} [contextExtensions=[]] - An array of context extensions to use for the compiled function.
 * @param {string[]} [params] - An optional array of parameter names for the compiled function.
 * @param {symbol} hostDefinedOptionId - A symbol referenced by the field `host_defined_option_symbol`.
 * @param {import('internal/modules/esm/utils').ImportModuleDynamicallyCallback} [importModuleDynamically] -
 * A function to use for dynamically importing modules.
 * @returns {object} An object containing the compiled function and any associated data.
 * @throws {TypeError} If any of the arguments are of the wrong type.
 * @throws {ERR_INVALID_ARG_TYPE} If the parsing context is not a valid context object.
 */
function internalCompileFunction(
  code, filename, lineOffset, columnOffset,
  cachedData, produceCachedData, parsingContext, contextExtensions,
  params, hostDefinedOptionId, importModuleDynamically) {
  const result = compileFunction(
    code,
    filename,
    lineOffset,
    columnOffset,
    cachedData,
    produceCachedData,
    parsingContext,
    contextExtensions,
    params,
    hostDefinedOptionId,
  );

  if (produceCachedData) {
    result.function.cachedDataProduced = result.cachedDataProduced;
  }

  if (result.cachedData) {
    result.function.cachedData = result.cachedData;
  }

  if (typeof result.cachedDataRejected === 'boolean') {
    result.function.cachedDataRejected = result.cachedDataRejected;
  }

  registerImportModuleDynamically(result.function, importModuleDynamically);

  return result;
}

/**
 * Creates a contextify script.
 * @param {string} code - The code of the script.
 * @param {string} filename - The filename of the script.
 * @param {number} lineOffset - The line offset of the script.
 * @param {number} columnOffset - The column offset of the script.
 * @param {Buffer} cachedData - The cached data of the script.
 * @param {boolean} produceCachedData - Indicates whether to produce cached data.
 * @param {object} parsingContext - The parsing context of the script.
 * @param {number} hostDefinedOptionId - The host-defined option ID.
 * @param {boolean} importModuleDynamically - Indicates whether to import modules dynamically.
 * @returns {ContextifyScript} The created contextify script.
 */
function makeContextifyScript(code,
                              filename,
                              lineOffset,
                              columnOffset,
                              cachedData,
                              produceCachedData,
                              parsingContext,
                              hostDefinedOptionId,
                              importModuleDynamically) {
  let script;
  // Calling `ReThrow()` on a native TryCatch does not generate a new
  // abort-on-uncaught-exception check. A dummy try/catch in JS land
  // protects against that.
  try { // eslint-disable-line no-useless-catch
    script = new ContextifyScript(code,
                                  filename,
                                  lineOffset,
                                  columnOffset,
                                  cachedData,
                                  produceCachedData,
                                  parsingContext,
                                  hostDefinedOptionId);
  } catch (e) {
    throw e; /* node-do-not-add-exception-line */
  }

  registerImportModuleDynamically(script, importModuleDynamically);
  return script;
}

/**
 * Runs a script in the current context.
 * Internal version of `vm.Script.prototype.runInThisContext()` which skips argument validation.
 * @param {ReturnType<makeContextifyScript>} script - The script to run.
 * @param {boolean} displayErrors - Whether to display errors.
 * @param {boolean} breakOnFirstLine - Whether to break on the first line.
 */
function runScriptInThisContext(script, displayErrors, breakOnFirstLine) {
  return ReflectApply(
    runInContext,
    script,
    [
      null,                // sandbox - use current context
      -1,                  // timeout
      displayErrors,       // displayErrors
      false,               // breakOnSigint
      breakOnFirstLine,    // breakOnFirstLine
    ],
  );
}

module.exports = {
  getHostDefinedOptionId,
  internalCompileFunction,
  isContext,
  makeContextifyScript,
  registerImportModuleDynamically,
  runScriptInThisContext,
};
