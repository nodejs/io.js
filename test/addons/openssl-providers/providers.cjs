'use strict';

const common = require('../../common');
if (!common.hasCrypto) {
  common.skip('missing crypto');
}
const { hasOpenSSL3 } = require('../../common/crypto');

if (!hasOpenSSL3) {
  common.skip('this test requires OpenSSL 3.x');
}
const assert = require('node:assert');
const { createHash, getCiphers, getHashes } = require('node:crypto');
const { debuglog } = require('node:util');
const { getProviders } = require(`./build/${common.buildType}/binding`);

// For the providers defined here, the expectation is that the listed ciphers
// and hash algorithms are only provided by the named provider. These are for
// basic checks and are not intended to list every cipher or hash algorithm
// supported by the provider.
const providers = {
  'default': {
    ciphers: ['des3-wrap'],
    hashes: ['sha512-256'],
  },
  'legacy': {
    ciphers: ['blowfish', 'idea'],
    hashes: ['md4', 'whirlpool'],
  },
};

const debug = debuglog('test');

module.exports = {
  getCurrentProviders: getProviders,
  testProviderPresent,
  testProviderAbsent,
};

function assertArrayDoesNotInclude(array, item, desc) {
  assert(!array.includes(item),
         `${desc} [${array}] unexpectedly includes "${item}"`);
}

function assertArrayIncludes(array, item, desc) {
  assert(array.includes(item),
         `${desc} [${array}] does not include "${item}"`);
}

function testProviderPresent(provider) {
  debug(`Checking '${provider}' is present`);
  assertArrayIncludes(getProviders(), provider, 'Loaded providers');
  for (const cipher of providers[provider].ciphers || []) {
    debug(`Checking '${cipher}' cipher is available`);
    assertArrayIncludes(getCiphers(), cipher, 'Available ciphers');
  }
  for (const hash of providers[provider].hashes || []) {
    debug(`Checking '${hash}' hash is available`);
    assertArrayIncludes(getHashes(), hash, 'Available hashes');
    createHash(hash);
  }
}

function testProviderAbsent(provider) {
  debug(`Checking '${provider}' is absent`);
  assertArrayDoesNotInclude(getProviders(), provider, 'Loaded providers');
  for (const cipher of providers[provider].ciphers || []) {
    debug(`Checking '${cipher}' cipher is unavailable`);
    assertArrayDoesNotInclude(getCiphers(), cipher, 'Available ciphers');
  }
  for (const hash of providers[provider].hashes || []) {
    debug(`Checking '${hash}' hash is unavailable`);
    assertArrayDoesNotInclude(getHashes(), hash, 'Available hashes');
    assert.throws(() => { createHash(hash); }, { code: 'ERR_OSSL_EVP_UNSUPPORTED' });
  }
}
