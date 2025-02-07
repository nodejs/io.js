'use strict';
const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

const size = crypto.getFips() || hasOpenSSL3 ? 1024 : 256;
const dh1 = crypto.createDiffieHellman(size);
const p1 = dh1.getPrime('buffer');

{
  const DiffieHellman = crypto.DiffieHellman;

  const dh = DiffieHellman(p1, 'buffer');
  assert(dh instanceof DiffieHellman, 'DiffieHellman is expected to return a ' +
                                      'new instance when called without `new`');
}

{
  const DiffieHellmanGroup = crypto.DiffieHellmanGroup;
  const dhg = DiffieHellmanGroup('modp5');
  assert(dhg instanceof DiffieHellmanGroup, 'DiffieHellmanGroup is expected ' +
                                            'to return a new instance when ' +
                                            'called without `new`');
}

{
  const ECDH = crypto.ECDH;
  const ecdh = ECDH('prime256v1');
  assert(ecdh instanceof ECDH, 'ECDH is expected to return a new instance ' +
                              'when called without `new`');
}
