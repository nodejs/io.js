'use strict';

const assert = require('assert');
let expectWarning;

// The formats could change when V8 is updated, then the tests should be
// updated accordingly.
function assertResultShape(result) {
  assert.strictEqual(typeof result.jsMemoryEstimate, 'number');
  assert.strictEqual(typeof result.jsMemoryRange[0], 'number');
  assert.strictEqual(typeof result.jsMemoryRange[1], 'number');
}

function assertSummaryShape(result) {
  assert.strictEqual(typeof result, 'object');
  assert.strictEqual(typeof result.total, 'object');
  assertResultShape(result.total);
}

function assertDetailedShape(result, contexts = 0) {
  assert.strictEqual(typeof result, 'object');
  assert.strictEqual(typeof result.total, 'object');
  assert.strictEqual(typeof result.current, 'object');
  assertResultShape(result.total);
  assertResultShape(result.current);
  if (contexts === 0) {
    assert.deepStrictEqual(result.other, []);
  } else {
    assert.strictEqual(result.other.length, contexts);
    for (const item of result.other) {
      assertResultShape(item);
    }
  }
}

function assertSingleDetailedShape(result) {
  assert.strictEqual(typeof result, 'object');
  assert.strictEqual(typeof result.total, 'object');
  assert.strictEqual(typeof result.current, 'object');
  assert.deepStrictEqual(result.other, []);
  assertResultShape(result.total);
  assertResultShape(result.current);
}

function expectExperimentalWarning() {
  expectWarning ??= require('./').expectWarning;
  expectWarning(
    'ExperimentalWarning',
    'vm.measureMemory is an experimental feature and might change at any time',
  );
}

module.exports = {
  assertSummaryShape,
  assertDetailedShape,
  assertSingleDetailedShape,
  expectExperimentalWarning,
};
