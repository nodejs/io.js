'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/non-ascii-character');

new RuleTester().run('non-ascii-characters', rule, {
  valid: [
    {
      code: 'console.log("fhqwhgads")',
      options: []
    },
  ],
  invalid: [
    {
      code: 'console.log("μ")',
      options: [],
      errors: [{ message: "Non-ASCII character 'μ' detected." }],
    },
  ]
});
