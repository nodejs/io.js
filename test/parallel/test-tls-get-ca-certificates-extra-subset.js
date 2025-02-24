'use strict';
// This tests that tls.getCACertificates('defulat') returns a superset
// of tls.getCACertificates('extra') when NODE_EXTRA_CA_CERTS is used.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const { spawnSyncAndExitWithoutError } = require('../common/child_process');
const fixtures = require('../common/fixtures');

spawnSyncAndExitWithoutError(process.execPath, [fixtures.path('tls-check-extra-ca-certificates.js')], {
  env: {
    ...process.env,
    NODE_EXTRA_CA_CERTS: fixtures.path('keys', 'ca1-cert.pem'),
  }
});
