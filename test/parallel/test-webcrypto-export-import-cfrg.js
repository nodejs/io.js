'use strict';

const common = require('../common');
const fixtures = require('../common/fixtures');

if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const crypto = require('crypto');
const { subtle } = globalThis.crypto;

const keyData = {
  'Ed25519': {
    jwsAlg: 'EdDSA',
    spki: Buffer.from(
      '302a300506032b6570032100a054b618c12b26c8d43595a5c38dd2b0140b944a' +
      '151f75003278c2b6c58ec08f', 'hex'),
    pkcs8: Buffer.from(
      '302e020100300506032b657004220420d53150bdcd17b4d4b21ae756d4965639' +
      'd75b28f56ff9111b1f88326913e445bc', 'hex'),
    jwk: {
      kty: 'OKP',
      crv: 'Ed25519',
      x: 'oFS2GMErJsjUNZWlw43SsBQLlEoVH3UAMnjCtsWOwI8',
      d: '1TFQvc0XtNSyGudW1JZWOddbKPVv-REbH4gyaRPkRbw'
    }
  },
  'Ed448': {
    jwsAlg: 'EdDSA',
    spki: Buffer.from(
      '3043300506032b6571033a0008cc38160c85bca5656ac4924af7ea97a9161b20' +
      '2528273dcb84afd2eeb99ac912a401b34ef15ef4d9486406a6eecc31e5909219' +
      'bd54866800', 'hex'),
    pkcs8: Buffer.from(
      '3047020100300506032b6571043b0439afd05b2fbb153b47c18dfa66baaed0de' +
      'fb4e88c651487cdee0fafc40fa3d048fe1cd145a44143243c0468166b5bc161a' +
      '82e3b904f3e2fcaaf9', 'hex'),
    jwk: {
      kty: 'OKP',
      crv: 'Ed448',
      x: 'CMw4FgyFvKVlasSSSvfql6kWGyAlKCc9y4Sv0u65mskSpAGzTvFe9NlIZAam7' +
         'swx5ZCSGb1UhmgA',
      d: 'r9BbL7sVO0fBjfpmuq7Q3vtOiMZRSHze4Pr8QPo9BI_hzRRaRBQyQ8BGgWa1v' +
         'BYaguO5BPPi_Kr5'
    }
  },
  'X25519': {
    jwsAlg: 'ECDH-ES',
    spki: Buffer.from(
      '302a300506032b656e032100f38d9f4e621a44e0428176a4c8a534b34f07f8db' +
      '30152f9ca0167aabf598fe65', 'hex'),
    pkcs8: Buffer.from(
      '302e020100300506032b656e04220420a8327850317b4b03a5a8b4e923413b1d' +
      'a4a642e0d6f7a72cf4d16a549e628a5f', 'hex'),
    jwk: {
      kty: 'OKP',
      crv: 'X25519',
      x: '842fTmIaROBCgXakyKU0s08H-NswFS-coBZ6q_WY_mU',
      d: 'qDJ4UDF7SwOlqLTpI0E7HaSmQuDW96cs9NFqVJ5iil8'
    }
  },
  'X448': {
    jwsAlg: 'ECDH-ES',
    spki: Buffer.from(
      '3042300506032b656f0339001d451c8c0c369a42eadfc2875cd44953caeb46c4' +
      '66dc86568280bfdbbb01f4709a1b0b1e0dd66cf7b11c84119ddc98890db72891' +
      '29e30da4', 'hex'),
    pkcs8: Buffer.from(
      '3046020100300506032b656f043a0438fc818f6546a81f963c27765dc1c05bfd' +
      'b169667e5e0cf45318ed1cb93872217ab0d9004e0c7dd0dcb00192f72039cc1a' +
      '1dff750ec31c8afb', 'hex'),
    jwk: {
      kty: 'OKP',
      crv: 'X448',
      x: 'HUUcjAw2mkLq38KHXNRJU8rrRsRm3IZWgoC_27sB9HCaGwseDdZs97EchBGd3' +
         'JiJDbcokSnjDaQ',
      d: '_IGPZUaoH5Y8J3ZdwcBb_bFpZn5eDPRTGO0cuThyIXqw2QBODH3Q3LABkvcgO' +
         'cwaHf91DsMcivs'
    }
  }
};

