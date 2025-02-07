'use strict';

const {
  RegExpPrototypeExec,
  StringPrototypeIndexOf,
  StringPrototypeSlice,
  Symbol,
  globalThis,
} = primordials;

const path = require('path');

const {
  codes: {
    ERR_EVAL_ESM_CANNOT_PRINT,
    ERR_INVALID_ARG_TYPE,
    ERR_UNCAUGHT_EXCEPTION_CAPTURE_ALREADY_SET,
  },
} = require('internal/errors');
const { pathToFileURL } = require('internal/url');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');
const { stripTypeScriptModuleTypes } = require('internal/modules/typescript');

const {
  executionAsyncId,
  clearDefaultTriggerAsyncId,
  clearAsyncIdStack,
  hasAsyncIdStack,
  afterHooksExist,
  emitAfter,
  popAsyncContext,
} = require('internal/async_hooks');
const { containsModuleSyntax } = internalBinding('contextify');
const { getOptionValue } = require('internal/options');
const {
  makeContextifyScript, runScriptInThisContext,
} = require('internal/vm');
const { emitExperimentalWarning } = require('internal/util');
// shouldAbortOnUncaughtToggle is a typed array for faster
// communication with JS.
const { shouldAbortOnUncaughtToggle } = internalBinding('util');

function tryGetCwd() {
  try {
    return process.cwd();
  } catch {
    // getcwd(3) can fail if the current working directory has been deleted.
    // Fall back to the directory name of the (absolute) executable path.
    // It's not really correct but what are the alternatives?
    return path.dirname(process.execPath);
  }
}

let evalIndex = 0;
function getEvalModuleUrl() {
  return `${pathToFileURL(process.cwd())}/[eval${++evalIndex}]`;
}

/**
 * Evaluate an ESM entry point and return the promise that gets fulfilled after
 * it finishes evaluation.
 * @param {string} source Source code the ESM
 * @param {boolean} print Whether the result should be printed.
 * @returns {Promise}
 */
function evalModuleEntryPoint(source, print) {
  if (print) {
    throw new ERR_EVAL_ESM_CANNOT_PRINT();
  }
  RegExpPrototypeExec(/^/, ''); // Necessary to reset RegExp statics before user code runs.
  return require('internal/modules/run_main').runEntryPointWithESMLoader(
    (loader) => loader.eval(source, getEvalModuleUrl(), true),
  );
}

function evalScript(name, body, breakFirstLine, print, shouldLoadESM = false) {
  const origModule = globalThis.module;  // Set e.g. when called from the REPL.
  const module = createModule(name);
  const baseUrl = pathToFileURL(module.filename).href;

  if (shouldUseModuleEntryPoint(name, body)) {
    return getOptionValue('--experimental-strip-types') ?
      evalTypeScriptModuleEntryPoint(body, print) :
      evalModuleEntryPoint(body, print);
  }

  const evalFunction = () => runScriptInContext(name,
                                                body,
                                                breakFirstLine,
                                                print,
                                                module,
                                                baseUrl,
                                                undefined,
                                                origModule);

  if (shouldLoadESM) {
    return require('internal/modules/run_main').runEntryPointWithESMLoader(evalFunction);
  }
  evalFunction();
}

const exceptionHandlerState = {
  captureFn: null,
  reportFlag: false,
};

function setUncaughtExceptionCaptureCallback(fn) {
  if (fn === null) {
    exceptionHandlerState.captureFn = fn;
    shouldAbortOnUncaughtToggle[0] = 1;
    process.report.reportOnUncaughtException = exceptionHandlerState.reportFlag;
    return;
  }
  if (typeof fn !== 'function') {
    throw new ERR_INVALID_ARG_TYPE('fn', ['Function', 'null'], fn);
  }
  if (exceptionHandlerState.captureFn !== null) {
    throw new ERR_UNCAUGHT_EXCEPTION_CAPTURE_ALREADY_SET();
  }
  exceptionHandlerState.captureFn = fn;
  shouldAbortOnUncaughtToggle[0] = 0;
  exceptionHandlerState.reportFlag =
    process.report.reportOnUncaughtException === true;
  process.report.reportOnUncaughtException = false;
}

