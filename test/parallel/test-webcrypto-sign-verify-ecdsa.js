'use strict';

const common = require('../common');

const { test } = require('node:test');
const { subtle } = globalThis.crypto;
const assert = require('node:assert');

if (!common.hasCrypto) common.skip('missing crypto');

const vectors = require('../fixtures/crypto/ecdsa')();

// Test function for verification
async function testVerify({
  name,
  hash,
  namedCurve,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  plaintext,
}) {
  const [publicKey, noVerifyPublicKey, privateKey, hmacKey, rsaKeys, okpKeys] =
    await Promise.all([
      subtle.importKey('spki', publicKeyBuffer, { name, namedCurve }, false, [
        'verify',
      ]),
      subtle.importKey(
        'spki',
        publicKeyBuffer,
        { name, namedCurve },
        false,
        [],
      ), // No usages for this key
      subtle.importKey('pkcs8', privateKeyBuffer, { name, namedCurve }, false, [
        'sign',
      ]),
      subtle.generateKey({ name: 'HMAC', hash: 'SHA-256' }, false, ['sign']),
      subtle.generateKey(
        {
          name: 'RSA-PSS',
          modulusLength: 1024,
          publicExponent: new Uint8Array([1, 0, 1]),
          hash: 'SHA-256',
        },
        false,
        ['sign'],
      ),
      subtle.generateKey({ name: 'Ed25519' }, false, ['sign']),
    ]);

  // Test valid verification
  assert(await subtle.verify({ name, hash }, publicKey, signature, plaintext));

  // Test verification with altered buffers
  const copy = Buffer.from(plaintext);
  const sigcopy = Buffer.from(signature);
  const p = subtle.verify({ name, hash }, publicKey, sigcopy, copy);
  copy[0] = 255 - copy[0];
  sigcopy[0] = 255 - sigcopy[0];
  assert(await p);

  // Test failure with wrong key or algorithm
  await assert.rejects(
    subtle.verify({ name, hash }, privateKey, signature, plaintext),
    {
      message: /Unable to use this key to verify/,
    },
  );

  await assert.rejects(
    subtle.verify({ name, hash }, noVerifyPublicKey, signature, plaintext),
    {
      message: /Unable to use this key to verify/,
    },
  );

  // Test failure when using the wrong algorithms
  await assert.rejects(
    subtle.verify({ name, hash }, hmacKey, signature, plaintext),
    {
      message: /Unable to use this key to verify/,
    },
  );

  await assert.rejects(
    subtle.verify({ name, hash }, rsaKeys.publicKey, signature, plaintext),
    {
      message: /Unable to use this key to verify/,
    },
  );

  await assert.rejects(
    subtle.verify({ name, hash }, okpKeys.publicKey, signature, plaintext),
    {
      message: /Unable to use this key to verify/,
    },
  );

  // Test failure when signature is altered
  {
    const copy = Buffer.from(signature);
    copy[0] = 255 - copy[0];
    assert(!(await subtle.verify({ name, hash }, publicKey, copy, plaintext)));
    assert(
      !(await subtle.verify(
        { name, hash },
        publicKey,
        copy.slice(1),
        plaintext,
      )),
    );
  }

  // Test failure when data is altered
  {
    const copy = Buffer.from(plaintext);
    copy[0] = 255 - copy[0];
    assert(!(await subtle.verify({ name, hash }, publicKey, signature, copy)));
  }

  // Test failure when wrong hash is used
  {
    const otherhash = hash === 'SHA-1' ? 'SHA-256' : 'SHA-1';
    assert(
      !(await subtle.verify(
        { name, hash: otherhash },
        publicKey,
        signature,
        copy,
      )),
    );
  }

  await assert.rejects(
    subtle.verify({ name, hash: 'sha256' }, publicKey, signature, copy),
    {
      message: /Unrecognized algorithm name/,
      name: 'NotSupportedError',
    },
  );
}

