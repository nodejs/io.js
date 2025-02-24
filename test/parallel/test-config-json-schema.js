// Flags: --no-warnings --expose-internals

'use strict';

const common = require('../common');

common.skipIfInspectorDisabled();

if (!common.hasCrypto) {
  common.skip('missing crypto');
}

const { hasOpenSSL3 } = require('../common/crypto');

if (!hasOpenSSL3) {
  common.skip('this test requires OpenSSL 3.x');
}

if (!common.hasIntl) {
  // A handful of the tests fail when ICU is not included.
  common.skip('missing Intl');
}

const {
  generateConfigJsonSchema,
} = require('internal/options');
const schemaInDoc = require('../../doc/node-config-schema.json');
const assert = require('assert');

const schema = generateConfigJsonSchema();

// This assertion ensures that whenever we add a new env option, we also add it
// to the JSON schema. The function getEnvOptionsInputType() returns all the available
// env options, so we can generate the JSON schema from it and compare it to the
// current JSON schema.
// To regenerate the JSON schema, run:
// out/Release/node --expose-internals tools/doc/generate-json-schema.mjs
// And then run make doc to update the out/doc/node-config-schema.json file.
assert.strictEqual(JSON.stringify(schema), JSON.stringify(schemaInDoc), 'JSON schema is outdated.' +
  'Run `out/Release/node --expose-internals tools/doc/generate-json-schema.mjs` to update it.');
