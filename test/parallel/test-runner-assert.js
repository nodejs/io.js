'use strict';
require('../common');
const assert = require('node:assert');
const test = require('node:test');

const uncopiedKeys = [
  'AssertionError',
  'CallTracker',
  'strict',
];
test('only methods from node:assert are on t.assert', (t) => {
  const expectedKeys = Object.keys(assert).filter((key) => !uncopiedKeys.includes(key)).sort();
  assert.deepStrictEqual(Object.keys(t.assert).sort(), expectedKeys);
});

test('t.assert.ok correctly parses the stacktrace', (t) => {
  t.assert.throws(() => t.assert.ok(1 === 2), /t\.assert\.ok\(1 === 2\)/);
});
