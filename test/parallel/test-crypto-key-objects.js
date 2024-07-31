'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');

const assert = require('assert');
const {
  createCipheriv,
  createDecipheriv,
  createSign,
  createVerify,
  createSecretKey,
  createPublicKey,
  createPrivateKey,
  KeyObject,
  randomBytes,
  publicDecrypt,
  publicEncrypt,
  privateDecrypt,
  privateEncrypt,
  getCurves,
  generateKeySync,
  generateKeyPairSync,
} = require('crypto');

const fixtures = require('../common/fixtures');

const publicPem = fixtures.readKey('rsa_public.pem', 'ascii');
const privatePem = fixtures.readKey('rsa_private.pem', 'ascii');

const publicDsa = fixtures.readKey('dsa_public_1025.pem', 'ascii');
const privateDsa = fixtures.readKey('dsa_private_encrypted_1025.pem',
                                    'ascii');

{
  // Attempting to create a key of a wrong type should throw
  const TYPE = 'wrong_type';

  assert.throws(() => new KeyObject(TYPE), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: `The argument 'type' is invalid. Received '${TYPE}'`
  });
}

{
  // Attempting to create a key with non-object handle should throw
  assert.throws(() => new KeyObject('secret', ''), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message:
      'The "handle" argument must be of type object. Received type ' +
      "string ('')"
  });
}

{
  assert.throws(() => KeyObject.from('invalid_key'), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message:
      'The "key" argument must be an instance of CryptoKey. Received type ' +
      "string ('invalid_key')"
  });
}

{
  const keybuf = randomBytes(32);
  const key = createSecretKey(keybuf);
  assert.strictEqual(key.type, 'secret');
  assert.strictEqual(key.toString(), '[object KeyObject]');
  assert.strictEqual(key.symmetricKeySize, 32);
  assert.strictEqual(key.asymmetricKeyType, undefined);
  assert.strictEqual(key.asymmetricKeyDetails, undefined);

  const exportedKey = key.export();
  assert(keybuf.equals(exportedKey));

  const plaintext = Buffer.from('Hello world', 'utf8');

  const cipher = createCipheriv('aes-256-ecb', key, null);
  const ciphertext = Buffer.concat([
    cipher.update(plaintext), cipher.final(),
  ]);

  const decipher = createDecipheriv('aes-256-ecb', key, null);
  const deciphered = Buffer.concat([
    decipher.update(ciphertext), decipher.final(),
  ]);

  assert(plaintext.equals(deciphered));
}

{
  // Passing an existing public key object to createPublicKey should throw.
  const publicKey = createPublicKey(publicPem);
  assert.throws(() => createPublicKey(publicKey), {
    name: 'TypeError',
    code: 'ERR_CRYPTO_INVALID_KEY_OBJECT_TYPE',
    message: 'Invalid key object type public, expected private.'
  });

  // Constructing a private key from a public key should be impossible, even
  // if the public key was derived from a private key.
  assert.throws(() => createPrivateKey(createPublicKey(privatePem)), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
  });

  // Similarly, passing an existing private key object to createPrivateKey
  // should throw.
  const privateKey = createPrivateKey(privatePem);
  assert.throws(() => createPrivateKey(privateKey), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
  });
}

