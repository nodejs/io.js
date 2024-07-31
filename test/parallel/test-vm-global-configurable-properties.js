'use strict';
// https://github.com/nodejs/node/issues/47799

require('../common');
const assert = require('assert');
const vm = require('vm');

const ctx = vm.createContext();

const window = vm.runInContext('this', ctx);

Object.defineProperty(window, 'x', { value: '1', configurable: true });
assert.strictEqual(window.x, '1');
Object.defineProperty(window, 'x', { value: '2', configurable: true });
assert.strictEqual(window.x, '2');
