'use strict';

const {
  RegExpPrototypeExec,
} = primordials;

const {
  prepareMainThreadExecution,
  markBootstrapComplete,
} = require('internal/process/pre_execution');
const { getOptionValue } = require('internal/options');
const { emitExperimentalWarning } = require('internal/util');

const isEntryURL = getOptionValue('--entry-url');
const mainEntry = prepareMainThreadExecution(!isEntryURL);

markBootstrapComplete();

// Necessary to reset RegExp statics before user code runs.
RegExpPrototypeExec(/^/, '');

if (isEntryURL) {
  emitExperimentalWarning('--entry-url');
}

/**
 * To support legacy monkey-patching of `Module.runMain`, we call `runMain` here to have the CommonJS loader begin the
 * execution of the main entry point, even if the ESM loader immediately takes over because the main entry is an ES
 * module or one of the other opt-in conditions (such as the use of `--import`) are met. Users can monkey-patch before
 * the main entry point is loaded by doing so via scripts loaded through `--require`. This monkey-patchability is
 * undesirable and should be removed once the module customization hooks provide equivalent functionality.
 */
require('internal/modules/cjs/loader').Module.runMain(mainEntry);