{
  const jwk = {
    e: 'AQAB',
    n: 't9xYiIonscC3vz_A2ceR7KhZZlDu_5bye53nCVTcKnWd2seY6UAdKersX6njr83Dd5OVe' +
       '1BW_wJvp5EjWTAGYbFswlNmeD44edEGM939B6Lq-_8iBkrTi8mGN4YCytivE24YI0D4XZ' +
       'MPfkLSpab2y_Hy4DjQKBq1ThZ0UBnK-9IhX37Ju_ZoGYSlTIGIhzyaiYBh7wrZBoPczIE' +
       'u6et_kN2VnnbRUtkYTF97ggcv5h-hDpUQjQW0ZgOMcTc8n-RkGpIt0_iM_bTjI3Tz_gsF' +
       'di6hHcpZgbopPL630296iByyigQCPJVzdusFrQN5DeC-zT_nGypQkZanLb4ZspSx9Q',
    d: 'ktnq2LvIMqBj4txP82IEOorIRQGVsw1khbm8A-cEpuEkgM71Yi_0WzupKktucUeevQ5i0' +
       'Yh8w9e1SJiTLDRAlJz66kdky9uejiWWl6zR4dyNZVMFYRM43ijLC-P8rPne9Fz16IqHFW' +
       '5VbJqA1xCBhKmuPMsD71RNxZ4Hrsa7Kt_xglQTYsLbdGIwDmcZihId9VGXRzvmCPsDRf2' +
       'fCkAj7HDeRxpUdEiEDpajADc-PWikra3r3b40tVHKWm8wxJLivOIN7GiYXKQIW6RhZgH-' +
       'Rk45JIRNKxNagxdeXUqqyhnwhbTo1Hite0iBDexN9tgoZk0XmdYWBn6ElXHRZ7VCDQ',
    p: '8UovlB4nrBm7xH-u7XXBMbqxADQm5vaEZxw9eluc-tP7cIAI4sglMIvL_FMpbd2pEeP_B' +
       'kR76NTDzzDuPAZvUGRavgEjy0O9j2NAs_WPK4tZF-vFdunhnSh4EHAF4Ij9kbsUi90NOp' +
       'bGfVqPdOaHqzgHKoR23Cuusk9wFQ2XTV8',
    q: 'wxHdEYT9xrpfrHPqSBQPpO0dWGKJEkrWOb-76rSfuL8wGR4OBNmQdhLuU9zTIh22pog-X' +
       'PnLPAecC-4yu_wtJ2SPCKiKDbJBre0CKPyRfGqzvA3njXwMxXazU4kGs-2Fg-xu_iKbaI' +
       'jxXrclBLhkxhBtySrwAFhxxOk6fFcPLSs',
    dp: 'qS_Mdr5CMRGGMH0bKhPUWEtAixUGZhJaunX5wY71Xoc_Gh4cnO-b7BNJ_-5L8WZog0vr' +
       '6PgiLhrqBaCYm2wjpyoG2o2wDHm-NAlzN_wp3G2EFhrSxdOux-S1c0kpRcyoiAO2n29rN' +
       'Da-jOzwBBcU8ACEPdLOCQl0IEFFJO33tl8',
    dq: 'WAziKpxLKL7LnL4dzDcx8JIPIuwnTxh0plCDdCffyLaT8WJ9lXbXHFTjOvt8WfPrlDP_' +
       'Ylxmfkw5BbGZOP1VLGjZn2DkH9aMiwNmbDXFPdG0G3hzQovx_9fajiRV4DWghLHeT9wzJ' +
       'fZabRRiI0VQR472300AVEeX4vgbrDBn600',
    qi: 'k7czBCT9rHn_PNwCa17hlTy88C4vXkwbz83Oa-aX5L4e5gw5lhcR2ZuZHLb2r6oMt9rl' +
       'D7EIDItSs-u21LOXWPTAlazdnpYUyw_CzogM_PN-qNwMRXn5uXFFhmlP2mVg2EdELTahX' +
       'ch8kWqHaCSX53yvqCtRKu_j76V31TfQZGM',
    kty: 'RSA',
  };
  const publicJwk = { kty: jwk.kty, e: jwk.e, n: jwk.n };

  const publicKey = createPublicKey(publicPem);
  assert.strictEqual(publicKey.type, 'public');
  assert.strictEqual(publicKey.toString(), '[object KeyObject]');
  assert.strictEqual(publicKey.asymmetricKeyType, 'rsa');
  assert.strictEqual(publicKey.symmetricKeySize, undefined);

  const privateKey = createPrivateKey(privatePem);
  assert.strictEqual(privateKey.type, 'private');
  assert.strictEqual(privateKey.toString(), '[object KeyObject]');
  assert.strictEqual(privateKey.asymmetricKeyType, 'rsa');
  assert.strictEqual(privateKey.symmetricKeySize, undefined);

  // It should be possible to derive a public key from a private key.
  const derivedPublicKey = createPublicKey(privateKey);
  assert.strictEqual(derivedPublicKey.type, 'public');
  assert.strictEqual(derivedPublicKey.toString(), '[object KeyObject]');
  assert.strictEqual(derivedPublicKey.asymmetricKeyType, 'rsa');
  assert.strictEqual(derivedPublicKey.symmetricKeySize, undefined);

  const publicKeyFromJwk = createPublicKey({ key: publicJwk, format: 'jwk' });
  assert.strictEqual(publicKeyFromJwk.type, 'public');
  assert.strictEqual(publicKeyFromJwk.toString(), '[object KeyObject]');
  assert.strictEqual(publicKeyFromJwk.asymmetricKeyType, 'rsa');
  assert.strictEqual(publicKeyFromJwk.symmetricKeySize, undefined);

  const privateKeyFromJwk = createPrivateKey({ key: jwk, format: 'jwk' });
  assert.strictEqual(privateKeyFromJwk.type, 'private');
  assert.strictEqual(privateKeyFromJwk.toString(), '[object KeyObject]');
  assert.strictEqual(privateKeyFromJwk.asymmetricKeyType, 'rsa');
  assert.strictEqual(privateKeyFromJwk.symmetricKeySize, undefined);

  // It should also be possible to import an encrypted private key as a public
  // key.
  const decryptedKey = createPublicKey({
    key: privateKey.export({
      type: 'pkcs8',
      format: 'pem',
      passphrase: '123',
      cipher: 'aes-128-cbc'
    }),
    format: 'pem',
    passphrase: '123'
  });
  assert.strictEqual(decryptedKey.type, 'public');
  assert.strictEqual(decryptedKey.asymmetricKeyType, 'rsa');

  // Test exporting with an invalid options object, this should throw.
  for (const opt of [undefined, null, 'foo', 0, NaN]) {
    assert.throws(() => publicKey.export(opt), {
      name: 'TypeError',
      code: 'ERR_INVALID_ARG_TYPE',
      message: /^The "options" argument must be of type object/
    });
  }

  for (const keyObject of [publicKey, derivedPublicKey, publicKeyFromJwk]) {
    assert.deepStrictEqual(
      keyObject.export({ format: 'jwk' }),
      { kty: 'RSA', n: jwk.n, e: jwk.e }
    );
  }

  for (const keyObject of [privateKey, privateKeyFromJwk]) {
    assert.deepStrictEqual(
      keyObject.export({ format: 'jwk' }),
      jwk
    );
  }

  // Exporting the key using JWK should not work since this format does not
  // support key encryption
  assert.throws(() => {
    privateKey.export({ format: 'jwk', passphrase: 'secret' });
  }, {
    message: 'The selected key encoding jwk does not support encryption.',
    code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS'
  });

  const publicDER = publicKey.export({
    format: 'der',
    type: 'pkcs1'
  });

  const privateDER = privateKey.export({
    format: 'der',
    type: 'pkcs1'
  });

  assert(Buffer.isBuffer(publicDER));
  assert(Buffer.isBuffer(privateDER));

  const plaintext = Buffer.from('Hello world', 'utf8');
  const testDecryption = (fn, ciphertexts, decryptionKeys) => {
    for (const ciphertext of ciphertexts) {
      for (const key of decryptionKeys) {
        const deciphered = fn(key, ciphertext);
        assert.deepStrictEqual(deciphered, plaintext);
      }
    }
  };

  testDecryption(privateDecrypt, [
    // Encrypt using the public key.
    publicEncrypt(publicKey, plaintext),
    publicEncrypt({ key: publicKey }, plaintext),
    publicEncrypt({ key: publicJwk, format: 'jwk' }, plaintext),

    // Encrypt using the private key.
    publicEncrypt(privateKey, plaintext),
    publicEncrypt({ key: privateKey }, plaintext),
    publicEncrypt({ key: jwk, format: 'jwk' }, plaintext),

    // Encrypt using a public key derived from the private key.
    publicEncrypt(derivedPublicKey, plaintext),
    publicEncrypt({ key: derivedPublicKey }, plaintext),

    // Test distinguishing PKCS#1 public and private keys based on the
    // DER-encoded data only.
    publicEncrypt({ format: 'der', type: 'pkcs1', key: publicDER }, plaintext),
    publicEncrypt({ format: 'der', type: 'pkcs1', key: privateDER }, plaintext),
  ], [
    privateKey,
    { format: 'pem', key: privatePem },
    { format: 'der', type: 'pkcs1', key: privateDER },
    { key: jwk, format: 'jwk' },
  ]);

  testDecryption(publicDecrypt, [
    privateEncrypt(privateKey, plaintext),
  ], [
    // Decrypt using the public key.
    publicKey,
    { format: 'pem', key: publicPem },
    { format: 'der', type: 'pkcs1', key: publicDER },
    { key: publicJwk, format: 'jwk' },

    // Decrypt using the private key.
    privateKey,
    { format: 'pem', key: privatePem },
    { format: 'der', type: 'pkcs1', key: privateDER },
    { key: jwk, format: 'jwk' },
  ]);
}

