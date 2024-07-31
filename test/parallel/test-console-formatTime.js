'use strict';
// Flags: --expose-internals
require('../common');
const { formatTime } = require('internal/util/debuglog');
const assert = require('assert');

assert.strictEqual(formatTime(100.0096), '100.01ms');
assert.strictEqual(formatTime(100.0115), '100.011ms');
assert.strictEqual(formatTime(1500.04), '1.500s');
assert.strictEqual(formatTime(1000.056), '1.000s');
assert.strictEqual(formatTime(60300.3), '1:00.300 (m:ss.mmm)');
assert.strictEqual(formatTime(4000457.4), '1:06:40.457 (h:mm:ss.mmm)');
assert.strictEqual(formatTime(3601310.4), '1:00:01.310 (h:mm:ss.mmm)');
assert.strictEqual(formatTime(3213601017.6), '892:40:01.018 (h:mm:ss.mmm)');
