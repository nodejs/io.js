'use strict';

const common = require('../common');

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

if (common.isPi) {
  common.skip('Too slow for Raspberry Pi devices');
}

const assert = require('assert');
const { subtle } = globalThis.crypto;

function getDeriveKeyInfo(name, length, hash, ...usages) {
  return [{ name, length, hash }, usages];
}

const kDerivedKeyTypes = [
  ['AES-CBC', 128, undefined, 'encrypt', 'decrypt'],
  ['AES-CBC', 256, undefined, 'encrypt', 'decrypt'],
  ['AES-CTR', 128, undefined, 'encrypt', 'decrypt'],
  ['AES-CTR', 256, undefined, 'encrypt', 'decrypt'],
  ['AES-GCM', 128, undefined, 'encrypt', 'decrypt'],
  ['AES-GCM', 256, undefined, 'encrypt', 'decrypt'],
  ['AES-KW', 128, undefined, 'wrapKey', 'unwrapKey'],
  ['AES-KW', 256, undefined, 'wrapKey', 'unwrapKey'],
  ['HMAC', 256, 'SHA-1', 'sign', 'verify'],
  ['HMAC', 256, 'SHA-256', 'sign', 'verify'],
  ['HMAC', 256, 'SHA-384', 'sign', 'verify'],
  ['HMAC', 256, 'SHA-512', 'sign', 'verify'],
];

const kPasswords = {
  short: '5040737377307264',
  long: '55736572732073686f756c64207069636b206c6f6' +
        'e6720706173737068726173657320286e6f742075' +
        '73652073686f72742070617373776f7264732921',
  empty: '',
};

const kSalts = {
  short: '4e61436c',
  long: '536f6469756d2043686c6f7269646520636f6d706f756e64',
  empty: '',
};