{
  // This should not cause a crash: https://github.com/nodejs/node/issues/25247
  assert.throws(() => {
    createPrivateKey({ key: '' });
  }, common.hasOpenSSL3 ? {
    message: 'error:1E08010C:DECODER routines::unsupported',
  } : {
    message: 'error:0909006C:PEM routines:get_name:no start line',
    code: 'ERR_OSSL_PEM_NO_START_LINE',
    reason: 'no start line',
    library: 'PEM routines',
    function: 'get_name',
  });

  // This should not abort either: https://github.com/nodejs/node/issues/29904
  assert.throws(() => {
    createPrivateKey({ key: Buffer.alloc(0), format: 'der', type: 'spki' });
  }, {
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The property 'options.type' is invalid. Received 'spki'"
  });

  // Unlike SPKI, PKCS#1 is a valid encoding for private keys (and public keys),
  // so it should be accepted by createPrivateKey, but OpenSSL won't parse it.
  assert.throws(() => {
    const key = createPublicKey(publicPem).export({
      format: 'der',
      type: 'pkcs1'
    });
    createPrivateKey({ key, format: 'der', type: 'pkcs1' });
  }, common.hasOpenSSL3 ? {
    message: /error:1E08010C:DECODER routines::unsupported/,
    library: 'DECODER routines'
  } : {
    message: /asn1 encoding/,
    library: 'asn1 encoding routines'
  });
}

