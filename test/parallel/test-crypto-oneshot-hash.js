'use strict';
// This tests crypto.hash() works.
const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const fixtures = require('../common/fixtures');
const fs = require('fs');

// Test errors for invalid arguments.
[undefined, null, true, 1, () => {}, {}].forEach((invalid) => {
  assert.throws(() => { crypto.hash(invalid, 'test'); }, { code: 'ERR_INVALID_ARG_TYPE' });
});

[undefined, null, true, 1, () => {}, {}].forEach((invalid) => {
  assert.throws(() => { crypto.hash('sha1', invalid); }, { code: 'ERR_INVALID_ARG_TYPE' });
});

[null, true, 1, () => {}, {}].forEach((invalid) => {
  assert.throws(() => { crypto.hash('sha1', 'test', invalid); }, { code: 'ERR_INVALID_ARG_TYPE' });
});

assert.throws(() => { crypto.hash('sha1', 'test', 'not an encoding'); }, { code: 'ERR_INVALID_ARG_VALUE' });

// Test that the output of crypto.hash() is the same as crypto.createHash().
const methods = crypto.getHashes();

const input = fs.readFileSync(fixtures.path('utf8_test_text.txt'));

for (const method of methods) {
  for (const outputEncoding of ['buffer', 'hex', 'base64', undefined]) {
    const oldDigest = crypto.createHash(method).update(input).digest(outputEncoding || 'hex');
    const digestFromBuffer = crypto.hash(method, input, outputEncoding);
    assert.deepStrictEqual(digestFromBuffer, oldDigest,
                           `different result from ${method} with encoding ${outputEncoding}`);
    const digestFromString = crypto.hash(method, input.toString(), outputEncoding);
    assert.deepStrictEqual(digestFromString, oldDigest,
                           `different result from ${method} with encoding ${outputEncoding}`);
  }
}
