'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { subtle } = globalThis.crypto;

const vectors = require('../fixtures/crypto/ecdsa')();

async function testVerify({ name,
                            hash,
                            namedCurve,
                            publicKeyBuffer,
                            privateKeyBuffer,
                            signature,
                            plaintext }) {
  const [
    publicKey,
    noVerifyPublicKey,
    privateKey,
    hmacKey,
    rsaKeys,
    okpKeys,
  ] = await Promise.all([
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name, namedCurve },
      false,
      ['verify']),
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name, namedCurve },
      false,
      [ /* No usages */ ]),
    subtle.importKey(
      'pkcs8',
      privateKeyBuffer,
      { name, namedCurve },
      false,
      ['sign']),
    subtle.generateKey(
      { name: 'HMAC', hash: 'SHA-256' },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'RSA-PSS',
        modulusLength: 1024,
        publicExponent: new Uint8Array([1, 0, 1]),
        hash: 'SHA-256',
      },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'Ed25519',
      },
      false,
      ['sign']),
  ]);

  assert(await subtle.verify({ name, hash }, publicKey, signature, plaintext));

  // Test verification with altered buffers
  const copy = Buffer.from(plaintext);
  const sigcopy = Buffer.from(signature);
  const p = subtle.verify({ name, hash }, publicKey, sigcopy, copy);
  copy[0] = 255 - copy[0];
  sigcopy[0] = 255 - sigcopy[0];
  assert(await p);

  // Test failure when using wrong key
  await assert.rejects(
    subtle.verify({ name, hash }, privateKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  await assert.rejects(
    subtle.verify({ name, hash }, noVerifyPublicKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  // Test failure when using the wrong algorithms
  await assert.rejects(
    subtle.verify({ name, hash }, hmacKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  await assert.rejects(
    subtle.verify({ name, hash }, rsaKeys.publicKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  await assert.rejects(
    subtle.verify({ name, hash }, okpKeys.publicKey, signature, plaintext), {
      message: /Unable to use this key to verify/
    });

  // Test failure when signature is altered
  {
    const copy = Buffer.from(signature);
    copy[0] = 255 - copy[0];
    assert(!(await subtle.verify(
      { name, hash },
      publicKey,
      copy,
      plaintext)));
    assert(!(await subtle.verify(
      { name, hash },
      publicKey,
      copy.slice(1),
      plaintext)));
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
    assert(!(await subtle.verify({
      name,
      hash: otherhash
    }, publicKey, signature, copy)));
  }

  await assert.rejects(
    subtle.verify({ name, hash: 'sha256' }, publicKey, signature, copy), {
      message: /Unrecognized algorithm name/,
      name: 'NotSupportedError',
    });
}

async function testSign({ name,
                          hash,
                          namedCurve,
                          publicKeyBuffer,
                          privateKeyBuffer,
                          signature,
                          plaintext }) {
  const [
    publicKey,
    privateKey,
    hmacKey,
    rsaKeys,
    okpKeys,
  ] = await Promise.all([
    subtle.importKey(
      'spki',
      publicKeyBuffer,
      { name, namedCurve },
      false,
      ['verify']),
    subtle.importKey(
      'pkcs8',
      privateKeyBuffer,
      { name, namedCurve },
      false,
      ['sign']),
    subtle.generateKey(
      { name: 'HMAC', hash: 'SHA-256' },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'RSA-PSS',
        modulusLength: 1024,
        publicExponent: new Uint8Array([1, 0, 1]),
        hash: 'SHA-256',
      },
      false,
      ['sign']),
    subtle.generateKey(
      {
        name: 'Ed25519',
      },
      false,
      ['sign']),
  ]);

  {
    const sig = await subtle.sign({ name, hash }, privateKey, plaintext);
    assert.strictEqual(sig.byteLength, signature.byteLength);
    assert(await subtle.verify({ name, hash }, publicKey, sig, plaintext));
  }

  {
    const copy = Buffer.from(plaintext);
    const p = subtle.sign({ name, hash }, privateKey, copy);
    copy[0] = 255 - copy[0];
    const sig = await p;
    assert(await subtle.verify({ name, hash }, publicKey, sig, plaintext));
  }

  // Test failure when using wrong key
  await assert.rejects(
    subtle.sign({ name, hash }, publicKey, plaintext), {
      message: /Unable to use this key to sign/
    });

  // Test failure when using the wrong algorithms
  await assert.rejects(
    subtle.sign({ name, hash }, hmacKey, plaintext), {
      message: /Unable to use this key to sign/
    });

  await assert.rejects(
    subtle.sign({ name, hash }, rsaKeys.privateKey, plaintext), {
      message: /Unable to use this key to sign/
    });

  await assert.rejects(
    subtle.sign({ name, hash }, okpKeys.privateKey, plaintext), {
      message: /Unable to use this key to sign/
    });
}

(async function() {
  const variations = [];

  for (let i = 0; i < vectors.length; ++i) {
    const vector = vectors[i];
    variations.push(testVerify(vector));
    variations.push(testSign(vector));
  }

  await Promise.all(variations);
})().then(common.mustCall());