const testVectors = [
  {
    name: 'Ed25519',
    privateUsages: ['sign'],
    publicUsages: ['verify']
  },
  {
    name: 'Ed448',
    privateUsages: ['sign'],
    publicUsages: ['verify']
  },
  {
    name: 'X25519',
    privateUsages: ['deriveKey', 'deriveBits'],
    publicUsages: []
  },
  {
    name: 'X448',
    privateUsages: ['deriveKey', 'deriveBits'],
    publicUsages: []
  },
];

async function testImportSpki({ name, publicUsages }, extractable) {
  const key = await subtle.importKey(
    'spki',
    keyData[name].spki,
    { name },
    extractable,
    publicUsages);
  assert.strictEqual(key.type, 'public');
  assert.strictEqual(key.extractable, extractable);
  assert.deepStrictEqual(key.usages, publicUsages);
  assert.deepStrictEqual(key.algorithm.name, name);

  if (extractable) {
    // Test the roundtrip
    const spki = await subtle.exportKey('spki', key);
    assert.strictEqual(
      Buffer.from(spki).toString('hex'),
      keyData[name].spki.toString('hex'));
  } else {
    await assert.rejects(
      subtle.exportKey('spki', key), {
        message: /key is not extractable/
      });
  }

  // Bad usage
  await assert.rejects(
    subtle.importKey(
      'spki',
      keyData[name].spki,
      { name },
      extractable,
      ['wrapKey']),
    { message: /Unsupported key usage/ });
}

async function testImportPkcs8({ name, privateUsages }, extractable) {
  const key = await subtle.importKey(
    'pkcs8',
    keyData[name].pkcs8,
    { name },
    extractable,
    privateUsages);
  assert.strictEqual(key.type, 'private');
  assert.strictEqual(key.extractable, extractable);
  assert.deepStrictEqual(key.usages, privateUsages);
  assert.deepStrictEqual(key.algorithm.name, name);

  if (extractable) {
    // Test the roundtrip
    const pkcs8 = await subtle.exportKey('pkcs8', key);
    assert.strictEqual(
      Buffer.from(pkcs8).toString('hex'),
      keyData[name].pkcs8.toString('hex'));
  } else {
    await assert.rejects(
      subtle.exportKey('pkcs8', key), {
        message: /key is not extractable/
      });
  }

  await assert.rejects(
    subtle.importKey(
      'pkcs8',
      keyData[name].pkcs8,
      { name },
      extractable,
      [/* empty usages */]),
    { name: 'SyntaxError', message: 'Usages cannot be empty when importing a private key.' });
}

