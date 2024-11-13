'use strict';

require('../common');
const assert = require('assert');

const actualKeys = new Set(Object.keys(process.features));
const expectedKeys = new Map([
  ['inspector', ['boolean']],
  ['debug', ['boolean']],
  ['uv', ['boolean']],
  ['ipv6', ['boolean']],
  ['tls_alpn', ['boolean']],
  ['tls_sni', ['boolean']],
  ['tls_ocsp', ['boolean']],
  ['tls', ['boolean']],
  ['cached_builtins', ['boolean']],
  ['require_module', ['boolean']],
  ['typescript', ['boolean', 'string']],
]);

assert.deepStrictEqual(actualKeys, new Set(expectedKeys.keys()));

for (const [key, expected] of expectedKeys) {
  assert.ok(expected.includes(typeof process.features[key]), `typeof process.features.${key} is not one of [${expected.join(', ')}]`);
}
