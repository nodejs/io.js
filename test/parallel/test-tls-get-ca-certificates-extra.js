'use strict';
// This tests that tls.getCACertificates('extra') returns the extra
// certificates from NODE_EXTRA_CA_CERTS correctly.

const common = require('../common');

if (!common.hasCrypto) common.skip('missing crypto');

const tmpdir = require('../common/tmpdir');
const fs = require('fs');
const assert = require('assert');
const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const fixtures = require('../common/fixtures');

tmpdir.refresh();
const certsJSON = tmpdir.resolve('certs.json');

// If NODE_EXTRA_CA_CERTS is set, it should contain a list of certificates.
spawnSyncAndExitWithoutError(process.execPath, [fixtures.path('tls-get-ca-certificates.js')], {
  env: {
    ...process.env,
    NODE_EXTRA_CA_CERTS: fixtures.path('keys', 'ca1-cert.pem'),
    CA_TYPE: 'extra',
    CA_OUT: certsJSON,
  }
});

const parsed = JSON.parse(fs.readFileSync(certsJSON, 'utf-8'));
assert.deepStrictEqual(parsed, [fixtures.readKey('ca1-cert.pem', 'utf8')]);
