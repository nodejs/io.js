'use strict';

const { platformTimeout } = require('../common');

const assert = require('assert');
const { getDefaultAutoSelectFamilyAttemptTimeout } = require('net');

assert.strictEqual(getDefaultAutoSelectFamilyAttemptTimeout(), platformTimeout(2500));
