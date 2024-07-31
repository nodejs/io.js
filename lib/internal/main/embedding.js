'use strict';

// This main script is currently only run when LoadEnvironment()
// is run with a non-null StartExecutionCallback or a UTF8
// main script. Effectively there are two cases where this happens:
// 1. It's a single-executable application *loading* a main script
//    bundled into the executable. This is currently done from
//    NodeMainInstance::Run().
// 2. It's an embedder application and LoadEnvironment() is invoked
//    as described above.

const {
  prepareMainThreadExecution,
} = require('internal/process/pre_execution');
const { isExperimentalSeaWarningNeeded, isSea } = internalBinding('sea');
const { emitExperimentalWarning } = require('internal/util');
const { emitWarningSync } = require('internal/process/warning');
const { BuiltinModule: { normalizeRequirableId } } = require('internal/bootstrap/realm');
const { Module } = require('internal/modules/cjs/loader');
const { compileFunctionForCJSLoader } = internalBinding('contextify');
const { maybeCacheSourceMap } = require('internal/source_map/source_map_cache');

const { codes: {
  ERR_UNKNOWN_BUILTIN_MODULE,
} } = require('internal/errors');

// Don't expand process.argv[1] because in a single-executable application or an
// embedder application, the user main script isn't necessarily provided via the
// command line (e.g. it could be provided via an API or bundled into the executable).
prepareMainThreadExecution(false, true);

const isLoadingSea = isSea();
if (isExperimentalSeaWarningNeeded()) {
  emitExperimentalWarning('Single executable application');
}

// This is roughly the same as:
//
// const mod = new Module(filename);
// mod._compile(content, filename);
//
// but the code has been duplicated because currently there is no way to set the
// value of require.main to module.
//
// TODO(RaisinTen): Find a way to deduplicate this.
function embedderRunCjs(content) {
  // The filename of the module (used for CJS module lookup)
  // is always the same as the location of the executable itself
  // at the time of the loading (which means it changes depending
  // on where the executable is in the file system).
  const filename = process.execPath;
  const customModule = new Module(filename, null);

  const {
    function: compiledWrapper,
    cachedDataRejected,
    sourceMapURL,
  } = compileFunctionForCJSLoader(
    content,
    filename,
    isLoadingSea, // is_sea_main
    false, // should_detect_module, ESM should be supported differently for embedded code
  );
  // Cache the source map for the module if present.
  if (sourceMapURL) {
    maybeCacheSourceMap(
      filename,
      content,
      customModule,
      false,  // isGeneratedSource
      undefined,  // sourceURL, TODO(joyeecheung): should be extracted by V8
      sourceMapURL,
    );
  }

  // cachedDataRejected is only set if cache from SEA is used.
  if (cachedDataRejected !== false && isLoadingSea) {
    emitWarningSync('Code cache data rejected.');
  }

  // Patch the module to make it look almost like a regular CJS module
  // instance.
  customModule.filename = process.execPath;
  customModule.paths = Module._nodeModulePaths(process.execPath);
  embedderRequire.main = customModule;

  return compiledWrapper(
    customModule.exports,  // exports
    embedderRequire,       // require
    customModule,          // module
    process.execPath,      // __filename
    customModule.path,     // __dirname
  );
}

let warnedAboutBuiltins = false;

function embedderRequire(id) {
  const normalizedId = normalizeRequirableId(id);
  if (!normalizedId) {
    if (isLoadingSea && !warnedAboutBuiltins) {
      emitWarningSync(
        'Currently the require() provided to the main script embedded into ' +
        'single-executable applications only supports loading built-in modules.\n' +
        'To load a module from disk after the single executable application is ' +
        'launched, use require("module").createRequire().\n' +
        'Support for bundled module loading or virtual file systems are under ' +
        'discussions in https://github.com/nodejs/single-executable');
      warnedAboutBuiltins = true;
    }
    throw new ERR_UNKNOWN_BUILTIN_MODULE(id);
  }
  return require(normalizedId);
}

return [process, embedderRequire, embedderRunCjs];
