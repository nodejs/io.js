// Flags: --expose-gc
'use strict';
const common = require('../common');

// This test verifies that when a V8 FinalizationRegistryCleanupTask is queue
// at the last moment when JavaScript can be executed, the callback of a
// FinalizationRegistry will not be invoked and the process should exit
// normally.

const reg = new FinalizationRegistry(
  common.mustNotCall('This FinalizationRegistry callback should never be called'));

function register() {
  // Create a temporary object in a new function scope to allow it to be GC-ed.
  reg.register({});
}

process.on('exit', () => {
  // This is the final chance to execute JavaScript.
  register();
  // Queue a FinalizationRegistryCleanupTask by a testing gc request.
  globalThis.gc();
});
