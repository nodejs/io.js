'use strict';
const common = require('../../common');
const assert = require('assert');
const bindingPath = require.resolve(`./build/${common.buildType}/binding`);
const binding = require(bindingPath);
assert.strictEqual(binding.hello(), 'world');
console.log('binding.hello() =', binding.hello());

const binding2 = require(require.resolve(`./build/${common.buildType}/binding2`));
assert.strictEqual(binding2.hello(), 'world');

// Test multiple loading of the same module.
delete require.cache[bindingPath];
const rebinding = require(bindingPath);
assert.strictEqual(rebinding.hello(), 'world');
assert.notStrictEqual(binding.hello, rebinding.hello);
