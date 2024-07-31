'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  randomUUID,
} = require('crypto');

const last = new Set([
  '00000000-0000-0000-0000-000000000000',
]);

function testMatch(uuid) {
  assert.match(
    uuid,
    /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
}

// Generate a number of UUID's to make sure we're
// not just generating the same value over and over
// and to make sure the batching changes the random
// bytes.
for (let n = 0; n < 130; n++) {
  const uuid = randomUUID();
  assert(!last.has(uuid));
  last.add(uuid);
  assert.strictEqual(typeof uuid, 'string');
  assert.strictEqual(uuid.length, 36);
  testMatch(uuid);

  // Check that version 4 identifier was populated.
  assert.strictEqual(
    Buffer.from(uuid.slice(14, 16), 'hex')[0] & 0x40, 0x40);

  // Check that clock_seq_hi_and_reserved was populated with reserved bits.
  assert.strictEqual(
    Buffer.from(uuid.slice(19, 21), 'hex')[0] & 0b1100_0000, 0b1000_0000);
}

// Test non-buffered UUID's
{
  testMatch(randomUUID({ disableEntropyCache: true }));
  testMatch(randomUUID({ disableEntropyCache: true }));
  testMatch(randomUUID({ disableEntropyCache: true }));
  testMatch(randomUUID({ disableEntropyCache: true }));

  assert.throws(() => randomUUID(1), {
    code: 'ERR_INVALID_ARG_TYPE'
  });

  assert.throws(() => randomUUID({ disableEntropyCache: '' }), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
}
