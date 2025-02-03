'use strict';

// This tests that the crypto error stack can be correctly converted.
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

assert.throws(() => {
  tls.createSecureContext({ clientCertEngine: 'x' });
}, (err) => {
  return err.name === 'Error' &&
         /could not load the shared library/.test(err.message) &&
         Array.isArray(err.opensslErrorStack) &&
        err.opensslErrorStack.length > 0;
});
