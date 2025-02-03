'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/lowercase-name-for-primitive');

new RuleTester().run('lowercase-name-for-primitive', rule, {
  valid: [
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "a", ["string", "number"])',
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "name", "string")',
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "name", "number")',
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "name", "boolean")',
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "name", "null")',
    'new errors.TypeError("ERR_INVALID_ARG_TYPE", "name", "undefined")',
  ],
  invalid: [
    {
      code: "new errors.TypeError('ERR_INVALID_ARG_TYPE', 'a', 'Number')",
      errors: [{ message: 'primitive should use lowercase: Number' }],
      output: "new errors.TypeError('ERR_INVALID_ARG_TYPE', 'a', 'number')",
    },
    {
      code: "new errors.TypeError('ERR_INVALID_ARG_TYPE', 'a', 'STRING')",
      errors: [{ message: 'primitive should use lowercase: STRING' }],
      output: "new errors.TypeError('ERR_INVALID_ARG_TYPE', 'a', 'string')",
    },
    {
      code: "new e.TypeError('ERR_INVALID_ARG_TYPE', a, ['String','Number'])",
      errors: [
        { message: 'primitive should use lowercase: String' },
        { message: 'primitive should use lowercase: Number' },
      ],
      output: "new e.TypeError('ERR_INVALID_ARG_TYPE', a, ['string','number'])",
    },
  ]
});