const kDerivations = {
  short: {
    short: {
      'SHA-384': {
        '1': '80cd0f15364366a72551c37975f7b637ba89' +
             'c29b4639ec720f69a70dbbed515c',
        '1000': 'aaec5a976d4d35cb2024486fc9f9bb9aa' +
                '3eae7cef2bce62664b5b3751cf50ff1',
        '100000': '6f5ea3c6c6f5e4833467b47c3a671e6' +
                  '5714e87071bd1e36d716f846b5cd28980',
      },
      'SHA-512': {
        '1': '69f4d5cef5c7d8ba938e880388c8f63b6b2' +
             '448b2626d1343fc5cb68bbd7f27b2',
        '1000': '865c5945e11f5bf3ddf002e7cb1748f6' +
                '224d2671e806dad4aaf090a04367da29',
        '100000': '483ba7f2e2fe382cf61d20b29812e2' +
                  'd49610a60041ae40ecf9fc7ef138e93876',
      },
      'SHA-1': {
        '1': '4624dbd21373ee5659c125b184eedaa26a3' +
             '3b77ca11314b9f0c9dae1e44e9b04',
        '1000': '5388ea5e62e1b557981abe5ce4132127' +
                '58aa6a9d2c5bf08c019d459dba666b90',
        '100000': 'f58f435fbc5c05865c914fd972108a' +
                  '09457d5f9a48f14e75e4cc02d98983038a',
      },
      'SHA-256': {
        '1': 'c6bc55a404adcea36a1ab56798085e0aaf6' +
             '97f6bb2c16a5072f838f17dfe6cb6',
        '1000': '4e6ca57957439be3a75370424225e221' +
                '1d55f05af00561df3f3efee9116bc34c',
        '100000': 'ab25796598e74b29c324f5ba4d90ea' +
                  '7dc89fc6891041b4d56c94156505f722c0',
      },
    },
    long: {
      'SHA-384': {
        '1': '6807346cc53eded1cb964a72628589a6bd4' +
             '8355990bfdfe7465109710207059d',
        '1000': 'a310ef3c6b3a95e6d8ca6644e3dcfd88' +
                '222a59fe8e00c52d6a12631d82c1d24b',
        '100000': '2c8c6674c879cf1850bc9b7fbdcc6e' +
                  'a7abb0a1522196a866875305dea57486f3',
      },
      'SHA-512': {
        '1': '5777027aff4051fb9b43c1f1ef0463bd677' +
             '51175d428a13da3da845a591332cd',
        '1000': '9c17fe96895eadbfd1cc095fc1bb834f' +
                '28e5ccc9ec96ca814cff941a4bf40727',
        '100000': 'b479c9715c421638dce0a705fc0b7b' +
                  'a7d56fa3063188063580e070dff1db497c',
      },
      'SHA-1': {
        '1': '576f7c165825bef9ef14b4bc2c82469d1e4' +
             '08ff8e7ba306694797f9e45b766ed',
        '1000': '89d3b27b5f6e8af015f2f87cf368a143' +
                '8a206c4ecf5fe681fc3bf94c56213ef6',
        '100000': '1e39e8bf6676fcd3156655457afa14' +
                  'bee771dbcbfcd07241c7cee209a7cb1fe9',
      },
      'SHA-256': {
        '1': '12b90f594f0908cf912d655c948f9c2a1ea' +
             'b855765bc12785ef18aa02b8e7edc',
        '1000': 'b1a7b7dc20df174a4a0e410dbfaf03b4' +
                'c375c450a89d7a9ed349b4e52e64dfd8',
        '100000': 'd4594d8a1b59520a48878922a65d66' +
                  '3d28f6a5fa49e931d300d8f9baf93d0aeb',
      },
    },
    empty: {
      'SHA-384': {
        '1': '4f1089c01e438bde649a379fa418fbc3b85' +
             '6258772dfe911806f9bd0809fbc7e',
        '1000': 'aeb5f97d6627eebcde6b139a00895500' +
                '30f7401c67e01c057a3338175e3f3a17',
        '100000': 'd7687df6c781dc88d64ef9cbaf95d3' +
                  'd5d1155f66b230239e6e81c1550c8840cf',
      },
      'SHA-512': {
        '1': '8f7b7d459c752f64bf12be625b65d496ac2' +
             '4ea36516b168e16fb026845b4e82e',
        '1000': 'b5ac720b7abe0832fc51a31b1ec5673b' +
                'eb1e41840adfd3d606e8638f4006eb48',
        '100000': 'ba1a0f36bad771526564051eb9ca20' +
                  '7da19b62e53762349976a9a3d1b0ef7e20',
      },
      'SHA-1': {
        '1': 'c0cffb0ce5db351faa24dad5902583cfc30' +
             'a9f54d9aa6991fe821d03122127e9',
        '1000': '736f3c3d6ebcc2a7b970403e2696c0eb' +
                '4cd1770f55f196fc7089e666c11f77da',
        '100000': '1c5095ac9a7bd410ef0f72c993eca9' +
                  '1bb0e571e9b2fbab704f8c131191fad16c',
      },
      'SHA-256': {
        '1': '019e54ab42f00485d3aa1b26fcde21ae5f5' +
             '2cb0f0960ffc9767f25c65e2db2f9',
        '1000': 'b9d2f2217b4ee5a8bf0345f36b2c9887' +
                '33f503a975dfeac7b7135f54a5f29971',
        '100000': 'a7a2869829797807b3e576c17878b4' +
                  '66449e89e60447d541775a96eb7c1a5ded',
      },
    },
  },
  long: {
    short: {
      'SHA-384': {
        '1': '5ede8836fdabeec5d3733b434abac443d41' +
             '5193b599e0926193b000f406a5a7d',
        '1000': 'faa442fbabf4058cc65368b53d7ec511' +
                '3c09ea7e5e3743312f4bebedd980ba37',
        '100000': 'f62ae6c7871b181aa71232f5eb8837' +
                  '2498ef32ac0a7d715119e8f052eb102d29',
      },
      'SHA-512': {
        '1': '3e9c12b3f6dfb6441594ec7063fca962ffd' +
             'a10b6cf30b898a31ef9f1306b1119',
        '1000': 'f0928f50a155f26a8c9c1bc7f3b5cb53' +
                '1c53a8f51040c9ce5fc79d430ff0c0f4',
        '100000': '974acfbb0f0f20c81ec92829f38c3d' +
                  'af086a7df58b912b856d1f5ecc9355ef1b',
      },
      'SHA-1': {
        '1': '8ae72f94e6fcd54fcbfca66200a211a51b2' +
             'f846787d20b6808bedf156ce46ca0',
        '1000': '53b42161134e15c871abd71aba1390d0' +
                '1f4c6a940caaf5c179258d8f1b1d680b',
        '100000': 'a7fda4c79dd3ba1a875f65e9248b21' +
                  '0899ca0814ae38995d8ce5a53560cbac31',
      },
      'SHA-256': {
        '1': 'ffa1e9a727a92c27ae6f74b1c7978f9e1af' +
             '860e106376340ac43d969d136405b',
        '1000': 'eeeb7714420a00b18acec2b5979d1da6' +
                '1373202b7f8ba71b086293aab859e0a0',
        '100000': 'deac70cbe3f1720e353b4e8016ddb5' +
                  '9475efb70b6a2385e735d2d6ea6d624a4d',
      },
    },
    long: {
      'SHA-384': {
        '1': 'cf55422cef6e1bc49e6d082b2273d480e8f' +
             '2e8822dadd1469c2a32d9657d12f1',
        '1000': '35658551f0ec13398a7b45e0261cfd65' +
                '4c1e52411e6e457dee68f4aeabe925a7',
        '100000': '1abab5f1e461df378b88c0a22be76e' +
                  'f2f1627df74ac7cbfb84bdccb354bc8889',
      },
      'SHA-512': {
        '1': 'de4afbc0add3e4d32f4bc6e122a88ae44a2' +
             'b3ccf0148e7762bac05c43e94ef7f',
        '1000': '43e12024c4d354727f7e58842ccb6033' +
                'a161d60dc5ae516f076e4a58a1880d38',
        '100000': 'f9a92384a4eadfc3560649b37fb676' +
                  'e83c453cbbd99f80bba6f0a10ebd150b52',
      },
      'SHA-1': {
        '1': '1d104ea5d235006a12a80f71b80ee528048' +
             'b64cc1a7a0f30f7df4ba26b8320c7',
        '1000': '6e90c86ee07b873e965071025673ff05' +
                '429f678c30f91b37e1e2da512036d320',
        '100000': '141030763bf983c8564d5d4c935fe3' +
                  'ca3549608159ac1934c1599040668c2363',
      },
      'SHA-256': {
        '1': 'fd5caeb8b3abe589bc159c4e51f800570e7' +
             '4f64397a6c5ee131dfed93f0511aa',
        '1000': '3fd587c94ba946b8b9dccddd2a5b74f6' +
                '778d4f61e691f83ac47a2fa9580bfdf8',
        '100000': '11992d8b813311244c544b62292945' +
                  'e208d403cebd6b9552a1a562065d9958ea',
      },
    },
    empty: {
      'SHA-384': {
        '1': '49ab3f9f882fdb9e528b4d9f1b3e8c71d26' +
             '39abf1701d56eb99bd51201e420ff',
        '1000': 'f9ca148b0c041890bff8831db6174719' +
                '7e94ce68f190edf269694b4d644861ca',
        '100000': '1749dfcd77e5258519ea2231ba2cd6' +
                  '543b073339ac9b1545bb643153faf6d17b',
      },
      'SHA-512': {
        '1': 'd1bfa1a6b8a977839f8c3f9d52dd02104e2' +
             '029c0eb2a6208cc408816e7768a8c',
        '1000': '457a7955ebecec71a51efb6237e5b1d6' +
                '2f4deab5c93d7b3d11d1e70faffa417e',
        '100000': 'e805ac9cc1d8412c42446d237d1b50' +
                  '4f9540b362bd1b75e451531e853e24753d',
      },
      'SHA-1': {
        '1': 'a46a62986d9c3909f41014dd72cfe34a261' +
             '247854d7312cf4fbead60b9b69edd',
        '1000': 'e7375de5036766c40cb85f43b53fce4f' +
                'fa402ab6be3571007ef5d55453fd7f0a',
        '100000': '7a403d9a13aed8164e9c072c545462' +
                  '251fd942f1736a6bf03ce1c88330048e04',
      },
      'SHA-256': {
        '1': '4f510c5181ac5c2c5fd4bd141f9712495be' +
             'ca279624742b4d6d30d08b96c0a69',
        '1000': '7e66c84bea888f92c348d9145585186c' +
                'ae472b12fba7f0ad28179575c1aa815a',
        '100000': '5f1a6ac4a56d9796a7309a78daaaf9' +
                  '18badaf5ed1eecc3f0b8a3a44c3d38d654',
      },
    },
  },
  empty: {
    short: {
      'SHA-384': {
        '1': 'e9f0da1e97dfa455f858ce6b9af1ecc0299' +
             'f125ff1a847eb5d4955866f43e604',
        '1000': '7ff7954aeddf41795fc8300666786d49' +
                '74269aa91cc7e93811c953331d56d609',
        '100000': '1c73132b6a55e9d9de2cdbfe1f55bf' +
                  '0ab59fd91f78f109c50096038b8557b147',
      },
      'SHA-512': {
        '1': 'e7e2b41f4887421bcb764eb4a56f63d2502' +
             'e33c764fbdf60626ad42ed9672342',
        '1000': 'd561c4c84e9c60ba4752a2d383bf55ef' +
                'f643fc9e452252d6821e39449350cf72',
        '100000': 'efd00752bc9ffafb5a399dd1d5834e' +
                  '8d2c2b676ecd4b2063fb1fe581d0f1380b',
      },
      'SHA-1': {
        '1': 'a667da47b8f857b7c65f70a6c8e7a06ce0d' +
             '25211a2b6ebaf58dcaaf268b46b1d',
        '1000': '72c92bbd3ddab4789e88e42ad1cda83c' +
                'c0729e6cb5106a577e50d5cf61782481',
        '100000': '06e19e1b83e6480b1554df2b31a2c9' +
                  '2d1bfcf9bc1bdbc8751ff8685bdeef7dc9',
      },
      'SHA-256': {
        '1': '2ddb49243eb3b5912cb260cdd87fb04ef0d' +
             '111bfa44d40a45e02a8a5c3c1518d',
        '1000': '2835f3ed53565420c90951509b0c1173' +
                'b645174f1546ab3ac3e6c85cb471b53b',
        '100000': '80aed905ca32ae0bb2a9d8f532f048' +
                  'a0e672463eef9f83dfa7d88bca726553ea',
      },
    },
    long: {
      'SHA-384': {
        '1': '7b0bcca81dd637a3b3398666619716c5f2b1' +
             'f4a5c24e85c18a9955559e4d7692',
        '1000': '8bb89cf71972fe5acc16fdc5f8cffd2c2' +
                'e7178c086b3bbe61cc1314619135958',
        '100000': '26c6a8ae4bd1fbe715ae478efff3eca' +
                  'e83afa617ed35bd4a3f63c3da76a42d22',
      },
      'SHA-512': {
        '1': 'bb73f8168a8f391d3d54ca892fb72b8e603' +
             '5e37f891e5a70491b94dc05510bc4',
        '1000': '5cacc16cdfbe052cfd73a9891b8c0e78' +
                'b19b2e07eae2423d48fed5e08aa8494b',
        '100000': '87fdfc293392cbf33ecc9b5141a2fe' +
                  'fa74d150499756863c484c0a78b6274d7f',
      },
      'SHA-1': {
        '1': '1f46b40cf2fb3dc41a3d9ced8897b861050' +
             '36810e2bfac7040814bd65d428d67',
        '1000': 'cc5748ecc41288a0e13368543aaa2ef6' +
                '2c97ba7518fa88f6e11c35763fc930b4',
        '100000': '33e2993bf4729dc993fff66e69cc55' +
                  '777135ebfabce533575bce4a96645a742c',
      },
      'SHA-256': {
        '1': '61c935c462c3321c89663545d13a4f6b52b' +
             '5191cfb7479e58dcfe6444d43106c',
        '1000': '1353f7458237ab332ee052e29f829a2a' +
                'b90e72630ea10493b4eecffb9ff89e1d',
        '100000': '79baf80ec582920538801e9d929ce0' +
                  '7084277987488d733a026852c452f06fb4',
      },
    },
    empty: {
      'SHA-384': {
        '1': '4bb042a5c28cee6f66f991c717fd7702677' +
             '87e2bb3031eae270d87d63ad99534',
        '1000': '9cbfe72d194da34e17c821dd1569ef50' +
                'a86eb4d893591776adc6a5c21e0031cf',
        '100000': 'ed6bd7282567abe48d542d067d09f4' +
                  '04bd044ae2cefe11dacc531c4764cd35cd',
      },
      'SHA-512': {
        '1': '6d2ecbbbfb2e6dcd7056faf9af6aa06eae5' +
             '94391db983279a6bf27e0eb228614',
        '1000': 'cb93096c3a02beeb1c5fac36765c9011' +
                'fe99f8d8ea62366048fc98cb98dfea8f',
        '100000': '89e16254ebad5cba72e0aebe1614c7' +
                  'f9b795a7505f2637206ce10a3449a2b8bb',
      },
      'SHA-1': {
        '1': '1e437a1c79d75be61e91141dae20affc489' +
             '2cc99abcc3fe753887bccc8920176',
        '1000': '6e40910ac02ec89cebb9d898b13a09d1' +
                'cd7adf6f8cc08cc473302c8973aa2e19',
        '100000': 'a9e1bebb36bc26d7c997d5483cbc8d' +
                  'e4a419d1e706571342632586ec330a7290',
      },
      'SHA-256': {
        '1': 'f7ce0b653d2d72a4108cf5abe912ffdd777' +
             '616dbbb27a70e8204f3ae2d0f6fad',
        '1000': '4fc58a21c100ce1835b8f9991d738b56' +
                '965d14b24e1761fbdffc69ac5e0b667a',
        '100000': '64a868d4b23af696d3734d0b814d04' +
                  'cdd1ac280128e97653a05f32b49c13a29a',
      },
    },
  },
};