[
  { private: fixtures.readKey('ed25519_private.pem', 'ascii'),
    public: fixtures.readKey('ed25519_public.pem', 'ascii'),
    keyType: 'ed25519',
    jwk: {
      crv: 'Ed25519',
      x: 'K1wIouqnuiA04b3WrMa-xKIKIpfHetNZRv3h9fBf768',
      d: 'wVK6M3SMhQh3NK-7GRrSV-BVWQx1FO5pW8hhQeu_NdA',
      kty: 'OKP'
    } },
  { private: fixtures.readKey('ed448_private.pem', 'ascii'),
    public: fixtures.readKey('ed448_public.pem', 'ascii'),
    keyType: 'ed448',
    jwk: {
      crv: 'Ed448',
      x: 'oX_ee5-jlcU53-BbGRsGIzly0V-SZtJ_oGXY0udf84q2hTW2RdstLktvwpkVJOoNb7o' +
         'Dgc2V5ZUA',
      d: '060Ke71sN0GpIc01nnGgMDkp0sFNQ09woVo4AM1ffax1-mjnakK0-p-S7-Xf859QewX' +
         'jcR9mxppY',
      kty: 'OKP'
    } },
  { private: fixtures.readKey('x25519_private.pem', 'ascii'),
    public: fixtures.readKey('x25519_public.pem', 'ascii'),
    keyType: 'x25519',
    jwk: {
      crv: 'X25519',
      x: 'aSb8Q-RndwfNnPeOYGYPDUN3uhAPnMLzXyfi-mqfhig',
      d: 'mL_IWm55RrALUGRfJYzw40gEYWMvtRkesP9mj8o8Omc',
      kty: 'OKP'
    } },
  { private: fixtures.readKey('x448_private.pem', 'ascii'),
    public: fixtures.readKey('x448_public.pem', 'ascii'),
    keyType: 'x448',
    jwk: {
      crv: 'X448',
      x: 'ioHSHVpTs6hMvghosEJDIR7ceFiE3-Xccxati64oOVJ7NWjfozE7ae31PXIUFq6cVYg' +
         'vSKsDFPA',
      d: 'tMNtrO_q8dlY6Y4NDeSTxNQ5CACkHiPvmukidPnNIuX_EkcryLEXt_7i6j6YZMKsrWy' +
         'S0jlSYJk',
      kty: 'OKP'
    } },
].forEach((info) => {
  const keyType = info.keyType;

  {
    const key = createPrivateKey(info.private);
    assert.strictEqual(key.type, 'private');
    assert.strictEqual(key.asymmetricKeyType, keyType);
    assert.strictEqual(key.symmetricKeySize, undefined);
    assert.strictEqual(
      key.export({ type: 'pkcs8', format: 'pem' }), info.private);
    assert.deepStrictEqual(
      key.export({ format: 'jwk' }), info.jwk);
  }

  {
    const key = createPrivateKey({ key: info.jwk, format: 'jwk' });
    assert.strictEqual(key.type, 'private');
    assert.strictEqual(key.asymmetricKeyType, keyType);
    assert.strictEqual(key.symmetricKeySize, undefined);
    assert.strictEqual(
      key.export({ type: 'pkcs8', format: 'pem' }), info.private);
    assert.deepStrictEqual(
      key.export({ format: 'jwk' }), info.jwk);
  }

  {
    for (const input of [
      info.private, info.public, { key: info.jwk, format: 'jwk' }]) {
      const key = createPublicKey(input);
      assert.strictEqual(key.type, 'public');
      assert.strictEqual(key.asymmetricKeyType, keyType);
      assert.strictEqual(key.symmetricKeySize, undefined);
      assert.strictEqual(
        key.export({ type: 'spki', format: 'pem' }), info.public);
      const jwk = { ...info.jwk };
      delete jwk.d;
      assert.deepStrictEqual(
        key.export({ format: 'jwk' }), jwk);
    }
  }
});

