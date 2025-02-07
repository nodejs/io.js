'use strict';

// This tests that --trace-env works.
const common = require('../common');
const { spawnSyncAndAssert } = require('../common/child_process');
const assert = require('assert');
const fixtures = require('../common/fixtures');

// Check reads from the Node.js internals.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('empty.js')], {
  stderr(output) {
    // This is a non-exhaustive list of the environment variables checked
    // during startup of an empty script at the time this test was written.
    // If the internals remove any one of them, the checks here can be updated
    // accordingly.
    if (common.hasIntl) {
      assert.match(output, /get "NODE_ICU_DATA"/);
    }
    if (common.hasCrypto) {
      assert.match(output, /get "NODE_EXTRA_CA_CERTS"/);

      const { hasOpenSSL3 } = require('../common/crypto');
      if (hasOpenSSL3) {
        assert.match(output, /get "OPENSSL_CONF"/);
      }
    }
    assert.match(output, /get "NODE_DEBUG_NATIVE"/);
    assert.match(output, /get "NODE_COMPILE_CACHE"/);
    assert.match(output, /get "NODE_NO_WARNINGS"/);
    assert.match(output, /get "NODE_V8_COVERAGE"/);
    assert.match(output, /get "NODE_DEBUG"/);
    assert.match(output, /get "NODE_CHANNEL_FD"/);
    assert.match(output, /get "NODE_UNIQUE_ID"/);
    if (common.isWindows) {
      assert.match(output, /get "USERPROFILE"/);
    } else {
      assert.match(output, /get "HOME"/);
    }
    assert.match(output, /get "NODE_PATH"/);
    assert.match(output, /get "WATCH_REPORT_DEPENDENCIES"/);
  }
});

// Check get from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'get.js') ], {
  env: {
    ...process.env,
    FOO: 'FOO',
    BAR: undefined,
  }
}, {
  stderr(output) {
    assert.match(output, /get "FOO"/);
    assert.match(output, /get "BAR"/);
  }
});

// Check set from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'set.js') ], {
  stderr(output) {
    assert.match(output, /set "FOO"/);
  }
});

// Check define from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'define.js') ], {
  stderr(output) {
    assert.match(output, /set "FOO"/);
  }
});

// Check query from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'query.js') ], {
  env: {
    ...process.env,
    FOO: 'FOO',
    BAR: undefined,
    BAZ: undefined,
  }
}, {
  stderr(output) {
    assert.match(output, /query "FOO"/);
    assert.match(output, /query "BAR"/);
    assert.match(output, /query "BAZ"/);
  }
});

// Check delete from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'delete.js') ], {
  stderr(output) {
    assert.match(output, /delete "FOO"/);
  }
});

// Check enumeration from user land.
spawnSyncAndAssert(process.execPath, ['--trace-env', fixtures.path('process-env', 'enumerate.js') ], {
  stderr(output) {
    const matches = output.match(/enumerate environment variables/g);
    assert.strictEqual(matches.length, 2);
  }
});