function hasUncaughtExceptionCaptureCallback() {
  return exceptionHandlerState.captureFn !== null;
}

function noop() {}

// XXX(joyeecheung): for some reason this cannot be defined at the top-level
// and exported to be written to process._fatalException, it has to be
// returned as an *anonymous function* wrapped inside a factory function,
// otherwise it breaks the test-timers.setInterval async hooks test -
// this may indicate that node::errors::TriggerUncaughtException() should
// fix up the callback scope before calling into process._fatalException,
// or this function should take extra care of the async hooks before it
// schedules a setImmediate.
function createOnGlobalUncaughtException() {
  // The C++ land node::errors::TriggerUncaughtException() will
  // exit the process if it returns false, and continue execution if it
  // returns true (which indicates that the exception is handled by the user).
  return (er, fromPromise) => {
    // It's possible that defaultTriggerAsyncId was set for a constructor
    // call that threw and was never cleared. So clear it now.
    clearDefaultTriggerAsyncId();

    const type = fromPromise ? 'unhandledRejection' : 'uncaughtException';
    process.emit('uncaughtExceptionMonitor', er, type);
    if (exceptionHandlerState.captureFn !== null) {
      exceptionHandlerState.captureFn(er);
    } else if (!process.emit('uncaughtException', er, type)) {
      // If someone handled it, then great. Otherwise, die in C++ land
      // since that means that we'll exit the process, emit the 'exit' event.
      try {
        if (!process._exiting) {
          process._exiting = true;
          process.exitCode = kGenericUserError;
          process.emit('exit', kGenericUserError);
        }
      } catch {
        // Nothing to be done about it at this point.
      }
      return false;
    }

    // If we handled an error, then make sure any ticks get processed
    // by ensuring that the next Immediate cycle isn't empty.
    require('timers').setImmediate(noop);

    // Emit the after() hooks now that the exception has been handled.
    if (afterHooksExist()) {
      do {
        const asyncId = executionAsyncId();
        if (asyncId === 0)
          popAsyncContext(0);
        else
          emitAfter(asyncId);
      } while (hasAsyncIdStack());
    }
    // And completely empty the id stack, including anything that may be
    // cached on the native side.
    clearAsyncIdStack();

    return true;
  };
}

function readStdin(callback) {
  process.stdin.setEncoding('utf8');

  let code = '';
  process.stdin.on('data', (d) => {
    code += d;
  });

  process.stdin.on('end', () => {
    callback(code);
  });
}

/**
 * Adds the TS message to the error stack.
 *
 * At the 3rd line of the stack, the message is added.
 * @param {string} originalStack The stack to decorate
 * @param {string} newMessage the message to add to the error stack
 * @returns {void}
 */
function decorateCJSErrorWithTSMessage(originalStack, newMessage) {
  let index;
  for (let i = 0; i < 3; i++) {
    index = StringPrototypeIndexOf(originalStack, '\n', index + 1);
  }
  return StringPrototypeSlice(originalStack, 0, index) +
         '\n' + newMessage +
         StringPrototypeSlice(originalStack, index);
}

/**
 *
 * Wrapper of evalScript
 *
 * This function wraps the evaluation of the source code in a try-catch block.
 * If the source code fails to be evaluated, it will retry evaluating the source code
 * with the TypeScript parser.
 *
 * If the source code fails to be evaluated with the TypeScript parser,
 * it will rethrow the original error, adding the TypeScript error message to the stack.
 *
 * This way we don't change the behavior of the code, but we provide a better error message
 * in case of a typescript error.
 * @param {string} name The name of the file
 * @param {string} source The source code to evaluate
 * @param {boolean} breakFirstLine Whether to break on the first line
 * @param {boolean} print If the result should be printed
 * @param {boolean} shouldLoadESM If the code should be loaded as an ESM module
 * @returns {void}
 */