[
  { private: fixtures.readKey('ec_p256_private.pem', 'ascii'),
    public: fixtures.readKey('ec_p256_public.pem', 'ascii'),
    keyType: 'ec',
    namedCurve: 'prime256v1',
    jwk: {
      crv: 'P-256',
      d: 'DxBsPQPIgMuMyQbxzbb9toew6Ev6e9O6ZhpxLNgmAEo',
      kty: 'EC',
      x: 'X0mMYR_uleZSIPjNztIkAS3_ud5LhNpbiIFp6fNf2Gs',
      y: 'UbJuPy2Xi0lW7UYTBxPK3yGgDu9EAKYIecjkHX5s2lI'
    } },
  { private: fixtures.readKey('ec_secp256k1_private.pem', 'ascii'),
    public: fixtures.readKey('ec_secp256k1_public.pem', 'ascii'),
    keyType: 'ec',
    namedCurve: 'secp256k1',
    jwk: {
      crv: 'secp256k1',
      d: 'c34ocwTwpFa9NZZh3l88qXyrkoYSxvC0FEsU5v1v4IM',
      kty: 'EC',
      x: 'cOzhFSpWxhalCbWNdP2H_yUkdC81C9T2deDpfxK7owA',
      y: '-A3DAZTk9IPppN-f03JydgHaFvL1fAHaoXf4SX4NXyo'
    } },
  { private: fixtures.readKey('ec_p384_private.pem', 'ascii'),
    public: fixtures.readKey('ec_p384_public.pem', 'ascii'),
    keyType: 'ec',
    namedCurve: 'secp384r1',
    jwk: {
      crv: 'P-384',
      d: 'dwfuHuAtTlMRn7ZBCBm_0grpc1D_4hPeNAgevgelljuC0--k_LDFosDgBlLLmZsi',
      kty: 'EC',
      x: 'hON3nzGJgv-08fdHpQxgRJFZzlK-GZDGa5f3KnvM31cvvjJmsj4UeOgIdy3rDAjV',
      y: 'fidHhtecNCGCfLqmrLjDena1NSzWzWH1u_oUdMKGo5XSabxzD7-8JZxjpc8sR9cl'
    } },
  { private: fixtures.readKey('ec_p521_private.pem', 'ascii'),
    public: fixtures.readKey('ec_p521_public.pem', 'ascii'),
    keyType: 'ec',
    namedCurve: 'secp521r1',
    jwk: {
      crv: 'P-521',
      d: 'ABIIbmn3Gm_Y11uIDkC3g2ijpRxIrJEBY4i_JJYo5OougzTl3BX2ifRluPJMaaHcNer' +
         'bQH_WdVkLLX86ShlHrRyJ',
      kty: 'EC',
      x: 'AaLFgjwZtznM3N7qsfb86awVXe6c6djUYOob1FN-kllekv0KEXV0bwcDjPGQz5f6MxL' +
         'CbhMeHRavUS6P10rsTtBn',
      y: 'Ad3flexBeAfXceNzRBH128kFbOWD6W41NjwKRqqIF26vmgW_8COldGKZjFkOSEASxPB' +
         'cvA2iFJRUyQ3whC00j0Np'
    } },
].forEach((info) => {
  const { keyType, namedCurve } = info;

  {
    const key = createPrivateKey(info.private);
    assert.strictEqual(key.type, 'private');
    assert.strictEqual(key.asymmetricKeyType, keyType);
    assert.deepStrictEqual(key.asymmetricKeyDetails, { namedCurve });
    assert.strictEqual(key.symmetricKeySize, undefined);
    assert.strictEqual(
      key.export({ type: 'pkcs8', format: 'pem' }), info.private);
    assert.deepStrictEqual(
      key.export({ format: 'jwk' }), info.jwk);
  }

  {
    const key = createPrivateKey({ key: info.jwk, format: 'jwk' });
    assert.strictEqual(key.type, 'private');
    assert.strictEqual(key.asymmetricKeyType, keyType);
    assert.deepStrictEqual(key.asymmetricKeyDetails, { namedCurve });
    assert.strictEqual(key.symmetricKeySize, undefined);
    assert.strictEqual(
      key.export({ type: 'pkcs8', format: 'pem' }), info.private);
    assert.deepStrictEqual(
      key.export({ format: 'jwk' }), info.jwk);
  }

  {
    for (const input of [
      info.private, info.public, { key: info.jwk, format: 'jwk' }]) {
      const key = createPublicKey(input);
      assert.strictEqual(key.type, 'public');
      assert.strictEqual(key.asymmetricKeyType, keyType);
      assert.deepStrictEqual(key.asymmetricKeyDetails, { namedCurve });
      assert.strictEqual(key.symmetricKeySize, undefined);
      assert.strictEqual(
        key.export({ type: 'spki', format: 'pem' }), info.public);
      const jwk = { ...info.jwk };
      delete jwk.d;
      assert.deepStrictEqual(
        key.export({ format: 'jwk' }), jwk);
    }
  }
});

