'use strict';
require('../common');
const assert = require('node:assert');
const test = require('node:test');

test('expected methods are on t.assert', (t) => {
  const uncopiedKeys = [
    'AssertionError',
    'CallTracker',
    'strict',
  ];
  const assertKeys = Object.keys(assert).filter((key) => !uncopiedKeys.includes(key));
  const expectedKeys = ['snapshot', 'fileSnapshot'].concat(assertKeys).sort();
  assert.deepStrictEqual(Object.keys(t.assert).sort(), expectedKeys);
});

test('t.assert.ok correctly parses the stacktrace', (t) => {
  t.assert.throws(() => t.assert.ok(1 === 2), /t\.assert\.ok\(1 === 2\)/);
});
