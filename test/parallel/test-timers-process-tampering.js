// Check that setImmediate works even if process is tampered with.
// This is a regression test for https://github.com/nodejs/node/issues/17681.

'use strict';
const common = require('../common');
globalThis.process = {};  // Boom!
common.allowGlobals(globalThis.process);
setImmediate(common.mustCall());
