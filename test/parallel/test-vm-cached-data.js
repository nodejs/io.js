'use strict';
const common = require('../common');
const assert = require('assert');
const vm = require('vm');
const spawnSync = require('child_process').spawnSync;

function getSource(tag) {
  return `(function ${tag}() { return '${tag}'; })`;
}

function produce(source, count) {
  count ||= 1;

  const out = spawnSync(process.execPath, [ '-e', `
    'use strict';
    const assert = require('assert');
    const vm = require('vm');

    var data;
    for (var i = 0; i < ${count}; i++) {
      var script = new vm.Script(process.argv[1], {
        produceCachedData: true
      });

      assert(!script.cachedDataProduced || script.cachedData instanceof Buffer);

      if (script.cachedDataProduced)
        data = script.cachedData.toString('base64');
    }
    console.log(data);
  `, source]);

  assert.strictEqual(out.status, 0, String(out.stderr));

  return Buffer.from(out.stdout.toString(), 'base64');
}

function testProduceConsume() {
  const source = getSource('original');

  const data = produce(source);

  for (const cachedData of common.getArrayBufferViews(data)) {
    // It should consume code cache
    const script = new vm.Script(source, {
      cachedData
    });
    assert(!script.cachedDataRejected);
    assert.strictEqual(script.runInThisContext()(), 'original');
  }
}
testProduceConsume();

function testProduceMultiple() {
  const source = getSource('original');

  produce(source, 3);
}
testProduceMultiple();

function testRejectInvalid() {
  const source = getSource('invalid');

  const data = produce(source);

  // It should reject invalid code cache
  const script = new vm.Script(getSource('invalid_1'), {
    cachedData: data
  });
  assert(script.cachedDataRejected);
  assert.strictEqual(script.runInThisContext()(), 'invalid_1');
}
testRejectInvalid();

function testRejectSlice() {
  const source = getSource('slice');

  const data = produce(source).slice(4);

  const script = new vm.Script(source, {
    cachedData: data
  });
  assert(script.cachedDataRejected);
}
testRejectSlice();

// It should throw on non-Buffer cachedData
assert.throws(() => {
  new vm.Script('function abc() {}', {
    cachedData: 'ohai'
  });
}, {
  code: 'ERR_INVALID_ARG_TYPE',
  name: 'TypeError',
  message: /must be an instance of Buffer, TypedArray, or DataView/
});
