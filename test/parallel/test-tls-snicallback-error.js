'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const net = require('net');
const tls = require('tls');

for (const SNICallback of ['fhqwhgads', 42, {}, []]) {
  assert.throws(() => {
    tls.createServer({ SNICallback });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  });

  assert.throws(() => {
    new tls.TLSSocket(new net.Socket(), { isServer: true, SNICallback });
  }, {
    code: 'ERR_INVALID_ARG_TYPE',
    name: 'TypeError',
  });
}
