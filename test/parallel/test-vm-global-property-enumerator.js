'use strict';
require('../common');
const globalNames = require('../common/globals');
const vm = require('vm');
const assert = require('assert');

// Regression of https://github.com/nodejs/node/issues/53346

const cases = [
  {
    get 1() {
      return 'value';
    },
    get key() {
      return 'value';
    },
  },
  {
    // Intentionally single setter.
    // eslint-disable-next-line accessor-pairs
    set key(value) {},
    // eslint-disable-next-line accessor-pairs
    set 1(value) {},
  },
  {},
  {
    key: 'value',
    1: 'value',
  },
  (new class GetterObject {
    get key() {
      return 'value';
    }
    get 1() {
      return 'value';
    }
  }()),
  (new class SetterObject {
    // Intentionally single setter.
    // eslint-disable-next-line accessor-pairs
    set key(value) {
      // noop
    }
    // eslint-disable-next-line accessor-pairs
    set 1(value) {
      // noop
    }
  }()),
  [],
  [['key', 'value']],
  {
    __proto__: {
      key: 'value',
      1: 'value',
    },
  },
  (() => {
    const obj = {
      __proto__: {
        [Symbol.toStringTag]: 'proto',
      },
    };
    Object.defineProperty(obj, '1', {
      value: 'value',
      enumerable: false,
      configurable: true,
    });
    Object.defineProperty(obj, 'key', {
      value: 'value',
      enumerable: false,
      configurable: true,
    });
    Object.defineProperty(obj, Symbol('symbol'), {
      value: 'value',
      enumerable: false,
      configurable: true,
    });
    Object.defineProperty(obj, Symbol('symbol-enumerable'), {
      value: 'value',
      enumerable: true,
      configurable: true,
    });
    return obj;
  })(),
];

for (const [idx, obj] of cases.entries()) {
  const ctx = vm.createContext(obj);
  const globalObj = vm.runInContext('this', ctx);
  assert.deepStrictEqual(Object.keys(globalObj), Object.keys(obj), `Case ${idx} failed: Object.keys`);

  const ownPropertyNamesInner = difference(Object.getOwnPropertyNames(globalObj), globalNames.intrinsics);
  const ownPropertyNamesOuter = Object.getOwnPropertyNames(obj);
  assert.deepStrictEqual(
    ownPropertyNamesInner,
    ownPropertyNamesOuter,
    `Case ${idx} failed: Object.getOwnPropertyNames`
  );

  // FIXME(legendecas): globalThis[@@toStringTag] is unconditionally
  // initialized to the sandbox's constructor name, even if it does not exist
  // on the sandbox object. This may incorrectly initialize the prototype
  // @@toStringTag on the globalThis as an own property, like
  // Window.prototype[@@toStringTag] should be a property on the prototype.
  assert.deepStrictEqual(
    Object.getOwnPropertySymbols(globalObj).filter((it) => it !== Symbol.toStringTag),
    Object.getOwnPropertySymbols(obj),
    `Case ${idx} failed: Object.getOwnPropertySymbols`
  );
}

function difference(arrA, arrB) {
  const setB = new Set(arrB);
  return arrA.filter((x) => !setB.has(x));
};
