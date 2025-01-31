// Flags: --expose-gc --noconcurrent_recompilation
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
if (common.isASan)
  common.skip('ASan messes with memory measurements');

const assert = require('assert');
const crypto = require('crypto');
const { hasOpenSSL3 } = require('../common/crypto');

const before = process.memoryUsage.rss();
{
  const size = crypto.getFips() || hasOpenSSL3 ? 1024 : 256;
  const dh = crypto.createDiffieHellman(size);
  const publicKey = dh.generateKeys();
  const privateKey = dh.getPrivateKey();
  for (let i = 0; i < 5e4; i += 1) {
    dh.setPublicKey(publicKey);
    dh.setPrivateKey(privateKey);
  }
}
globalThis.gc();
const after = process.memoryUsage.rss();

// RSS should stay the same, ceteris paribus, but allow for
// some slop because V8 mallocs memory during execution.
assert(after - before < 10 << 21, `before=${before} after=${after}`);