{
  // Reading an encrypted key without a passphrase should fail.
  assert.throws(() => createPrivateKey(privateDsa), common.hasOpenSSL3 ? {
    name: 'Error',
    message: 'error:07880109:common libcrypto routines::interrupted or ' +
             'cancelled',
  } : {
    name: 'TypeError',
    code: 'ERR_MISSING_PASSPHRASE',
    message: 'Passphrase required for encrypted key'
  });

  // Reading an encrypted key with a passphrase that exceeds OpenSSL's buffer
  // size limit should fail with an appropriate error code.
  assert.throws(() => createPrivateKey({
    key: privateDsa,
    format: 'pem',
    passphrase: Buffer.alloc(1025, 'a')
  }), common.hasOpenSSL3 ? { name: 'Error' } : {
    code: 'ERR_OSSL_PEM_BAD_PASSWORD_READ',
    name: 'Error'
  });

  // The buffer has a size of 1024 bytes, so this passphrase should be permitted
  // (but will fail decryption).
  assert.throws(() => createPrivateKey({
    key: privateDsa,
    format: 'pem',
    passphrase: Buffer.alloc(1024, 'a')
  }), {
    message: /bad decrypt/
  });

  const publicKey = createPublicKey(publicDsa);
  assert.strictEqual(publicKey.type, 'public');
  assert.strictEqual(publicKey.asymmetricKeyType, 'dsa');
  assert.strictEqual(publicKey.symmetricKeySize, undefined);
  assert.throws(
    () => publicKey.export({ format: 'jwk' }),
    { code: 'ERR_CRYPTO_JWK_UNSUPPORTED_KEY_TYPE' });

  const privateKey = createPrivateKey({
    key: privateDsa,
    format: 'pem',
    passphrase: 'secret'
  });
  assert.strictEqual(privateKey.type, 'private');
  assert.strictEqual(privateKey.asymmetricKeyType, 'dsa');
  assert.strictEqual(privateKey.symmetricKeySize, undefined);
  assert.throws(
    () => privateKey.export({ format: 'jwk' }),
    { code: 'ERR_CRYPTO_JWK_UNSUPPORTED_KEY_TYPE' });
}

