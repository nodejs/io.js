'use strict';
// This tests that tls.getCACertificates('extra') returns an empty
// array if NODE_EXTRA_CA_CERTS is empty.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const tmpdir = require('../common/tmpdir');
const fs = require('fs');

const assert = require('assert');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const fixtures = require('../common/fixtures');

tmpdir.refresh();
const certsJSON = tmpdir.resolve('certs.json');

// If NODE_EXTRA_CA_CERTS is not set, it should be an empty array.
spawnSyncAndExitWithoutError(process.execPath, [fixtures.path('tls-get-ca-certificates.js')], {
  env: {
    ...process.env,
    NODE_EXTRA_CA_CERTS: undefined,
    CA_TYPE: 'extra',
    CA_OUT: certsJSON,
  }
});

const parsed = JSON.parse(fs.readFileSync(certsJSON, 'utf-8'));
assert.deepStrictEqual(parsed, []);
