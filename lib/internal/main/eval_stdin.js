'use strict';

// Stdin is not a TTY, we will read it and execute it.

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');

const { getOptionValue } = require('internal/options');

const {
  evalModuleEntryPoint,
  evalScript,
  readStdin,
} = require('internal/process/execution');

prepareMainThreadExecution();
markBootstrapComplete();

readStdin((code) => {
  // This is necessary for fork() and CJS module compilation.
  // TODO(joyeecheung): pass this with something really internal.
  process._eval = code;

  const print = getOptionValue('--print');
  const shouldLoadESM = getOptionValue('--import').length > 0;
  if (getOptionValue('--input-type') === 'module' ||
    (getOptionValue('--experimental-default-type') === 'module' && getOptionValue('--input-type') !== 'commonjs')) {
    evalModuleEntryPoint(code, print);
  } else {
    evalScript('[stdin]',
               code,
               getOptionValue('--inspect-brk'),
               print,
               shouldLoadESM);
  }
});
