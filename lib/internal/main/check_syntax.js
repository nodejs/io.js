'use strict';

// If user passed `-c` or `--check` arguments to Node, check its syntax
// instead of actually running the file.

const { getOptionValue } = require('internal/options');
const { URL, pathToFileURL } = require('internal/url');
const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');

const {
  readStdin,
} = require('internal/process/execution');

const {
  Module: {
    _resolveFilename: resolveCJSModuleName,
  },
  wrapSafe,
} = require('internal/modules/cjs/loader');

// TODO(joyeecheung): not every one of these are necessary
prepareMainThreadExecution(true);

if (process.argv[1] && process.argv[1] !== '-') {
  // Expand process.argv[1] into a full path.
  const path = require('path');
  process.argv[1] = path.resolve(process.argv[1]);

  // Read the source.
  const filename = resolveCJSModuleName(process.argv[1]);

  const fs = require('fs');
  const source = fs.readFileSync(filename, 'utf-8');

  markBootstrapComplete();

  loadESMIfNeeded(() => checkSyntax(source, filename));
} else {
  markBootstrapComplete();

  loadESMIfNeeded(() => readStdin((code) => {
    checkSyntax(code, '[stdin]');
  }));
}

function loadESMIfNeeded(cb) {
  const hasModulePreImport = getOptionValue('--import').length > 0;

  if (hasModulePreImport) {
    require('internal/modules/run_main').runEntryPointWithESMLoader(cb);
    return;
  }
  cb();
}

async function checkSyntax(source, filename) {
  let format;
  if (filename === '[stdin]' || filename === '[eval]') {
    format = (getOptionValue('--input-type') === 'module' ||
      (getOptionValue('--experimental-default-type') === 'module' && getOptionValue('--input-type') !== 'commonjs')) ?
      'module' : 'commonjs';
  } else {
    const { defaultResolve } = require('internal/modules/esm/resolve');
    const { defaultGetFormat } = require('internal/modules/esm/get_format');
    const { url } = await defaultResolve(pathToFileURL(filename).toString());
    format = await defaultGetFormat(new URL(url));
  }

  if (format === 'module') {
    const { ModuleWrap } = internalBinding('module_wrap');
    new ModuleWrap(filename, undefined, source, 0, 0);
    return;
  }

  wrapSafe(filename, source, undefined, format);
}
