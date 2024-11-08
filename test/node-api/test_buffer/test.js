'use strict';
// Flags: --expose-gc --no-concurrent-array-buffer-sweeping

const common = require('../../common');
const binding = require(`./build/${common.buildType}/test_buffer`);
const assert = require('assert');
const tick = require('util').promisify(require('../../common/tick'));

(async function() {
  assert.strictEqual(binding.newBuffer().toString(), binding.theText);
  assert.strictEqual(binding.newExternalBuffer().toString(), binding.theText);
  console.log('gc1');
  global.gc();
  assert.strictEqual(binding.getDeleterCallCount(), 0);
  await tick(10);
  assert.strictEqual(binding.getDeleterCallCount(), 1);
  assert.strictEqual(binding.copyBuffer().toString(), binding.theText);

  let buffer = binding.staticBuffer();
  assert.strictEqual(binding.bufferHasInstance(buffer), true);
  assert.strictEqual(binding.bufferInfo(buffer), true);
  buffer = null;
  global.gc();
  assert.strictEqual(binding.getDeleterCallCount(), 1);
  await tick(10);
  console.log('gc2');
  assert.strictEqual(binding.getDeleterCallCount(), 2);

  // To test this doesn't crash
  binding.invalidObjectAsBuffer({});

  const testBuffer = binding.bufferFromArrayBuffer();
  assert(testBuffer instanceof Buffer, 'Expected a Buffer');
})().then(common.mustCall());
