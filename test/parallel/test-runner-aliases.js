'use strict';
require('../common');
const { strictEqual } = require('node:assert');
const test = require('node:test');

strictEqual(test.test, test);
strictEqual(test.it, test);
strictEqual(test.describe, test.suite);
