'use strict';

require('../common');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const assert = require('assert');

// Verify that the default Error.prepareStackTrace is present.
assert.strictEqual(typeof Error.prepareStackTrace, 'function');

// Error.prepareStackTrace() can be overridden.
{
  let prepareCalled = false;
  Error.prepareStackTrace = (_error, trace) => {
    prepareCalled = true;
  };
  try {
    throw new Error('foo');
  } catch (err) {
    err.stack; // eslint-disable-line no-unused-expressions
  }
  assert(prepareCalled);
}

if (process.argv[2] !== 'child') {
  // Verify that the above test still passes when source-maps support is
  // enabled.
  spawnSyncAndExitWithoutError(
    process.execPath,
    ['--enable-source-maps', __filename, 'child']);
}