function evalTypeScript(name, source, breakFirstLine, print, shouldLoadESM = false) {
  const origModule = globalThis.module;  // Set e.g. when called from the REPL.
  const module = createModule(name);
  const baseUrl = pathToFileURL(module.filename).href;

  if (shouldUseModuleEntryPoint(name, source)) {
    return evalTypeScriptModuleEntryPoint(source, print);
  }

  let compiledScript;
  // This variable can be modified if the source code is stripped.
  let sourceToRun = source;
  try {
    compiledScript = compileScript(name, source, baseUrl);
  } catch (originalError) {
    try {
      sourceToRun = stripTypeScriptModuleTypes(source, name, false);
      // Retry the CJS/ESM syntax detection after stripping the types.
      if (shouldUseModuleEntryPoint(name, sourceToRun)) {
        return evalTypeScriptModuleEntryPoint(source, print);
      }
      // If the ContextifiedScript was successfully created, execute it.
      // outside the try-catch block to avoid catching runtime errors.
      compiledScript = compileScript(name, sourceToRun, baseUrl);
      // Emit the experimental warning after the code was successfully evaluated.
      emitExperimentalWarning('Type Stripping');
    } catch (tsError) {
      // If it's invalid or unsupported TypeScript syntax, rethrow the original error
      // with the TypeScript error message added to the stack.
      if (tsError.code === 'ERR_INVALID_TYPESCRIPT_SYNTAX' || tsError.code === 'ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX') {
        originalError.stack = decorateCJSErrorWithTSMessage(originalError.stack, tsError.message);
        throw originalError;
      }

      throw tsError;
    }
  }

  const evalFunction = () => runScriptInContext(name,
                                                sourceToRun,
                                                breakFirstLine,
                                                print,
                                                module,
                                                baseUrl,
                                                compiledScript,
                                                origModule);

  if (shouldLoadESM) {
    return require('internal/modules/run_main').runEntryPointWithESMLoader(evalFunction);
  }
  evalFunction();
}

/**
 * Wrapper of evalModuleEntryPoint
 *
 * This function wraps the compilation of the source code in a try-catch block.
 * If the source code fails to be compiled, it will retry transpiling the source code
 * with the TypeScript parser.
 * @param {string} source The source code to evaluate
 * @param {boolean} print If the result should be printed
 * @returns {Promise} The module evaluation promise
 */
function evalTypeScriptModuleEntryPoint(source, print) {
  if (print) {
    throw new ERR_EVAL_ESM_CANNOT_PRINT();
  }

  RegExpPrototypeExec(/^/, ''); // Necessary to reset RegExp statics before user code runs.

  return require('internal/modules/run_main').runEntryPointWithESMLoader(
    async (loader) => {
      const url = getEvalModuleUrl();
      let moduleWrap;
      try {
        // Compile the module to check for syntax errors.
        moduleWrap = loader.createModuleWrap(source, url);
      } catch (originalError) {
        try {
          const strippedSource = stripTypeScriptModuleTypes(source, url, false);
          // If the moduleWrap was successfully created, execute the module job.
          // outside the try-catch block to avoid catching runtime errors.
          moduleWrap = loader.createModuleWrap(strippedSource, url);
          // Emit the experimental warning after the code was successfully compiled.
          emitExperimentalWarning('Type Stripping');
        } catch (tsError) {
          // If it's invalid or unsupported TypeScript syntax, rethrow the original error
          // with the TypeScript error message added to the stack.
          if (tsError.code === 'ERR_INVALID_TYPESCRIPT_SYNTAX' ||
            tsError.code === 'ERR_UNSUPPORTED_TYPESCRIPT_SYNTAX') {
            originalError.stack = `${tsError.message}\n\n${originalError.stack}`;
            throw originalError;
          }

          throw tsError;
        }
      }
      // If the moduleWrap was successfully created either with by just compiling
      // or after transpilation, execute the module job.
      return loader.executeModuleJob(url, moduleWrap, true);
    },
  );
};

/**
 *
 * Function used to shortcut when `--input-type=module-typescript` is set.
 * @param {string} source
 * @param {boolean} print
 */
function parseAndEvalModuleTypeScript(source, print) {
  // We know its a TypeScript module, we can safely emit the experimental warning.
  const strippedSource = stripTypeScriptModuleTypes(source, getEvalModuleUrl());
  evalModuleEntryPoint(strippedSource, print);
}

