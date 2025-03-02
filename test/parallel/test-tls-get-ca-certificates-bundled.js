'use strict';
// This tests that tls.getCACertificates() returns the bundled
// certificates correctly.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const { assertIsCAArray } = require('../common/tls');

const certs = tls.getCACertificates('bundled');
assertIsCAArray(certs);

// It's the same as tls.rootCertificates - both are
// Mozilla CA stores across platform.
assert.strictEqual(certs, tls.rootCertificates);

// It's cached on subsequent accesses.
assert.strictEqual(certs, tls.getCACertificates('bundled'));
