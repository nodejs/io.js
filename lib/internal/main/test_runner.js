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
const {
  parseCommandLine,
  setupTestReporters,
} = require('internal/test_runner/utils');
const { exitCodes: { kGenericUserError } } = internalBinding('errors');
let debug = require('internal/util/debuglog').debuglog('test_runner', (fn) => {
  debug = fn;
});

prepareMainThreadExecution(false);
markBootstrapComplete();

const {
  perFileTimeout,
  runnerConcurrency,
  shard,
  watchMode,
} = parseCommandLine();

let concurrency = runnerConcurrency;
let inspectPort;

if (isUsingInspector()) {
  process.emitWarning('Using the inspector with --test forces running at a concurrency of 1. ' +
  'Use the inspectPort option to run with concurrency');
  concurrency = 1;
  inspectPort = process.debugPort;
}

const options = {
  concurrency,
  inspectPort,
  watch: watchMode,
  setup: setupTestReporters,
  timeout: perFileTimeout,
  shard,
  globPatterns: ArrayPrototypeSlice(process.argv, 1),
};
debug('test runner configuration:', options);
run(options).on('test:fail', (data) => {
  if (data.todo === undefined || data.todo === false) {
    process.exitCode = kGenericUserError;
  }
});
