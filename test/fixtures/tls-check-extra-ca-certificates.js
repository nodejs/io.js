'use strict';

const tls = require('tls');
const assert = require('assert');

const defaultSet = new Set(tls.getCACertificates('default'));
const extraSet = new Set(tls.getCACertificates('extra'));
console.log(defaultSet.size, 'default certificates');
console.log(extraSet.size, 'extra certificates')

// Parent process is supposed to call this with
// NODE_EXTRA_CA_CERTS set to test/fixtures/keys/ca1-cert.pem.
assert.strictEqual(extraSet.size, 1);

// Check that default set is a super set of extra set.
assert.deepStrictEqual(defaultSet.intersection(extraSet),
                       extraSet);
