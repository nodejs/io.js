'use strict';

const {
  ArrayPrototypeSlice,
} = primordials;

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');
const { isUsingInspector } = require('internal/util/inspector');
const { run } = require('internal/test_runner/runner');
const { parseCommandLine } = require('internal/test_runner/utils');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');
let debug = require('internal/util/debuglog').debuglog('test_runner', (fn) => {
  debug = fn;
});

prepareMainThreadExecution(false);
markBootstrapComplete();

const options = parseCommandLine();

if (isUsingInspector() && options.isolation === 'process') {
  process.emitWarning('Using the inspector with --test forces running at a concurrency of 1. ' +
  'Use the inspectPort option to run with concurrency');
  options.concurrency = 1;
  options.inspectPort = process.debugPort;
}

options.globPatterns = ArrayPrototypeSlice(process.argv, 1);

debug('test runner configuration:', options);
run(options).on('test:summary', (data) => {
  if (!data.success) {
    process.exitCode = kGenericUserError;
  }
});
