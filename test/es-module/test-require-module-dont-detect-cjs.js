// Flags: --experimental-require-module
'use strict';

require('../common');
const assert = require('assert');

assert.throws(() => {
  require('../fixtures/es-modules/es-note-unexpected-export-1.cjs');
}, {
  message: /Unexpected token 'export'/
});
