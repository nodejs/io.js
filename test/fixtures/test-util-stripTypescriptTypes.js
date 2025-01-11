'use strict';

const assert = require('assert');
const util = require('util');

assert.strictEqual(util.stripTypescriptTypes('let s: string'), 'let s        ');
