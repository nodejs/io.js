// Flags: --experimental-eventsource
'use strict';

require('../common');
const assert = require('assert');

assert.strictEqual(typeof EventSource, 'function');
