'use strict';
const {
  SafeMap,
} = primordials;
const {
  validateFunction,
  validateString,
} = require('internal/validators');
const assert = require('assert');
const methodsToCopy = [
  'deepEqual',
  'deepStrictEqual',
  'doesNotMatch',
  'doesNotReject',
  'doesNotThrow',
  'equal',
  'fail',
  'ifError',
  'match',
  'notDeepEqual',
  'notDeepStrictEqual',
  'notEqual',
  'notStrictEqual',
  'partialDeepStrictEqual',
  'rejects',
  'strictEqual',
  'throws',
];
let assertMap;

function getAssertionMap() {
  if (assertMap === undefined) {
    assertMap = new SafeMap();

    for (let i = 0; i < methodsToCopy.length; i++) {
      assertMap.set(methodsToCopy[i], assert[methodsToCopy[i]]);
    }
  }

  return assertMap;
}

function register(name, fn) {
  validateString(name, 'name');
  validateFunction(fn, 'fn');
  const map = getAssertionMap();
  map.set(name, fn);
}

module.exports = { getAssertionMap, register };
