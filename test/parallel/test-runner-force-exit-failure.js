'use strict';
require('../common');
const { match, doesNotMatch, strictEqual } = require('node:assert');
const { spawnSync } = require('node:child_process');
const fixtures = require('../common/fixtures');
const fixture = fixtures.path('test-runner/throws_sync_and_async.js');
const r = spawnSync(process.execPath, ['--test', '--test-force-exit', fixture]);

strictEqual(r.status, 1);
strictEqual(r.signal, null);
strictEqual(r.stderr.toString(), '');

const stdout = r.stdout.toString();
match(stdout, /error: 'fails'/);
doesNotMatch(stdout, /this should not have a chance to be thrown/);
