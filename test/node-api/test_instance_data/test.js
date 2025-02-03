'use strict';
// Test API calls for instance data.

const common = require('../../common');

if (module !== require.main) {
  // When required as a module, run the tests.
  const test_instance_data =
    require(`./build/${common.buildType}/test_instance_data`);

  // Test that instance data can be used in an async work callback.
  new Promise((resolve) => test_instance_data.asyncWorkCallback(resolve))

    // Test that the buffer finalizer can access the instance data.
    .then(() => new Promise((resolve) => {
      test_instance_data.testBufferFinalizer(resolve);
      global.gc();
    }))

    // Test that the thread-safe function can access the instance data.
    .then(() => new Promise((resolve) =>
      test_instance_data.testThreadsafeFunction(common.mustCall(),
                                                common.mustCall(resolve))));
} else {
  // When launched as a script, run tests in either a child process or in a
  // worker thread.
  const assert = require('assert');
  const requireAs = require('../../common/require-as');
  const runOptions = { stdio: ['inherit', 'pipe', 'inherit'] };
  const { spawnSync } = require('child_process');

  // Run tests in a child process.
  requireAs(__filename, ['--expose-gc'], runOptions, 'child');

  // Run tests in a worker thread in a child process.
  requireAs(__filename, ['--expose-gc'], runOptions, 'worker');

  function testProcessExit(addonName) {
    // Make sure that process exit is clean when the instance data has
    // references to JS objects.
    const path = require.resolve(`./build/${common.buildType}/${addonName}`);
    const child = spawnSync(process.execPath, ['-e', `require(${JSON.stringify(path)});`]);
    assert.strictEqual(child.signal, null);
    assert.strictEqual(child.status, 0);
    assert.strictEqual(child.stderr.toString(), 'addon_free');
  }

  testProcessExit('test_ref_then_set');
  testProcessExit('test_set_then_ref');
}
