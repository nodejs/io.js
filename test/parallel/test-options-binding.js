// Flags: --expose-internals
'use strict';

const common = require('../common');
const assert = require('assert');
const { getOptionValue } = require('internal/options');

Map.prototype.get =
  common.mustNotCall('`getOptionValue` must not call user-mutable method');
assert.strictEqual(getOptionValue('--expose-internals'), true);

Object.prototype['--nonexistent-option'] = 'foo';
assert.strictEqual(getOptionValue('--nonexistent-option'), undefined);

// Make the test common global leak test happy.
delete Object.prototype['--nonexistent-option'];
