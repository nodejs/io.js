'use strict';
require('../common');
const vm = require('vm');
const assert = require('assert');

// Regression of https://github.com/nodejs/node/issues/53346

const cases = [
  {
    get key() {
      return 'value';
    },
  },
  {
    // Intentionally single setter.
    // eslint-disable-next-line accessor-pairs
    set key(value) {},
  },
  {},
  {
    key: 'value',
  },
  (new class GetterObject {
    get key() {
      return 'value';
    }
  }()),
  (new class SetterObject {
    // Intentionally single setter.
    // eslint-disable-next-line accessor-pairs
    set key(value) {
      // noop
    }
  }()),
  [],
  [['key', 'value']],
  {
    __proto__: {
      key: 'value',
    },
  },
];

for (const [idx, obj] of cases.entries()) {
  const ctx = vm.createContext(obj);
  const globalObj = vm.runInContext('this', ctx);
  const keys = Object.keys(globalObj);
  assert.deepStrictEqual(keys, Object.keys(obj), `Case ${idx} failed`);
}
