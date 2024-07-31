'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}
common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/alphabetize-errors');

new RuleTester().run('alphabetize-errors', rule, {
  valid: [
    { code: `
      E('AAA', 'foo');
      E('BBB', 'bar');
      E('CCC', 'baz');
    `, options: [{ checkErrorDeclarations: true }] },
    `
      E('AAA', 'foo');
      E('CCC', 'baz');
      E('BBB', 'bar');
    `,
    `const {
      codes: {
        ERR_A,
        ERR_B,
      },
    } = require("internal/errors")`,
  ],
  invalid: [
    {
      code: `
        E('BBB', 'bar');
        E('AAA', 'foo');
        E('CCC', 'baz');
      `,
      options: [{ checkErrorDeclarations: true }],
      errors: [{ message: 'Out of ASCIIbetical order - BBB >= AAA', line: 3 }]
    },
    {
      code: `const {
        codes: {
          ERR_B,
          ERR_A,
        },
      } = require("internal/errors")`,
      errors: [{ message: 'Out of ASCIIbetical order - ERR_B >= ERR_A', line: 4 }]
    },
    {
      code: 'const internalErrors = require("internal/errors")',
      errors: [{ message: /Use destructuring/ }]
    },
    {
      code: 'const {codes} = require("internal/errors")',
      errors: [{ message: /Use destructuring/ }]
    },
    {
      code: 'const {codes:{ERR_A}} = require("internal/errors")',
      errors: [{ message: /Use multiline destructuring/ }]
    },
  ]
});
