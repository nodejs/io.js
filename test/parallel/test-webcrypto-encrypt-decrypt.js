'use strict';

const common = require('../common');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const { subtle } = globalThis.crypto;

// This is only a partial test. The WebCrypto Web Platform Tests
// will provide much greater coverage.

// Test Encrypt/Decrypt RSA-OAEP
{
  const buf = globalThis.crypto.getRandomValues(new Uint8Array(50));

  async function test() {
    const ec = new TextEncoder();
    const { publicKey, privateKey } = await subtle.generateKey({
      name: 'RSA-OAEP',
      modulusLength: 2048,
      publicExponent: new Uint8Array([1, 0, 1]),
      hash: 'SHA-384',
    }, true, ['encrypt', 'decrypt']);

    const ciphertext = await subtle.encrypt({
      name: 'RSA-OAEP',
      label: ec.encode('a label')
    }, publicKey, buf);

    const plaintext = await subtle.decrypt({
      name: 'RSA-OAEP',
      label: ec.encode('a label')
    }, privateKey, ciphertext);

    assert.strictEqual(
      Buffer.from(plaintext).toString('hex'),
      Buffer.from(buf).toString('hex'));

    await assert.rejects(() => subtle.encrypt({
      name: 'RSA-OAEP',
    }, privateKey, buf), {
      name: 'InvalidAccessError',
      message: 'The requested operation is not valid for the provided key'
    });

    await assert.rejects(() => subtle.decrypt({
      name: 'RSA-OAEP',
    }, publicKey, ciphertext), {
      name: 'InvalidAccessError',
      message: 'The requested operation is not valid for the provided key'
    });
  }

  test().then(common.mustCall());
}

// Test Encrypt/Decrypt AES-CTR
{
  const buf = globalThis.crypto.getRandomValues(new Uint8Array(50));
  const counter = globalThis.crypto.getRandomValues(new Uint8Array(16));

  async function test() {
    const key = await subtle.generateKey({
      name: 'AES-CTR',
      length: 256
    }, true, ['encrypt', 'decrypt']);

    const ciphertext = await subtle.encrypt(
      { name: 'AES-CTR', counter, length: 64 }, key, buf,
    );

    const plaintext = await subtle.decrypt(
      { name: 'AES-CTR', counter, length: 64 }, key, ciphertext,
    );

    assert.strictEqual(
      Buffer.from(plaintext).toString('hex'),
      Buffer.from(buf).toString('hex'));
  }

  test().then(common.mustCall());
}

// Test Encrypt/Decrypt AES-CBC
{
  const buf = globalThis.crypto.getRandomValues(new Uint8Array(50));
  const iv = globalThis.crypto.getRandomValues(new Uint8Array(16));

  async function test() {
    const key = await subtle.generateKey({
      name: 'AES-CBC',
      length: 256
    }, true, ['encrypt', 'decrypt']);

    const ciphertext = await subtle.encrypt(
      { name: 'AES-CBC', iv }, key, buf,
    );

    const plaintext = await subtle.decrypt(
      { name: 'AES-CBC', iv }, key, ciphertext,
    );

    assert.strictEqual(
      Buffer.from(plaintext).toString('hex'),
      Buffer.from(buf).toString('hex'));
  }

  test().then(common.mustCall());
}

// Test Encrypt/Decrypt AES-GCM
{
  const buf = globalThis.crypto.getRandomValues(new Uint8Array(50));
  const iv = globalThis.crypto.getRandomValues(new Uint8Array(12));

  async function test() {
    const key = await subtle.generateKey({
      name: 'AES-GCM',
      length: 256
    }, true, ['encrypt', 'decrypt']);

    const ciphertext = await subtle.encrypt(
      { name: 'AES-GCM', iv }, key, buf,
    );

    const plaintext = await subtle.decrypt(
      { name: 'AES-GCM', iv }, key, ciphertext,
    );

    assert.strictEqual(
      Buffer.from(plaintext).toString('hex'),
      Buffer.from(buf).toString('hex'));
  }

  test().then(common.mustCall());
}
