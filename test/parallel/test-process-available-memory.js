'use strict';
require('../common');
const assert = require('assert');
const availableMemory = process.availableMemory();
assert(typeof availableMemory, 'number');
