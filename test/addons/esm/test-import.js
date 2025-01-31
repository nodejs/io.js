// Flags: --experimental-addon-modules
'use strict';
const common = require('../../common');

import('./test-esm.mjs')
  .then((mod) => mod.run())
  .then(common.mustCall());