async function testImportJwk({ name, publicUsages, privateUsages }, extractable) {

  const jwk = keyData[name].jwk;

  const [
    publicKey,
    privateKey,
  ] = await Promise.all([
    subtle.importKey(
      'jwk',
      {
        kty: jwk.kty,
        crv: jwk.crv,
        x: jwk.x,
      },
      { name },
      extractable, publicUsages),
    subtle.importKey(
      'jwk',
      jwk,
      { name },
      extractable,
      privateUsages),
    subtle.importKey(
      'jwk',
      {
        alg: keyData[name].jwsAlg,
        kty: jwk.kty,
        crv: jwk.crv,
        x: jwk.x,
      },
      { name },
      extractable, publicUsages),
    subtle.importKey(
      'jwk',
      {
        ...jwk,
        alg: keyData[name].jwsAlg,
      },
      { name },
      extractable,
      privateUsages),
  ]);

  assert.strictEqual(publicKey.type, 'public');
  assert.strictEqual(privateKey.type, 'private');
  assert.strictEqual(publicKey.extractable, extractable);
  assert.strictEqual(privateKey.extractable, extractable);
  assert.deepStrictEqual(publicKey.usages, publicUsages);
  assert.deepStrictEqual(privateKey.usages, privateUsages);
  assert.strictEqual(publicKey.algorithm.name, name);
  assert.strictEqual(privateKey.algorithm.name, name);

  if (extractable) {
    // Test the round trip
    const [
      pubJwk,
      pvtJwk,
    ] = await Promise.all([
      subtle.exportKey('jwk', publicKey),
      subtle.exportKey('jwk', privateKey),
    ]);

    assert.deepStrictEqual(pubJwk.key_ops, publicUsages);
    assert.strictEqual(pubJwk.ext, true);
    assert.strictEqual(pubJwk.kty, 'OKP');
    assert.strictEqual(pubJwk.x, jwk.x);
    assert.strictEqual(pubJwk.crv, jwk.crv);

    assert.deepStrictEqual(pvtJwk.key_ops, privateUsages);
    assert.strictEqual(pvtJwk.ext, true);
    assert.strictEqual(pvtJwk.kty, 'OKP');
    assert.strictEqual(pvtJwk.x, jwk.x);
    assert.strictEqual(pvtJwk.crv, jwk.crv);
    assert.strictEqual(pvtJwk.d, jwk.d);

    assert.strictEqual(pubJwk.alg, undefined);
    assert.strictEqual(pvtJwk.alg, undefined);
  } else {
    await assert.rejects(
      subtle.exportKey('jwk', publicKey), {
        message: /key is not extractable/
      });
    await assert.rejects(
      subtle.exportKey('jwk', privateKey), {
        message: /key is not extractable/
      });
  }

  {
    const invalidUse = name.startsWith('X') ? 'sig' : 'enc';
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { ...jwk, use: invalidUse },
        { name },
        extractable,
        privateUsages),
      { message: 'Invalid JWK "use" Parameter' });
  }

  // The JWK alg member is ignored
  // https://github.com/WICG/webcrypto-secure-curves/pull/24
  if (name.startsWith('Ed')) {
    await subtle.importKey(
      'jwk',
      { kty: jwk.kty, x: jwk.x, crv: jwk.crv, alg: 'foo' },
      { name },
      extractable,
      publicUsages);

    await subtle.importKey(
      'jwk',
      { ...jwk, alg: 'foo' },
      { name },
      extractable,
      privateUsages);
  }

  for (const crv of [undefined, name === 'Ed25519' ? 'Ed448' : 'Ed25519']) {
    await assert.rejects(
      subtle.importKey(
        'jwk',
        { kty: jwk.kty, x: jwk.x, y: jwk.y, crv },
        { name },
        extractable,
        publicUsages),
      { message: 'JWK "crv" Parameter and algorithm name mismatch' });

    await assert.rejects(
      subtle.importKey(
        'jwk',
        { ...jwk, crv },
        { name },
        extractable,
        privateUsages),
      { message: 'JWK "crv" Parameter and algorithm name mismatch' });
  }

  await assert.rejects(
    subtle.importKey(
      'jwk',
      { ...jwk },
      { name },
      extractable,
      [/* empty usages */]),
    { name: 'SyntaxError', message: 'Usages cannot be empty when importing a private key.' });
}

async function testImportRaw({ name, publicUsages }) {
  const jwk = keyData[name].jwk;

  const publicKey = await subtle.importKey(
    'raw',
    Buffer.from(jwk.x, 'base64url'),
    { name },
    true, publicUsages);

  assert.strictEqual(publicKey.type, 'public');
  assert.deepStrictEqual(publicKey.usages, publicUsages);
  assert.strictEqual(publicKey.algorithm.name, name);
}

(async function() {
  const tests = [];
  for (const vector of testVectors) {
    for (const extractable of [true, false]) {
      tests.push(testImportSpki(vector, extractable));
      tests.push(testImportPkcs8(vector, extractable));
      tests.push(testImportJwk(vector, extractable));
    }
    tests.push(testImportRaw(vector));
  }
  await Promise.all(tests);
})().then(common.mustCall());

{
  const rsaPublic = crypto.createPublicKey(
    fixtures.readKey('rsa_public_2048.pem'));
  const rsaPrivate = crypto.createPrivateKey(
    fixtures.readKey('rsa_private_2048.pem'));

  for (const [name, publicUsages, privateUsages] of [
    ['Ed25519', ['verify'], ['sign']],
    ['X448', [], ['deriveBits']],
  ]) {
    assert.rejects(subtle.importKey(
      'spki',
      rsaPublic.export({ format: 'der', type: 'spki' }),
      { name },
      true, publicUsages), { message: /Invalid key type/ }).then(common.mustCall());
    assert.rejects(subtle.importKey(
      'pkcs8',
      rsaPrivate.export({ format: 'der', type: 'pkcs8' }),
      { name },
      true, privateUsages), { message: /Invalid key type/ }).then(common.mustCall());
  }
}
