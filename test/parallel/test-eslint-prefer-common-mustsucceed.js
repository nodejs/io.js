'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-common-mustsucceed');

const msg1 = 'Please use common.mustSucceed instead of ' +
             'common.mustCall(assert.ifError).';
const msg2 = 'Please use common.mustSucceed instead of ' +
             'common.mustCall with assert.ifError.';

new RuleTester().run('prefer-common-mustsucceed', rule, {
  valid: [
    'foo((err) => assert.ifError(err))',
    'foo(function(err) { assert.ifError(err) })',
    'foo(assert.ifError)',
    'common.mustCall((err) => err)',
  ],
  invalid: [
    {
      code: 'common.mustCall(assert.ifError)',
      errors: [{ message: msg1 }]
    },
    {
      code: 'common.mustCall((err) => assert.ifError(err))',
      errors: [{ message: msg2 }]
    },
    {
      code: 'common.mustCall((e) => assert.ifError(e))',
      errors: [{ message: msg2 }]
    },
    {
      code: 'common.mustCall(function(e) { assert.ifError(e); })',
      errors: [{ message: msg2 }]
    },
    {
      code: 'common.mustCall(function(e) { return assert.ifError(e); })',
      errors: [{ message: msg2 }]
    },
    {
      code: 'common.mustCall(function(e) {{ assert.ifError(e); }})',
      errors: [{ message: msg2 }]
    },
  ]
});
