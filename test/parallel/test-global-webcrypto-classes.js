// Flags: --expose-internals
'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');

const webcrypto = require('internal/crypto/webcrypto');
assert.strictEqual(Crypto, webcrypto.Crypto);
assert.strictEqual(CryptoKey, webcrypto.CryptoKey);
assert.strictEqual(SubtleCrypto, webcrypto.SubtleCrypto);
