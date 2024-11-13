'use strict';

const {
  ObjectKeys,
  StringPrototypeToLowerCase,
} = primordials;

const kHashContextNode = 1;
const kHashContextWebCrypto = 2;
const kHashContextJwkRsa = 3;
const kHashContextJwkRsaPss = 4;
const kHashContextJwkRsaOaep = 5;
const kHashContextJwkHmac = 6;

// WebCrypto and JWK use a bunch of different names for the
// standard set of SHA-* digest algorithms... which is ... fun.
// Here we provide a utility for mapping between them in order
// make it easier in the code.

const kHashNames = {
  sha1: {
    [kHashContextNode]: 'sha1',
    [kHashContextWebCrypto]: 'SHA-1',
    [kHashContextJwkRsa]: 'RS1',
    [kHashContextJwkRsaPss]: 'PS1',
    [kHashContextJwkRsaOaep]: 'RSA-OAEP',
    [kHashContextJwkHmac]: 'HS1',
  },
  sha256: {
    [kHashContextNode]: 'sha256',
    [kHashContextWebCrypto]: 'SHA-256',
    [kHashContextJwkRsa]: 'RS256',
    [kHashContextJwkRsaPss]: 'PS256',
    [kHashContextJwkRsaOaep]: 'RSA-OAEP-256',
    [kHashContextJwkHmac]: 'HS256',
  },
  sha384: {
    [kHashContextNode]: 'sha384',
    [kHashContextWebCrypto]: 'SHA-384',
    [kHashContextJwkRsa]: 'RS384',
    [kHashContextJwkRsaPss]: 'PS384',
    [kHashContextJwkRsaOaep]: 'RSA-OAEP-384',
    [kHashContextJwkHmac]: 'HS384',
  },
  sha512: {
    [kHashContextNode]: 'sha512',
    [kHashContextWebCrypto]: 'SHA-512',
    [kHashContextJwkRsa]: 'RS512',
    [kHashContextJwkRsaPss]: 'PS512',
    [kHashContextJwkRsaOaep]: 'RSA-OAEP-512',
    [kHashContextJwkHmac]: 'HS512',
  },
};

{
  // Index the aliases
  const keys = ObjectKeys(kHashNames);
  for (let n = 0; n < keys.length; n++) {
    const contexts = ObjectKeys(kHashNames[keys[n]]);
    for (let i = 0; i < contexts.length; i++) {
      const alias =
        StringPrototypeToLowerCase(kHashNames[keys[n]][contexts[i]]);
      if (kHashNames[alias] === undefined)
        kHashNames[alias] = kHashNames[keys[n]];
    }
  }
}

function normalizeHashName(name, context = kHashContextNode) {
  if (typeof name !== 'string')
    return name;
  name = StringPrototypeToLowerCase(name);
  const alias = kHashNames[name]?.[context];
  return alias || name;
}

normalizeHashName.kContextNode = kHashContextNode;
normalizeHashName.kContextWebCrypto = kHashContextWebCrypto;
normalizeHashName.kContextJwkRsa = kHashContextJwkRsa;
normalizeHashName.kContextJwkRsaPss = kHashContextJwkRsaPss;
normalizeHashName.kContextJwkRsaOaep = kHashContextJwkRsaOaep;
normalizeHashName.kContextJwkHmac = kHashContextJwkHmac;

module.exports = normalizeHashName;
