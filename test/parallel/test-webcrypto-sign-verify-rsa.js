'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { subtle } = globalThis.crypto;

const rsa_pkcs = require('../fixtures/crypto/rsa_pkcs');
const rsa_pss = require('../fixtures/crypto/rsa_pss');

async function testVerify({
  algorithm,
  hash,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  plaintext,
}) {
  const [
    publicKey,
    noVerifyPublicKey,
    privateKey,
    hmacKey,
    ecdsaKeys,
  ] = await Promise.all([
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name: algorithm.name, hash },
      false,
      ['verify']),
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name: algorithm.name, hash },
      false,
      [ /* No usages */ ]),
    subtle.importKey(
      'pkcs8',
      privateKeyBuffer,
      { name: algorithm.name, hash },
      false,
      ['sign']),
    subtle.generateKey(
      { name: 'HMAC', hash: 'SHA-256' },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'ECDSA',
        namedCurve: 'P-521',
        hash: 'SHA-256',
      },
      false,
      ['sign']),
  ]);

  assert(await subtle.verify(algorithm, publicKey, signature, plaintext));

  // Test verification with altered buffers
  const copy = Buffer.from(plaintext);
  const sigcopy = Buffer.from(signature);
  const p = subtle.verify(algorithm, publicKey, sigcopy, copy);
  copy[0] = 255 - copy[0];
  sigcopy[0] = 255 - sigcopy[0];
  assert(await p);

  // Test failure when using wrong key
  await assert.rejects(
    subtle.verify(algorithm, privateKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  await assert.rejects(
    subtle.verify(algorithm, noVerifyPublicKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  // Test failure when using the wrong algorithms
  await assert.rejects(
    subtle.verify(algorithm, hmacKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  await assert.rejects(
    subtle.verify(algorithm, ecdsaKeys.publicKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  // Test failure when signature is altered
  {
    const copy = Buffer.from(signature);
    copy[0] = 255 - copy[0];
    assert(!(await subtle.verify(algorithm, publicKey, copy, plaintext)));
    assert(!(await subtle.verify(
      algorithm,
      publicKey,
      copy.slice(1),
      plaintext)));
  }

  // Test failure when data is altered
  {
    const copy = Buffer.from(plaintext);
    copy[0] = 255 - copy[0];
    assert(!(await subtle.verify(algorithm, publicKey, signature, copy)));
  }

  // Test failure when wrong hash is used
  {
    const otherhash = hash === 'SHA-1' ? 'SHA-256' : 'SHA-1';
    const keyWithOtherHash = await subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name: algorithm.name, hash: otherhash },
      false,
      ['verify']);
    assert(!(await subtle.verify(algorithm, keyWithOtherHash, signature, plaintext)));
  }
}

async function testSign({
  algorithm,
  hash,
  publicKeyBuffer,
  privateKeyBuffer,
  signature,
  plaintext,
}) {
  const [
    publicKey,
    privateKey,
    hmacKey,
    ecdsaKeys,
  ] = await Promise.all([
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name: algorithm.name, hash },
      false,
      ['verify']),
    subtle.importKey(
      'pkcs8',
      privateKeyBuffer,
      { name: algorithm.name, hash },
      false,
      ['sign']),
    subtle.generateKey(
      { name: 'HMAC', hash: 'SHA-256' },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'ECDSA',
        namedCurve: 'P-521',
        hash: 'SHA-256',
      },
      false,
      ['sign']),
  ]);

  {
    const sig = await subtle.sign(algorithm, privateKey, plaintext);
    assert.strictEqual(sig.byteLength, signature.byteLength);
    assert(await subtle.verify(algorithm, publicKey, sig, plaintext));
  }

  {
    const copy = Buffer.from(plaintext);
    const p = subtle.sign(algorithm, privateKey, copy);
    copy[0] = 255 - copy[0];
    const sig = await p;
    assert(await subtle.verify(algorithm, publicKey, sig, plaintext));
  }

  // Test failure when using wrong key
  await assert.rejects(
    subtle.sign(algorithm, publicKey, plaintext), {
      message: /Unable to use this key to sign/
    });

  // Test failure when using the wrong algorithms
  await assert.rejects(
    subtle.sign(algorithm, hmacKey, plaintext), {
      message: /Unable to use this key to sign/
    });

  await assert.rejects(
    subtle.sign(algorithm, ecdsaKeys.privateKey, plaintext), {
      message: /Unable to use this key to sign/
    });
}

async function testSaltLength(keyLength, hash, hLen) {
  const { publicKey, privateKey } = await subtle.generateKey({
    name: 'RSA-PSS',
    modulusLength: keyLength,
    publicExponent: new Uint8Array([1, 0, 1]),
    hash,
  }, false, ['sign', 'verify']);

  const data = Buffer.from('Hello, world!');
  const max = keyLength / 8 - hLen - 2;

  const signature = await subtle.sign({ name: 'RSA-PSS', saltLength: max }, privateKey, data);
  await assert.rejects(
    subtle.sign({ name: 'RSA-PSS', saltLength: max + 1 }, privateKey, data), (err) => {
      assert.strictEqual(err.name, 'OperationError');
      assert.strictEqual(err.cause?.code, 'ERR_OUT_OF_RANGE');
      assert.strictEqual(err.cause?.message, `The value of "algorithm.saltLength" is out of range. It must be >= 0 && <= ${max}. Received ${max + 1}`);
      return true;
    });
  await subtle.verify({ name: 'RSA-PSS', saltLength: max }, publicKey, signature, data);
  await assert.rejects(
    subtle.verify({ name: 'RSA-PSS', saltLength: max + 1 }, publicKey, signature, data), (err) => {
      assert.strictEqual(err.name, 'OperationError');
      assert.strictEqual(err.cause?.code, 'ERR_OUT_OF_RANGE');
      assert.strictEqual(err.cause?.message, `The value of "algorithm.saltLength" is out of range. It must be >= 0 && <= ${max}. Received ${max + 1}`);
      return true;
    });
}

(async function() {
  const variations = [];

  rsa_pkcs().forEach((vector) => {
    variations.push(testVerify(vector));
    variations.push(testSign(vector));
  });
  rsa_pss().forEach((vector) => {
    variations.push(testVerify(vector));
    variations.push(testSign(vector));
  });

  for (const keyLength of [1024, 2048]) {
    for (const [hash, hLen] of [['SHA-1', 20], ['SHA-256', 32], ['SHA-384', 48], ['SHA-512', 64]]) {
      variations.push(testSaltLength(keyLength, hash, hLen));
    }
  }

  await Promise.all(variations);
})().then(common.mustCall());
