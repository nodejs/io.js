'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');
const { spawnSyncAndAssert } = require('../common/child_process');

if (process.env.NODE_PENDING_DEPRECATION)
  common.skip('test does not work when NODE_PENDING_DEPRECATION is set');

spawnSyncAndAssert(
  process.execPath,
  [ fixtures.path('warning_node_modules', 'new-buffer-cjs.js') ],
  {
    trim: true,
    stderr: '',
  }
);

spawnSyncAndAssert(
  process.execPath,
  [ fixtures.path('warning_node_modules', 'new-buffer-esm.mjs') ],
  {
    trim: true,
    stderr: '',
  }
);

spawnSyncAndAssert(
  process.execPath,
  [
    '--pending-deprecation',
    fixtures.path('warning_node_modules', 'new-buffer-cjs.js'),
  ],
  {
    stderr: /DEP0005/
  }
);

spawnSyncAndAssert(
  process.execPath,
  [
    '--pending-deprecation',
    fixtures.path('warning_node_modules', 'new-buffer-esm.mjs'),
  ],
  {
    stderr: /DEP0005/
  }
);
