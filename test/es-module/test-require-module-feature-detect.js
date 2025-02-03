'use strict';

// This tests that process.features.require_module can be used to feature-detect
// require(esm) without triggering a warning.

require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');

spawnSyncAndAssert(process.execPath, [
  '--experimental-require-module',
  '-p',
  'process.features.require_module',
], {
  trim: true,
  stdout: 'true',
  stderr: '',  // Should not emit warnings.
});

// It is now enabled by default.
spawnSyncAndAssert(process.execPath, [
  '-p',
  'process.features.require_module',
], {
  trim: true,
  stdout: 'true',
  stderr: '',  // Should not emit warnings.
});

spawnSyncAndAssert(process.execPath, [
  '--no-experimental-require-module',
  '-p',
  'process.features.require_module',
], {
  trim: true,
  stdout: 'false',
  stderr: '',  // Should not emit warnings.
});