async function setupBaseKeys() {
  const promises = [];

  const baseKeys = {};
  const noBits = {};
  const noKey = {};
  let wrongKey = null;

  Object.keys(kPasswords).forEach((size) => {
    promises.push(
      subtle.importKey(
        'raw',
        Buffer.from(kPasswords[size], 'hex'),
        { name: 'PBKDF2' },
        false,
        ['deriveKey', 'deriveBits'])
        .then((key) => baseKeys[size] = key));

    promises.push(
      subtle.importKey(
        'raw',
        Buffer.from(kPasswords[size], 'hex'),
        { name: 'PBKDF2' },
        false,
        ['deriveBits'])
        .then((key) => noKey[size] = key));

    promises.push(
      subtle.importKey(
        'raw',
        Buffer.from(kPasswords[size], 'hex'),
        { name: 'PBKDF2' },
        false,
        ['deriveKey'])
        .then((key) => noBits[size] = key));
  });

  promises.push(
    subtle.generateKey(
      { name: 'ECDH', namedCurve: 'P-521' },
      false,
      ['deriveKey', 'deriveBits'])
      .then((key) => wrongKey = key.privateKey));

  await Promise.all(promises);

  return { baseKeys, noBits, noKey, wrongKey };
}

async function testDeriveBits(
  baseKeys,
  size,
  saltSize,
  hash,
  iterations) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    hash,
    iterations,
  };

  const bits = await subtle.deriveBits(algorithm, baseKeys[size], 256);

  assert(bits instanceof ArrayBuffer);
  assert.strictEqual(
    Buffer.from(bits).toString('hex'),
    kDerivations[size][saltSize][hash][iterations]);
}

