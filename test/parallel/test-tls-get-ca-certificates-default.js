'use strict';

// This tests that tls.getCACertificates() returns the default
// certificates correctly.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');
const { assertIsCAArray } = require('../common/tls');

const certs = tls.getCACertificates();
assertIsCAArray(certs);

const certs2 = tls.getCACertificates('default');
assert.strictEqual(certs, certs2);

// It's cached on subsequent accesses.
assert.strictEqual(certs, tls.getCACertificates('default'));
