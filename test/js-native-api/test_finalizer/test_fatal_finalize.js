'use strict';
const common = require('../../common');

if (process.argv[2] === 'child') {
  const test_finalizer = require(`./build/${common.buildType}/test_finalizer`);

  (() => {
    const obj = {};
    test_finalizer.addFinalizerFailOnJS(obj);
  })();

  // Collect garbage 10 times. At least one of those should throw the exception
  // and cause the whole process to bail with it, its text printed to stderr and
  // asserted by the parent process to match expectations.
  let gcCount = 10;
  (function gcLoop() {
    global.gc();
    if (--gcCount > 0) {
      setImmediate(() => gcLoop());
    }
  })();
  return;
}

const assert = require('assert');
const { spawnSync } = require('child_process');
const child = spawnSync(process.execPath, [
  '--expose-gc', __filename, 'child',
]);
assert(common.nodeProcessAborted(child.status, child.signal));
assert.match(child.stderr.toString(), /Finalizer is calling a function that may affect GC state/);
