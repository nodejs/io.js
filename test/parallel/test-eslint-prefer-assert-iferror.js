'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-assert-iferror');

new RuleTester().run('prefer-assert-iferror', rule, {
  valid: [
    'assert.ifError(err);',
    'if (err) throw somethingElse;',
    'throw err;',
    'if (err) { throw somethingElse; }',
  ],
  invalid: [
    {
      code: 'require("assert");\n' +
            'if (err) throw err;',
      errors: [{ message: 'Use assert.ifError(err) instead.' }],
      output: 'require("assert");\n' +
              'assert.ifError(err);'
    },
    {
      code: 'require("assert");\n' +
            'if (error) { throw error; }',
      errors: [{ message: 'Use assert.ifError(error) instead.' }],
      output: 'require("assert");\n' +
              'assert.ifError(error);'
    },
  ]
});
