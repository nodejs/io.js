'use strict';

// Flags: --network-family-autoselection-attempt-timeout=123

const { platformTimeout } = require('../common');

const assert = require('assert');
const { getDefaultAutoSelectFamilyAttemptTimeout } = require('net');

assert.strictEqual(getDefaultAutoSelectFamilyAttemptTimeout(), platformTimeout(123 * 10));
