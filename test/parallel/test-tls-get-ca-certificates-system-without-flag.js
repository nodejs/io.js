'use strict';

// This tests that tls.getCACertificates() returns the system
// certificates correctly when --use-system-ca is disabled.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const tmpdir = require('../common/tmpdir');
const fs = require('fs');

const assert = require('assert');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const fixtures = require('../common/fixtures');
const tls = require('tls');

const certs = tls.getCACertificates('system');
if (certs.length === 0) {
  common.skip('No trusted system certificates installed. Skip.');
}

tmpdir.refresh();
const certsJSON = tmpdir.resolve('certs.json');
spawnSyncAndExitWithoutError(process.execPath, [
  '--no-use-system-ca',
  fixtures.path('tls-get-ca-certificates.js'),
], {
  env: {
    ...process.env,
    CA_TYPE: 'system',
    CA_OUT: certsJSON,
  }
});

const parsed = JSON.parse(fs.readFileSync(certsJSON, 'utf-8'));
assert.deepStrictEqual(parsed, certs);
