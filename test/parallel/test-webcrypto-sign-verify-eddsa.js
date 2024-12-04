'use strict';

const common = require('../common');
const assert = require('node:assert');
const { test } = require('node:test');
const { subtle } = globalThis.crypto;

if (!common.hasCrypto) common.skip('missing crypto');

const vectors = require('../fixtures/crypto/eddsa')();

// Test function for verification
async function testVerify({
  name,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  data,
}) {
  const [publicKey, noVerifyPublicKey, privateKey, hmacKey, rsaKeys, ecKeys] =
    await Promise.all([
      subtle.importKey('spki', publicKeyBuffer, { name }, false, ['verify']),
      subtle.importKey('spki', publicKeyBuffer, { name }, false, []), // No usages for this key
      subtle.importKey('pkcs8', privateKeyBuffer, { name }, false, ['sign']),
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
      subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, false, [
        'sign',
      ]),
    ]);

  // Test valid verification
  assert(await subtle.verify({ name }, publicKey, signature, data));

  // Test verification with altered data or signature
  const copy = Buffer.from(data);
  const sigcopy = Buffer.from(signature);
  const p = subtle.verify({ name }, publicKey, sigcopy, copy);
  copy[0] = 255 - copy[0];
  sigcopy[0] = 255 - sigcopy[0];
  assert(await p);

  // Test failure with wrong key or algorithm
  await assert.rejects(subtle.verify({ name }, privateKey, signature, data), {
    message: /Unable to use this key to verify/,
  });

  await assert.rejects(
    subtle.verify({ name }, noVerifyPublicKey, signature, data),
    {
      message: /Unable to use this key to verify/,
    },
  );

  await assert.rejects(subtle.verify({ name }, hmacKey, signature, data), {
    message: /Unable to use this key to verify/,
  });

  await assert.rejects(
    subtle.verify({ name }, rsaKeys.publicKey, signature, data),
    {
      message: /Unable to use this key to verify/,
    },
  );

  await assert.rejects(
    subtle.verify({ name }, ecKeys.publicKey, signature, data),
    {
      message: /Unable to use this key to verify/,
    },
  );

  // Test failure when signature or data is altered
  const alteredSig = Buffer.from(signature);
  alteredSig[0] = 255 - alteredSig[0];
  assert(!(await subtle.verify({ name }, publicKey, alteredSig, data)));
  assert(
    !(await subtle.verify({ name }, publicKey, alteredSig.slice(1), data)),
  );

  const alteredData = Buffer.from(data);
  alteredData[0] = 255 - alteredData[0];
  assert(!(await subtle.verify({ name }, publicKey, signature, alteredData)));
}

// Test function for signing
async function testSign({
  name,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  data,
}) {
  const [publicKey, privateKey, hmacKey, rsaKeys, ecKeys] = await Promise.all([
    subtle.importKey('spki', publicKeyBuffer, { name }, false, ['verify']),
    subtle.importKey('pkcs8', privateKeyBuffer, { name }, false, ['sign']),
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
    subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, false, ['sign']),
  ]);

  // Test valid signing and verification
  const sig = await subtle.sign({ name }, privateKey, data);
  assert.strictEqual(sig.byteLength, signature.byteLength);
  assert(await subtle.verify({ name }, publicKey, sig, data));

  const modifiedData = Buffer.from(data);
  const sigModified = await subtle.sign({ name }, privateKey, modifiedData);
  modifiedData[0] = 255 - modifiedData[0];
  assert(await subtle.verify({ name }, publicKey, sigModified, data));

  // Test failure with wrong key or algorithm
  await assert.rejects(subtle.sign({ name }, publicKey, data), {
    message: /Unable to use this key to sign/,
  });

  await assert.rejects(subtle.sign({ name }, hmacKey, data), {
    message: /Unable to use this key to sign/,
  });

  await assert.rejects(subtle.sign({ name }, rsaKeys.privateKey, data), {
    message: /Unable to use this key to sign/,
  });

  await assert.rejects(subtle.sign({ name }, ecKeys.privateKey, data), {
    message: /Unable to use this key to sign/,
  });
}

vectors.forEach((vector) => {
  test(`Verify: ${vector.name}`, async () => {
    await testVerify(vector);
  });

  test(`Sign: ${vector.name}`, async () => {
    await testSign(vector);
  });
});

// Special case for Ed448
test('Ed448 context', async () => {
  const vector = vectors.find(({ name }) => name === 'Ed448');
  const [privateKey, publicKey] = await Promise.all([
    subtle.importKey(
      'pkcs8',
      vector.privateKeyBuffer,
      { name: 'Ed448' },
      false,
      ['sign'],
    ),
    subtle.importKey('spki', vector.publicKeyBuffer, { name: 'Ed448' }, false, [
      'verify',
    ]),
  ]);

  const sig = await subtle.sign(
    { name: 'Ed448', context: Buffer.alloc(0) },
    privateKey,
    vector.data,
  );
  assert.deepStrictEqual(Buffer.from(sig), vector.signature);
  assert.strictEqual(
    await subtle.verify(
      { name: 'Ed448', context: Buffer.alloc(0) },
      publicKey,
      sig,
      vector.data,
    ),
    true,
  );

  await assert.rejects(
    subtle.sign(
      { name: 'Ed448', context: Buffer.alloc(1) },
      privateKey,
      vector.data,
    ),
    {
      message: /Non zero-length context is not yet supported/,
    },
  );

  await assert.rejects(
    subtle.verify(
      { name: 'Ed448', context: Buffer.alloc(1) },
      publicKey,
      sig,
      vector.data,
    ),
    {
      message: /Non zero-length context is not yet supported/,
    },
  );
});