// Test function for signing
async function testSign({
  name,
  hash,
  namedCurve,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  plaintext,
}) {
  const [publicKey, privateKey, hmacKey, rsaKeys, okpKeys] = await Promise.all([
    subtle.importKey('spki', publicKeyBuffer, { name, namedCurve }, false, [
      'verify',
    ]),
    subtle.importKey('pkcs8', privateKeyBuffer, { name, namedCurve }, false, [
      'sign',
    ]),
    subtle.generateKey({ name: 'HMAC', hash: 'SHA-256' }, false, ['sign']),
    subtle.generateKey(
      {
        name: 'RSA-PSS',
        modulusLength: 1024,
        publicExponent: new Uint8Array([1, 0, 1]),
        hash: 'SHA-256',
      },
      false,
      ['sign'],
    ),
    subtle.generateKey({ name: 'Ed25519' }, false, ['sign']),
  ]);

  {
    const sig = await subtle.sign({ name, hash }, privateKey, plaintext);
    assert.strictEqual(sig.byteLength, signature.byteLength);
    assert(await subtle.verify({ name, hash }, publicKey, sig, plaintext));
  }

  {
    const copy = Buffer.from(plaintext);
    const sig = await subtle.sign({ name, hash }, privateKey, copy);
    copy[0] = 255 - copy[0];
    assert(await subtle.verify({ name, hash }, publicKey, sig, plaintext));
  }

  // Test failure with wrong key or algorithm
  await assert.rejects(subtle.sign({ name, hash }, publicKey, plaintext), {
    message: /Unable to use this key to sign/,
  });

  // Test failure when using the wrong algorithms
  await assert.rejects(subtle.sign({ name, hash }, hmacKey, plaintext), {
    message: /Unable to use this key to sign/,
  });

  await assert.rejects(
    subtle.sign({ name, hash }, rsaKeys.privateKey, plaintext),
    {
      message: /Unable to use this key to sign/,
    },
  );

  await assert.rejects(
    subtle.sign({ name, hash }, okpKeys.privateKey, plaintext),
    {
      message: /Unable to use this key to sign/,
    },
  );
}

// Use the `node:test` module to create test cases
vectors.forEach((vector) => {
  test(`Verify: ${vector.name}`, async () => {
    await testVerify(vector);
  });

  test(`Sign: ${vector.name}`, async () => {
    await testSign(vector);
  });
});

// Special case for Ed25519 (if necessary)
test('Ed25519 context', async () => {
  const vector = vectors.find(({ name }) => name === 'Ed25519');

  // If Ed25519 vector is missing, skip the test gracefully
  if (!vector) {
    console.warn('Ed25519 vector not found, skipping Ed25519 tests');
    return; // Skip this test
  }

  const { privateKeyBuffer, publicKeyBuffer, signature, plaintext } = vector;

  if (!privateKeyBuffer || !publicKeyBuffer || !signature || !plaintext) {
    throw new Error('Missing data for Ed25519 vector');
  }

  const [privateKey, publicKey] = await Promise.all([
    subtle.importKey('pkcs8', privateKeyBuffer, { name: 'Ed25519' }, false, [
      'sign',
    ]),
    subtle.importKey('spki', publicKeyBuffer, { name: 'Ed25519' }, false, [
      'verify',
    ]),
  ]);

  const sig = await subtle.sign(
    { name: 'Ed25519', context: Buffer.alloc(0) },
    privateKey,
    plaintext,
  );
  assert.deepStrictEqual(Buffer.from(sig), signature);
  assert.strictEqual(
    await subtle.verify(
      { name: 'Ed25519', context: Buffer.alloc(0) },
      publicKey,
      sig,
      plaintext,
    ),
    true,
  );

  await assert.rejects(
    subtle.sign(
      { name: 'Ed25519', context: Buffer.alloc(1) },
      privateKey,
      plaintext,
    ),
    {
      message: /Non zero-length context is not yet supported/,
    },
  );

  await assert.rejects(
    subtle.verify(
      { name: 'Ed25519', context: Buffer.alloc(1) },
      publicKey,
      sig,
      plaintext,
    ),
    {
      message: /Non zero-length context is not yet supported/,
    },
  );
});
