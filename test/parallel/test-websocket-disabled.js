// Flags: --no-experimental-websocket
'use strict';

require('../common');
const assert = require('assert');

assert.strictEqual(typeof WebSocket, 'undefined');
assert.strictEqual(typeof CloseEvent, 'undefined');
