'use strict';
// Flags: --no-use-openssl-ca
// This tests that tls.getCACertificates() returns the bundled
// certificates correctly.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

const defaultSet = new Set(tls.getCACertificates('default'));
const bundledSet = new Set(tls.getCACertificates('bundled'));

// When --use-openssl-ca is false (i.e. bundled CA is sued),
// default is a superset of bundled certificates.
assert.deepStrictEqual(defaultSet.intersection(bundledSet), bundledSet);
