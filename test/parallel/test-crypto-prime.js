'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');

const {
  generatePrime,
  generatePrimeSync,
  checkPrime,
  checkPrimeSync,
} = require('crypto');

const { Worker } = require('worker_threads');

const { promisify } = require('util');
const pgeneratePrime = promisify(generatePrime);
const pCheckPrime = promisify(checkPrime);

['hello', false, {}, []].forEach((i) => {
  assert.throws(() => generatePrime(i), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrimeSync(i), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

['hello', false, 123].forEach((i) => {
  assert.throws(() => generatePrime(80, i, common.mustNotCall()), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrimeSync(80, i), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

['hello', false, 123].forEach((i) => {
  assert.throws(() => generatePrime(80, {}), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

[-1, 0, 2 ** 31, 2 ** 31 + 1, 2 ** 32 - 1, 2 ** 32].forEach((size) => {
  assert.throws(() => generatePrime(size, common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE',
    message: />= 1 && <= 2147483647/
  });
  assert.throws(() => generatePrimeSync(size), {
    code: 'ERR_OUT_OF_RANGE',
    message: />= 1 && <= 2147483647/
  });
});

['test', -1, {}, []].forEach((i) => {
  assert.throws(() => generatePrime(8, { safe: i }, common.mustNotCall()), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrime(8, { rem: i }, common.mustNotCall()), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrime(8, { add: i }, common.mustNotCall()), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrimeSync(8, { safe: i }), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrimeSync(8, { rem: i }), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
  assert.throws(() => generatePrimeSync(8, { add: i }), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

{
  // Negative BigInts should not be converted to 0 silently.

  assert.throws(() => generatePrime(20, { add: -1n }, common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.add" is out of range. It must be >= 0. ' +
             'Received -1n'
  });

  assert.throws(() => generatePrime(20, { rem: -1n }, common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "options.rem" is out of range. It must be >= 0. ' +
             'Received -1n'
  });

  assert.throws(() => checkPrime(-1n, common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE',
    message: 'The value of "candidate" is out of range. It must be >= 0. ' +
             'Received -1n'
  });
}

generatePrime(80, common.mustSucceed((prime) => {
  assert(checkPrimeSync(prime));
  checkPrime(prime, common.mustSucceed((result) => {
    assert(result);
  }));
}));

assert(checkPrimeSync(generatePrimeSync(80)));

generatePrime(80, {}, common.mustSucceed((prime) => {
  assert(checkPrimeSync(prime));
}));

assert(checkPrimeSync(generatePrimeSync(80, {})));

generatePrime(32, { safe: true }, common.mustSucceed((prime) => {
  assert(checkPrimeSync(prime));
  const buf = Buffer.from(prime);
  const val = buf.readUInt32BE();
  const check = (val - 1) / 2;
  buf.writeUInt32BE(check);
  assert(checkPrimeSync(buf));
}));

{
  const prime = generatePrimeSync(32, { safe: true });
  assert(checkPrimeSync(prime));
  const buf = Buffer.from(prime);
  const val = buf.readUInt32BE();
  const check = (val - 1) / 2;
  buf.writeUInt32BE(check);
  assert(checkPrimeSync(buf));
}

const add = 12;
const rem = 11;
const add_buf = Buffer.from([add]);
const rem_buf = Buffer.from([rem]);
generatePrime(
  32,
  { add: add_buf, rem: rem_buf },
  common.mustSucceed((prime) => {
    assert(checkPrimeSync(prime));
    const buf = Buffer.from(prime);
    const val = buf.readUInt32BE();
    assert.strictEqual(val % add, rem);
  }));

{
  const prime = generatePrimeSync(32, { add: add_buf, rem: rem_buf });
  assert(checkPrimeSync(prime));
  const buf = Buffer.from(prime);
  const val = buf.readUInt32BE();
  assert.strictEqual(val % add, rem);
}

{
  const prime = generatePrimeSync(32, { add: BigInt(add), rem: BigInt(rem) });
  assert(checkPrimeSync(prime));
  const buf = Buffer.from(prime);
  const val = buf.readUInt32BE();
  assert.strictEqual(val % add, rem);
}

{
  // The behavior when specifying only add without rem should depend on the
  // safe option.

  if (process.versions.openssl >= '1.1.1f') {
    generatePrime(128, {
      bigint: true,
      add: 5n
    }, common.mustSucceed((prime) => {
      assert(checkPrimeSync(prime));
      assert.strictEqual(prime % 5n, 1n);
    }));

    generatePrime(128, {
      bigint: true,
      safe: true,
      add: 5n
    }, common.mustSucceed((prime) => {
      assert(checkPrimeSync(prime));
      assert.strictEqual(prime % 5n, 3n);
    }));
  }
}

{
  // This is impossible because it implies (prime % 2**64) == 1 and
  // prime < 2**64, meaning prime = 1, but 1 is not prime.
  for (const add of [2n ** 64n, 2n ** 65n]) {
    assert.throws(() => {
      generatePrimeSync(64, { add });
    }, {
      code: 'ERR_OUT_OF_RANGE',
      message: 'invalid options.add'
    });
  }

  // Any parameters with rem >= add lead to an impossible condition.
  for (const rem of [7n, 8n, 3000n]) {
    assert.throws(() => {
      generatePrimeSync(64, { add: 7n, rem });
    }, {
      code: 'ERR_OUT_OF_RANGE',
      message: 'invalid options.rem'
    });
  }

  // This is possible, but not allowed. It implies prime == 7, which means that
  // we did not actually generate a random prime.
  assert.throws(() => {
    generatePrimeSync(3, { add: 8n, rem: 7n });
  }, {
    code: 'ERR_OUT_OF_RANGE'
  });

  if (process.versions.openssl >= '1.1.1f') {
    // This is possible and allowed (but makes little sense).
    assert.strictEqual(generatePrimeSync(4, {
      add: 15n,
      rem: 13n,
      bigint: true
    }), 13n);
  }
}

[1, 'hello', {}, []].forEach((i) => {
  assert.throws(() => checkPrime(i), {
    code: 'ERR_INVALID_ARG_TYPE'
  });
});

for (const checks of ['hello', {}, []]) {
  assert.throws(() => checkPrime(2n, { checks }, common.mustNotCall()), {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /checks/
  });
  assert.throws(() => checkPrimeSync(2n, { checks }), {
    code: 'ERR_INVALID_ARG_TYPE',
    message: /checks/
  });
}

for (const checks of [-(2 ** 31), -1, 2 ** 31, 2 ** 32 - 1, 2 ** 32, 2 ** 50]) {
  assert.throws(() => checkPrime(2n, { checks }, common.mustNotCall()), {
    code: 'ERR_OUT_OF_RANGE',
    message: /<= 2147483647/
  });
  assert.throws(() => checkPrimeSync(2n, { checks }), {
    code: 'ERR_OUT_OF_RANGE',
    message: /<= 2147483647/
  });
}

{
  const bytes = Buffer.alloc(67108864);
  bytes[0] = 0x1;
  assert.throws(() => checkPrime(bytes, common.mustNotCall()), {
    code: 'ERR_OSSL_BN_BIGNUM_TOO_LONG',
    message: /bignum too long/
  });
  assert.throws(() => checkPrimeSync(bytes), {
    code: 'ERR_OSSL_BN_BIGNUM_TOO_LONG',
    message: /bignum too long/
  });
}

assert(!checkPrimeSync(Buffer.from([0x1])));
assert(checkPrimeSync(Buffer.from([0x2])));
assert(checkPrimeSync(Buffer.from([0x3])));
assert(!checkPrimeSync(Buffer.from([0x4])));

assert(
  !checkPrimeSync(
    Buffer.from([0x1]),
    {
      fast: true,
      trialDivision: true,
      checks: 10
    }));

(async function() {
  const prime = await pgeneratePrime(36);
  assert(await pCheckPrime(prime));
})().then(common.mustCall());

assert.throws(() => {
  generatePrimeSync(32, { bigint: '' });
}, { code: 'ERR_INVALID_ARG_TYPE' });

assert.throws(() => {
  generatePrime(32, { bigint: '' }, common.mustNotCall());
}, { code: 'ERR_INVALID_ARG_TYPE' });

{
  const prime = generatePrimeSync(3, { bigint: true });
  assert.strictEqual(typeof prime, 'bigint');
  assert.strictEqual(prime, 7n);
  assert(checkPrimeSync(prime));
  checkPrime(prime, common.mustSucceed(assert));
}

{
  generatePrime(3, { bigint: true }, common.mustSucceed((prime) => {
    assert.strictEqual(typeof prime, 'bigint');
    assert.strictEqual(prime, 7n);
    assert(checkPrimeSync(prime));
    checkPrime(prime, common.mustSucceed(assert));
  }));
}

{
  // Verify that generatePrime can be reasonably interrupted.
  const worker = new Worker(`
    const { generatePrime } = require('crypto');
    generatePrime(2048, () => {
      throw new Error('should not be called');
    });
    process.exit(42);
  `, { eval: true });

  worker.on('error', common.mustNotCall());
  worker.on('exit', common.mustCall((exitCode) => assert.strictEqual(exitCode, 42)));
}
