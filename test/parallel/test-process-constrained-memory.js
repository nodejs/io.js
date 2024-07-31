'use strict';
require('../common');
const assert = require('assert');

const constrainedMemory = process.constrainedMemory();
assert.strictEqual(typeof constrainedMemory, 'number');
