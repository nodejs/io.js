'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const { RuleTester } = require('../../tools/eslint/node_modules/eslint');
const rule = require('../../tools/eslint-rules/no-duplicate-requires');

new RuleTester({
  languageOptions: {
    sourceType: 'script',
  },
}).run('no-duplicate-requires', rule, {
  valid: [
    {
      code: 'require("a"); require("b"); (function() { require("a"); });',
    },
    {
      code: 'require(a); require(a);',
    },
  ],
  invalid: [
    {
      code: 'require("a"); require("a");',
      errors: [{ message: '\'a\' require is duplicated.' }],
    },
  ],
});
