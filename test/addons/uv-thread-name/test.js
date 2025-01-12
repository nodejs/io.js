'use strict';
const common = require('../../common');
const assert = require('node:assert');
const bindingPath = require.resolve(`./build/${common.buildType}/binding`);
const binding = require(bindingPath);

assert.strictEqual(binding.getMainThreadName(), 'MainThread');
