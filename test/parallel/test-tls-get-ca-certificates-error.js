'use strict';

// This tests that tls.getCACertificates() throws error when being
// passed an invalid argument.

const common = require('../common');
if (!common.hasCrypto) common.skip('missing crypto');

const assert = require('assert');
const tls = require('tls');

for (const invalid of [1, null, () => {}, true]) {
  assert.throws(() => tls.getCACertificates(invalid), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
}

assert.throws(() => tls.getCACertificates('test'), {
  code: 'ERR_INVALID_ARG_VALUE'
});
