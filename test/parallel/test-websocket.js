'use strict';

require('../common');
const assert = require('assert');

assert.strictEqual(typeof WebSocket, 'function');
assert.strictEqual(typeof CloseEvent, 'function');