{
  // Test RSA-PSS.
  {
    // This key pair does not restrict the message digest algorithm or salt
    // length.
    const publicPem = fixtures.readKey('rsa_pss_public_2048.pem');
    const privatePem = fixtures.readKey('rsa_pss_private_2048.pem');

    const publicKey = createPublicKey(publicPem);
    const privateKey = createPrivateKey(privatePem);

    // Because no RSASSA-PSS-params appears in the PEM, no defaults should be
    // added for the PSS parameters. This is different from an empty
    // RSASSA-PSS-params sequence (see test below).
    const expectedKeyDetails = {
      modulusLength: 2048,
      publicExponent: 65537n
    };

    assert.strictEqual(publicKey.type, 'public');
    assert.strictEqual(publicKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(publicKey.asymmetricKeyDetails, expectedKeyDetails);

    assert.strictEqual(privateKey.type, 'private');
    assert.strictEqual(privateKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(privateKey.asymmetricKeyDetails, expectedKeyDetails);

    assert.throws(
      () => publicKey.export({ format: 'jwk' }),
      { code: 'ERR_CRYPTO_JWK_UNSUPPORTED_KEY_TYPE' });
    assert.throws(
      () => privateKey.export({ format: 'jwk' }),
      { code: 'ERR_CRYPTO_JWK_UNSUPPORTED_KEY_TYPE' });

    for (const key of [privatePem, privateKey]) {
      // Any algorithm should work.
      for (const algo of ['sha1', 'sha256']) {
        // Any salt length should work.
        for (const saltLength of [undefined, 8, 10, 12, 16, 18, 20]) {
          const signature = createSign(algo)
                            .update('foo')
                            .sign({ key, saltLength });

          for (const pkey of [key, publicKey, publicPem]) {
            const okay = createVerify(algo)
                         .update('foo')
                         .verify({ key: pkey, saltLength }, signature);

            assert.ok(okay);
          }
        }
      }
    }

    // Exporting the key using PKCS#1 should not work since this would discard
    // any algorithm restrictions.
    assert.throws(() => {
      publicKey.export({ format: 'pem', type: 'pkcs1' });
    }, {
      code: 'ERR_CRYPTO_INCOMPATIBLE_KEY_OPTIONS'
    });
  }

  {
    // This key pair enforces sha1 as the message digest and the MGF1
    // message digest and a salt length of 20 bytes.

    const publicPem = fixtures.readKey('rsa_pss_public_2048_sha1_sha1_20.pem');
    const privatePem =
        fixtures.readKey('rsa_pss_private_2048_sha1_sha1_20.pem');

    const publicKey = createPublicKey(publicPem);
    const privateKey = createPrivateKey(privatePem);

    // Unlike the previous key pair, this key pair contains an RSASSA-PSS-params
    // sequence. However, because all values in the RSASSA-PSS-params are set to
    // their defaults (see RFC 3447), the ASN.1 structure contains an empty
    // sequence. Node.js should add the default values to the key details.
    const expectedKeyDetails = {
      modulusLength: 2048,
      publicExponent: 65537n,
      hashAlgorithm: 'sha1',
      mgf1HashAlgorithm: 'sha1',
      saltLength: 20
    };

    assert.strictEqual(publicKey.type, 'public');
    assert.strictEqual(publicKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(publicKey.asymmetricKeyDetails, expectedKeyDetails);

    assert.strictEqual(privateKey.type, 'private');
    assert.strictEqual(privateKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(privateKey.asymmetricKeyDetails, expectedKeyDetails);
  }

  {
    // This key pair enforces sha256 as the message digest and the MGF1
    // message digest and a salt length of at least 16 bytes.
    const publicPem =
      fixtures.readKey('rsa_pss_public_2048_sha256_sha256_16.pem');
    const privatePem =
      fixtures.readKey('rsa_pss_private_2048_sha256_sha256_16.pem');

    const publicKey = createPublicKey(publicPem);
    const privateKey = createPrivateKey(privatePem);

    assert.strictEqual(publicKey.type, 'public');
    assert.strictEqual(publicKey.asymmetricKeyType, 'rsa-pss');

    assert.strictEqual(privateKey.type, 'private');
    assert.strictEqual(privateKey.asymmetricKeyType, 'rsa-pss');

    for (const key of [privatePem, privateKey]) {
      // Signing with anything other than sha256 should fail.
      assert.throws(() => {
        createSign('sha1').sign(key);
      }, /digest not allowed/);

      // Signing with salt lengths less than 16 bytes should fail.
      for (const saltLength of [8, 10, 12]) {
        assert.throws(() => {
          createSign('sha1').sign({ key, saltLength });
        }, /pss saltlen too small/);
      }

      // Signing with sha256 and appropriate salt lengths should work.
      for (const saltLength of [undefined, 16, 18, 20]) {
        const signature = createSign('sha256')
                          .update('foo')
                          .sign({ key, saltLength });

        for (const pkey of [key, publicKey, publicPem]) {
          const okay = createVerify('sha256')
                       .update('foo')
                       .verify({ key: pkey, saltLength }, signature);

          assert.ok(okay);
        }
      }
    }
  }

  {
    // This key enforces sha512 as the message digest and sha256 as the MGF1
    // message digest.
    const publicPem =
      fixtures.readKey('rsa_pss_public_2048_sha512_sha256_20.pem');
    const privatePem =
      fixtures.readKey('rsa_pss_private_2048_sha512_sha256_20.pem');

    const publicKey = createPublicKey(publicPem);
    const privateKey = createPrivateKey(privatePem);

    const expectedKeyDetails = {
      modulusLength: 2048,
      publicExponent: 65537n,
      hashAlgorithm: 'sha512',
      mgf1HashAlgorithm: 'sha256',
      saltLength: 20
    };

    assert.strictEqual(publicKey.type, 'public');
    assert.strictEqual(publicKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(publicKey.asymmetricKeyDetails, expectedKeyDetails);

    assert.strictEqual(privateKey.type, 'private');
    assert.strictEqual(privateKey.asymmetricKeyType, 'rsa-pss');
    assert.deepStrictEqual(privateKey.asymmetricKeyDetails, expectedKeyDetails);

    // Node.js usually uses the same hash function for the message and for MGF1.
    // However, when a different MGF1 message digest algorithm has been
    // specified as part of the key, it should automatically switch to that.
    // This behavior is required by sections 3.1 and 3.3 of RFC4055.
    for (const key of [privatePem, privateKey]) {
      // sha256 matches the MGF1 hash function and should be used internally,
      // but it should not be permitted as the main message digest algorithm.
      for (const algo of ['sha1', 'sha256']) {
        assert.throws(() => {
          createSign(algo).sign(key);
        }, /digest not allowed/);
      }

      // sha512 should produce a valid signature.
      const signature = createSign('sha512')
                        .update('foo')
                        .sign(key);

      for (const pkey of [key, publicKey, publicPem]) {
        const okay = createVerify('sha512')
                     .update('foo')
                     .verify(pkey, signature);

        assert.ok(okay);
      }
    }
  }
}

{
  // Exporting an encrypted private key requires a cipher
  const privateKey = createPrivateKey(privatePem);
  assert.throws(() => {
    privateKey.export({
      format: 'pem', type: 'pkcs8', passphrase: 'super-secret'
    });
  }, {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_VALUE',
    message: "The property 'options.cipher' is invalid. Received undefined"
  });
}

{
  // SecretKeyObject export buffer format (default)
  const buffer = Buffer.from('Hello World');
  const keyObject = createSecretKey(buffer);
  assert.deepStrictEqual(keyObject.export(), buffer);
  assert.deepStrictEqual(keyObject.export({}), buffer);
  assert.deepStrictEqual(keyObject.export({ format: 'buffer' }), buffer);
  assert.deepStrictEqual(keyObject.export({ format: undefined }), buffer);
}

{
  // Exporting an "oct" JWK from a SecretKeyObject
  const buffer = Buffer.from('Hello World');
  const keyObject = createSecretKey(buffer);
  assert.deepStrictEqual(
    keyObject.export({ format: 'jwk' }),
    { kty: 'oct', k: 'SGVsbG8gV29ybGQ' }
  );
}

{
  // Exporting a JWK unsupported curve EC key
  const supported = ['prime256v1', 'secp256k1', 'secp384r1', 'secp521r1'];
  // Find an unsupported curve regardless of whether a FIPS compliant crypto
  // provider is currently in use.
  const namedCurve = getCurves().find((curve) => !supported.includes(curve));
  assert(namedCurve);
  const keyPair = generateKeyPairSync('ec', { namedCurve });
  const { publicKey, privateKey } = keyPair;
  assert.throws(
    () => publicKey.export({ format: 'jwk' }),
    {
      code: 'ERR_CRYPTO_JWK_UNSUPPORTED_CURVE',
      message: `Unsupported JWK EC curve: ${namedCurve}.`
    });
  assert.throws(
    () => privateKey.export({ format: 'jwk' }),
    {
      code: 'ERR_CRYPTO_JWK_UNSUPPORTED_CURVE',
      message: `Unsupported JWK EC curve: ${namedCurve}.`
    });
}

{
  const first = Buffer.from('Hello');
  const second = Buffer.from('World');
  const keyObject = createSecretKey(first);
  assert(createSecretKey(first).equals(createSecretKey(first)));
  assert(!createSecretKey(first).equals(createSecretKey(second)));

  assert.throws(() => keyObject.equals(0), {
    name: 'TypeError',
    code: 'ERR_INVALID_ARG_TYPE',
    message: 'The "otherKeyObject" argument must be an instance of KeyObject. Received type number (0)'
  });

  assert(keyObject.equals(keyObject));
  assert(!keyObject.equals(createPublicKey(publicPem)));
  assert(!keyObject.equals(createPrivateKey(privatePem)));
}

{
  const first = generateKeyPairSync('ed25519');
  const second = generateKeyPairSync('ed25519');
  const secret = generateKeySync('aes', { length: 128 });

  assert(first.publicKey.equals(first.publicKey));
  assert(first.publicKey.equals(createPublicKey(
    first.publicKey.export({ format: 'pem', type: 'spki' }))));
  assert(!first.publicKey.equals(second.publicKey));
  assert(!first.publicKey.equals(second.privateKey));
  assert(!first.publicKey.equals(secret));

  assert(first.privateKey.equals(first.privateKey));
  assert(first.privateKey.equals(createPrivateKey(
    first.privateKey.export({ format: 'pem', type: 'pkcs8' }))));
  assert(!first.privateKey.equals(second.privateKey));
  assert(!first.privateKey.equals(second.publicKey));
  assert(!first.privateKey.equals(secret));
}

{
  const first = generateKeyPairSync('ed25519');
  const second = generateKeyPairSync('ed448');

  assert(!first.publicKey.equals(second.publicKey));
  assert(!first.publicKey.equals(second.privateKey));
  assert(!first.privateKey.equals(second.privateKey));
  assert(!first.privateKey.equals(second.publicKey));
}

{
  const first = createSecretKey(Buffer.alloc(0));
  const second = createSecretKey(new ArrayBuffer(0));
  const third = createSecretKey(Buffer.alloc(1));
  assert(first.equals(first));
  assert(first.equals(second));
  assert(!first.equals(third));
  assert(!third.equals(first));
}

{
  // This should not cause a crash: https://github.com/nodejs/node/issues/44471
  for (const key of ['', 'foo', null, undefined, true, Boolean]) {
    assert.throws(() => {
      createPublicKey({ key, format: 'jwk' });
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
    assert.throws(() => {
      createPrivateKey({ key, format: 'jwk' });
    }, { code: 'ERR_INVALID_ARG_TYPE', message: /The "key\.key" property must be of type object/ });
  }
}
