'use strict';

const common = require('../common');
const assert = require('assert');

common.allowGlobals('rawDebug');
assert.strictEqual(typeof global.rawDebug === 'function', true);

global.rawDebug = 42;
assert.strictEqual(global.rawDebug === 42, true);