async function testDeriveBitsBadLengths(
  baseKeys,
  size,
  saltSize,
  hash,
  iterations) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    iterations,
    hash,
  };

  return Promise.all([
    assert.rejects(
      subtle.deriveBits(algorithm, baseKeys[size], undefined), {
        name: 'OperationError',
      }),
    assert.rejects(
      subtle.deriveBits(algorithm, baseKeys[size], null), {
        message: 'length cannot be null',
        name: 'OperationError',
      }),
    assert.rejects(
      subtle.deriveBits(algorithm, baseKeys[size]), {
        message: 'length cannot be null',
        name: 'OperationError',
      }),
    assert.rejects(
      subtle.deriveBits(algorithm, baseKeys[size], 15), {
        message: /length must be a multiple of 8/,
        name: 'OperationError',
      }),
  ]);
}

async function testDeriveBitsBadHash(
  baseKeys,
  size,
  saltSize,
  hash,
  iterations) {
  const salt = Buffer.from(kSalts[saltSize], 'hex');
  const algorithm = { name: 'PBKDF2', salt, iterations };

  return Promise.all([
    assert.rejects(
      subtle.deriveBits(
        {
          ...algorithm,
          hash: hash.substring(0, 3) + hash.substring(4),
        }, baseKeys[size], 256), {
        message: /Unrecognized algorithm name/,
        name: 'NotSupportedError',
      }),
    assert.rejects(
      subtle.deriveBits(
        {
          ...algorithm,
          hash: 'HKDF',
        },
        baseKeys[size], 256), {
        message: /Unrecognized algorithm name/,
        name: 'NotSupportedError',
      }),
  ]);
}

