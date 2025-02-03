'use strict';

require('../common');
const {
  spawnSyncAndExitWithoutError,
  spawnSyncAndAssert,
} = require('../common/child_process');
const fixtures = require('../common/fixtures');
spawnSyncAndExitWithoutError(
  process.execPath,
  [
    fixtures.path('spawn-worker-with-copied-env'),
  ],
  {
    env: {
      ...process.env,
      NODE_OPTIONS: '--title=foo'
    }
  }
);

spawnSyncAndAssert(
  process.execPath,
  [
    fixtures.path('spawn-worker-with-trace-exit'),
  ],
  {
    env: {
      ...process.env,
    }
  },
  {
    stderr: /spawn-worker-with-trace-exit\.js:17/
  }
);