/**
 * Function used to shortcut when `--input-type=commonjs-typescript` is set
 * @param {string} name The name of the file
 * @param {string} source The source code to evaluate
 * @param {boolean} breakFirstLine Whether to break on the first line
 * @param {boolean} print If the result should be printed
 * @param {boolean} shouldLoadESM If the code should be loaded as an ESM module
 * @returns {void}
 */
function parseAndEvalCommonjsTypeScript(name, source, breakFirstLine, print, shouldLoadESM = false) {
  // We know its a TypeScript module, we can safely emit the experimental warning.
  const strippedSource = stripTypeScriptModuleTypes(source, getEvalModuleUrl());
  evalScript(name, strippedSource, breakFirstLine, print, shouldLoadESM);
}

/**
 *
 * @param {string} name - The filename of the script.
 * @param {string} body - The code of the script.
 * @param {string} baseUrl Path of the parent importing the module.
 * @returns {ContextifyScript} The created contextify script.
 */
function compileScript(name, body, baseUrl) {
  const hostDefinedOptionId = Symbol(name);
  async function importModuleDynamically(specifier, _, importAttributes) {
    const cascadedLoader = require('internal/modules/esm/loader').getOrInitializeCascadedLoader();
    return cascadedLoader.import(specifier, baseUrl, importAttributes);
  }
  return makeContextifyScript(
    body,                    // code
    name,                    // filename,
    0,                       // lineOffset
    0,                       // columnOffset,
    undefined,               // cachedData
    false,                   // produceCachedData
    undefined,               // parsingContext
    hostDefinedOptionId,     // hostDefinedOptionId
    importModuleDynamically, // importModuleDynamically
  );
}

/**
 * @param {string} name - The filename of the script.
 * @param {string} body - The code of the script.
 * @returns {boolean} Whether the module entry point should be evaluated as a module.
 */
function shouldUseModuleEntryPoint(name, body) {
  return getOptionValue('--experimental-detect-module') &&
    getOptionValue('--input-type') === '' &&
    containsModuleSyntax(body, name, null, 'no CJS variables');
}

/**
 *
 * @param {string} name - The filename of the script.
 * @returns {import('internal/modules/esm/loader').CJSModule} The created module.
 */
function createModule(name) {
  const CJSModule = require('internal/modules/cjs/loader').Module;
  const cwd = tryGetCwd();
  const module = new CJSModule(name);
  module.filename = path.join(cwd, name);
  module.paths = CJSModule._nodeModulePaths(cwd);
  return module;
}

/**
 *
 * @param {string} name - The filename of the script.
 * @param {string} body - The code of the script.
 * @param {boolean} breakFirstLine Whether to break on the first line
 * @param {boolean} print If the result should be printed
 * @param {import('internal/modules/esm/loader').CJSModule} module The module
 * @param {string} baseUrl Path of the parent importing the module.
 * @param {object} compiledScript The compiled script.
 * @param {any} origModule The original module.
 * @returns {void}
 */
function runScriptInContext(name, body, breakFirstLine, print, module, baseUrl, compiledScript, origModule) {
  // Create wrapper for cache entry
  const script = `
      globalThis.module = module;
      globalThis.exports = exports;
      globalThis.__dirname = __dirname;
      globalThis.require = require;
      return (main) => main();
    `;
  globalThis.__filename = name;
  RegExpPrototypeExec(/^/, ''); // Necessary to reset RegExp statics before user code runs.
  const result = module._compile(script, `${name}-wrapper`)(() => {
    // If the script was already compiled, use it.
    return runScriptInThisContext(
      compiledScript ?? compileScript(name, body, baseUrl),
      true, !!breakFirstLine);
  });
  if (print) {
    const { log } = require('internal/console/global');

    process.on('exit', () => {
      log(result);
    });
  }
  if (origModule !== undefined)
    globalThis.module = origModule;
}

module.exports = {
  parseAndEvalCommonjsTypeScript,
  parseAndEvalModuleTypeScript,
  readStdin,
  tryGetCwd,
  evalModuleEntryPoint,
  evalTypeScript,
  evalScript,
  onGlobalUncaughtException: createOnGlobalUncaughtException(),
  setUncaughtExceptionCaptureCallback,
  hasUncaughtExceptionCaptureCallback,
};
