'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-optional-chaining');

new RuleTester().run('prefer-optional-chaining', rule, {
  valid: [
    {
      code: 'hello?.world',
      options: []
    },
  ],
  invalid: [
    {
      code: 'hello && hello.world',
      options: [],
      errors: [{ message: 'Prefer optional chaining.' }],
      output: 'hello?.world'
    },
    {
      code: 'hello && hello.world && hello.world.foobar',
      options: [],
      errors: [{ message: 'Prefer optional chaining.' }],
      output: 'hello?.world?.foobar'
    },
  ]
});
