'use strict';

require('../common');
const vm = require('vm');
const assert = require('assert');

const sym1 = Symbol('1');
const sym2 = Symbol('2');
const sandbox = {
  a: true,
  [sym1]: true,
};
Object.defineProperty(sandbox, 'b', { value: true });
Object.defineProperty(sandbox, sym2, { value: true });

const ctx = vm.createContext(sandbox);

assert.deepStrictEqual(Reflect.ownKeys(sandbox), ['a', 'b', sym1, sym2]);
assert.deepStrictEqual(Object.getOwnPropertyNames(sandbox), ['a', 'b']);
assert.deepStrictEqual(Object.getOwnPropertySymbols(sandbox), [sym1, sym2]);

const nativeKeys = vm.runInNewContext('Reflect.ownKeys(this);');
const ownKeys = vm.runInContext('Reflect.ownKeys(this);', ctx);
const restKeys = ownKeys.filter((key) => !nativeKeys.includes(key));
// This should not fail
assert.deepStrictEqual(Array.from(restKeys), ['a', 'b', sym1, sym2]);