async function testDeriveBitsBadUsage(
  noBits,
  size,
  saltSize,
  hash,
  iterations) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    iterations,
    hash,
  };

  return assert.rejects(
    subtle.deriveBits(algorithm, noBits[size], 256), {
      message: /baseKey does not have deriveBits usage/,
    });
}


async function testDeriveKey(
  baseKeys,
  size,
  saltSize,
  hash,
  iterations,
  keyType,
  usages) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    hash,
    iterations,
  };

  const key = await subtle.deriveKey(
    algorithm,
    baseKeys[size],
    keyType,
    true,
    usages);

  const bits = await subtle.exportKey('raw', key);

  assert.strictEqual(
    Buffer.from(bits).toString('hex'),
    kDerivations[size][saltSize][hash][iterations]
      .slice(0, keyType.length / 4));
}

async function testDeriveKeyBadHash(
  baseKeys,
  size,
  saltSize,
  hash,
  iterations,
  keyType,
  usages) {
  const salt = Buffer.from(kSalts[saltSize], 'hex');
  const algorithm = { name: 'PBKDF2', salt, iterations };

  return Promise.all([
    assert.rejects(
      subtle.deriveKey(
        {
          ...algorithm,
          hash: hash.substring(0, 3) + hash.substring(4),
        },
        baseKeys[size],
        keyType,
        true,
        usages),
      {
        message: /Unrecognized algorithm name/,
        name: 'NotSupportedError',
      }),
    assert.rejects(
      subtle.deriveKey(
        {
          ...algorithm,
          hash: 'HKDF',
        },
        baseKeys[size],
        keyType,
        true,
        usages),
      {
        message: /Unrecognized algorithm name/,
        name: 'NotSupportedError',
      }),
  ]);
}

