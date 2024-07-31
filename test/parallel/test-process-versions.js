'use strict';
const common = require('../common');
const assert = require('assert');

// Import of pure js (non-shared) deps for comparison
const acorn = require('../../deps/acorn/acorn/package.json');
const cjs_module_lexer = require('../../deps/cjs-module-lexer/package.json');

const expected_keys = [
  'ares',
  'brotli',
  'modules',
  'uv',
  'v8',
  'zlib',
  'nghttp2',
  'napi',
  'llhttp',
  'uvwasi',
  'acorn',
  'simdjson',
  'simdutf',
  'sqlite',
  'ada',
  'cjs_module_lexer',
  'nbytes',
  'amaro',
];

const hasUndici = process.config.variables.node_builtin_shareable_builtins.includes('deps/undici/undici.js');

if (hasUndici) {
  expected_keys.push('undici');
}

if (common.hasCrypto) {
  expected_keys.push('openssl');
  expected_keys.push('ncrypto');
}

if (common.hasQuic) {
  expected_keys.push('ngtcp2');
  expected_keys.push('nghttp3');
}

if (common.hasIntl) {
  expected_keys.push('icu');
  expected_keys.push('cldr');
  expected_keys.push('tz');
  expected_keys.push('unicode');
}

expected_keys.sort();
expected_keys.unshift('node');

const actual_keys = Object.keys(process.versions);

assert.deepStrictEqual(actual_keys, expected_keys);

const commonTemplate = /^\d+\.\d+\.\d+(?:-.*)?$/;

assert.match(process.versions.acorn, commonTemplate);
assert.match(process.versions.ares, commonTemplate);
assert.match(process.versions.brotli, commonTemplate);
assert.match(process.versions.llhttp, commonTemplate);
assert.match(process.versions.node, commonTemplate);
assert.match(process.versions.uv, commonTemplate);
assert.match(process.versions.nbytes, commonTemplate);
assert.match(process.versions.zlib, /^\d+(?:\.\d+){1,3}(?:-.*)?$/);

if (hasUndici) {
  assert.match(process.versions.undici, commonTemplate);
}

assert.match(
  process.versions.v8,
  /^\d+\.\d+\.\d+(?:\.\d+)?-node\.\d+(?: \(candidate\))?$/
);
assert.match(process.versions.modules, /^\d+$/);
assert.match(process.versions.cjs_module_lexer, commonTemplate);

if (common.hasCrypto) {
  assert.match(process.versions.ncrypto, commonTemplate);
  if (process.config.variables.node_shared_openssl) {
    assert.ok(process.versions.openssl);
  } else {
    const versionRegex = common.hasOpenSSL3 ?
      // The following also matches a development version of OpenSSL 3.x which
      // can be in the format '3.0.0-alpha4-dev'. This can be handy when
      // building and linking against the main development branch of OpenSSL.
      /^\d+\.\d+\.\d+(?:[-+][a-z0-9]+)*$/ :
      /^\d+\.\d+\.\d+[a-z]?(\+quic)?(-fips)?$/;
    assert.match(process.versions.openssl, versionRegex);
  }
}

for (let i = 0; i < expected_keys.length; i++) {
  const key = expected_keys[i];
  const descriptor = Object.getOwnPropertyDescriptor(process.versions, key);
  assert.strictEqual(descriptor.writable, false);
}

assert.strictEqual(process.config.variables.napi_build_version,
                   process.versions.napi);

if (hasUndici) {
  const undici = require('../../deps/undici/src/package.json');
  const expectedUndiciVersion = undici.version;
  assert.strictEqual(process.versions.undici, expectedUndiciVersion);
}

const expectedAcornVersion = acorn.version;
assert.strictEqual(process.versions.acorn, expectedAcornVersion);
const expectedCjsModuleLexerVersion = cjs_module_lexer.version;
assert.strictEqual(process.versions.cjs_module_lexer, expectedCjsModuleLexerVersion);
