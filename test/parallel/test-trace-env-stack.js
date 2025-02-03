'use strict';

// This tests that --trace-env-js-stack and --trace-env-native-stack works.
require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');

// Check reads from the Node.js internals.
// The stack trace may vary depending on internal refactorings, but it's relatively
// stable that:
// 1. Some C++ code would check the environment variables using
//    node::credentials::SafeGetenv.
// 2. Some JavaScript code would access process.env which goes to node::EnvGetter
// 3. Some JavaScript code would access process.env from pre_execution where
//    the run time states are used to finish bootstrap.
spawnSyncAndAssert(process.execPath, ['--trace-env-native-stack', fixtures.path('empty.js')], {
  stderr(output) {
    if (output.includes('PrintTraceEnvStack')) {  // Some platforms do not support back traces.
      assert.match(output, /node::credentials::SafeGetenv/);  // This is usually not optimized away.
    }
  }
});

spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('empty.js')], {
  stderr: /internal\/process\/pre_execution/
});

// Check get from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'get.js')], {
  env: {
    ...process.env,
    FOO: 'FOO',
    BAR: undefined,
  }
}, {
  stderr(output) {
    assert.match(output, /get\.js:1:25/);
    assert.match(output, /get\.js:2:25/);
  }
});

// Check set from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'set.js')], {
  stderr(output) {
    assert.match(output, /set\.js:1:17/);
  }
});

// // Check define from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'define.js')], {
  stderr(output) {
    assert.match(output, /define\.js:1:8/);
  }
});

// Check query from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'query.js')], {
  ...process.env,
  FOO: 'FOO',
  BAR: undefined,
  BAZ: undefined,
}, {
  stderr(output) {
    assert.match(output, /query\.js:1:19/);
    assert.match(output, /query\.js:2:20/);
    assert.match(output, /query\.js:3:25/);
  }
});

// Check delete from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'delete.js')], {
  stderr(output) {
    assert.match(output, /delete\.js:1:16/);
  }
});

// Check enumeration from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env-js-stack', fixtures.path('process-env', 'enumerate.js') ], {
  stderr(output) {
    assert.match(output, /enumerate\.js:1:8/);
    assert.match(output, /enumerate\.js:3:26/);
  }
});
