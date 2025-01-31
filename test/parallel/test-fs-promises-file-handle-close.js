// Flags: --expose-gc --no-warnings
'use strict';

// Test that a runtime warning is emitted when a FileHandle object
// is allowed to close on garbage collection. In the future, this
// test should verify that closing on garbage collection throws a
// process fatal exception.

const common = require('../common');
const assert = require('assert');
const { promises: fs } = require('fs');

const warning =
  'Closing a FileHandle object on garbage collection is deprecated. ' +
  'Please close FileHandle objects explicitly using ' +
  'FileHandle.prototype.close(). In the future, an error will be ' +
  'thrown if a file descriptor is closed during garbage collection.';

async function doOpen() {
  const fh = await fs.open(__filename);

  common.expectWarning({
    Warning: [[`Closing file descriptor ${fh.fd} on garbage collection`]],
    DeprecationWarning: [[warning, 'DEP0137']]
  });

  return fh;
}

doOpen().then(common.mustCall((fd) => {
  assert.strictEqual(typeof fd, 'object');
})).then(common.mustCall(() => {
  setImmediate(() => {
    // The FileHandle should be out-of-scope and no longer accessed now.
    globalThis.gc();

    // Wait an extra event loop turn, as the warning is emitted from the
    // native layer in an unref()'ed setImmediate() callback.
    setImmediate(common.mustCall());
  });
}));
