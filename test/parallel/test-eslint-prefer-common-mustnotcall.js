'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-common-mustnotcall');

const message = 'Please use common.mustNotCall(msg) instead of ' +
                'common.mustCall(fn, 0) or common.mustCall(0).';

new RuleTester().run('prefer-common-mustnotcall', rule, {
  valid: [
    'common.mustNotCall(fn)',
    'common.mustCall(fn)',
    'common.mustCall(fn, 1)',
  ],
  invalid: [
    {
      code: 'common.mustCall(fn, 0)',
      errors: [{ message }]
    },
    {
      code: 'common.mustCall(0)',
      errors: [{ message }]
    },
  ]
});