async function testDeriveKeyBadUsage(
  noKey,
  size,
  saltSize,
  hash,
  iterations,
  keyType,
  usages) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    iterations,
    hash,
  };

  return assert.rejects(
    subtle.deriveKey(algorithm, noKey[size], keyType, true, usages), {
      message: /baseKey does not have deriveKey usage/,
    });
}

async function testWrongKeyType(
  wrongKey,
  saltSize,
  hash,
  iterations,
  keyType,
  usages,
) {
  const algorithm = {
    name: 'PBKDF2',
    salt: Buffer.from(kSalts[saltSize], 'hex'),
    iterations,
    hash,
  };

  return assert.rejects(
    subtle.deriveKey(algorithm, wrongKey, keyType, true, usages), {
      message: /Key algorithm mismatch/,
    });
}

(async function() {
  const {
    baseKeys,
    noBits,
    noKey,
    wrongKey,
  } = await setupBaseKeys();

  const variations = [];

  Object.keys(kDerivations).forEach((size) => {
    Object.keys(kDerivations[size]).forEach((saltSize) => {
      Object.keys(kDerivations[size][saltSize]).forEach((hash) => {
        Object.keys(kDerivations[size][saltSize][hash])
          .forEach((iterations) => {
            const args = [baseKeys, size, saltSize, hash, iterations | 0];
            variations.push(testDeriveBits(...args));
            variations.push(testDeriveBitsBadLengths(...args));
            variations.push(testDeriveBitsBadHash(...args));
            variations.push(
              testDeriveBitsBadUsage(
                noBits,
                size,
                saltSize,
                hash,
                iterations | 0));

            kDerivedKeyTypes.forEach((keyType) => {
              const keyArgs = getDeriveKeyInfo(...keyType);
              variations.push(testDeriveKey(...args, ...keyArgs));
              variations.push(testDeriveKeyBadHash(...args, ...keyArgs));
              variations.push(
                testDeriveKeyBadUsage(
                  noKey,
                  size,
                  saltSize,
                  hash,
                  iterations | 0,
                  ...keyArgs));
              variations.push(
                testWrongKeyType(
                  wrongKey,
                  saltSize,
                  hash,
                  iterations,
                  ...keyArgs));
            });
          });
      });
    });
  });

  await Promise.all(variations);
})().then(common.mustCall());


// https://github.com/w3c/webcrypto/pull/380
{
  crypto.subtle.importKey('raw', new Uint8Array(0), 'PBKDF2', false, ['deriveBits']).then((key) => {
    return crypto.subtle.deriveBits({
      name: 'PBKDF2',
      hash: { name: 'SHA-256' },
      iterations: 10,
      salt: new Uint8Array(0),
    }, key, 0);
  }).then((bits) => {
    assert.deepStrictEqual(bits, new ArrayBuffer(0));
  })
  .then(common.mustCall());
}
