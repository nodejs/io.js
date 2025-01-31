// Flags: --experimental-addon-modules
'use strict';
const common = require('../../common');

require('./test-esm.mjs')
  .run().then(common.mustCall());
