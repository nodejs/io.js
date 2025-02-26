'use strict';

require('../common');

const { ok } = require('assert');

// Test verifying that Temporal is present in the global scope

ok(globalThis.Temporal);
