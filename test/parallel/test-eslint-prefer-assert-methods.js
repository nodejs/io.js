'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-assert-methods');

new RuleTester().run('prefer-assert-methods', rule, {
  valid: [
    'assert.strictEqual(foo, bar);',
    'assert(foo === bar && baz);',
    'assert.notStrictEqual(foo, bar);',
    'assert(foo !== bar && baz);',
    'assert.equal(foo, bar);',
    'assert(foo == bar && baz);',
    'assert.notEqual(foo, bar);',
    'assert(foo != bar && baz);',
    'assert.ok(foo);',
    'assert.ok(foo != bar);',
    'assert.ok(foo === bar && baz);',
  ],
  invalid: [
    {
      code: 'assert(foo == bar);',
      errors: [{
        message: "'assert.equal' should be used instead of '=='"
      }],
      output: 'assert.equal(foo, bar);'
    },
    {
      code: 'assert(foo === bar);',
      errors: [{
        message: "'assert.strictEqual' should be used instead of '==='"
      }],
      output: 'assert.strictEqual(foo, bar);'
    },
    {
      code: 'assert(foo != bar);',
      errors: [{
        message: "'assert.notEqual' should be used instead of '!='"
      }],
      output: 'assert.notEqual(foo, bar);'
    },
    {
      code: 'assert(foo !== bar);',
      errors: [{
        message: "'assert.notStrictEqual' should be used instead of '!=='"
      }],
      output: 'assert.notStrictEqual(foo, bar);'
    },
  ]
});
