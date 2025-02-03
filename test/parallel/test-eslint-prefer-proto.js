'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/prefer-proto');

new RuleTester().run('prefer-common-mustsucceed', rule, {
  valid: [
    '({ __proto__: null })',
    'const x = { __proto__: null };',
    `
        class X {
            field = { __proto__: null };

            constructor() {
                this.x = { __proto__: X };
            }
        }
    `,
    'foo({ __proto__: Array.prototype })',
    '({ "__proto__": null })',
    "({ '__proto__': null })",
    'ObjectCreate(null, undefined)',
    'Object.create(null, undefined)',
    'Object.create(null, undefined, undefined)',
    'ObjectCreate(null, descriptors)',
    'Object.create(null, descriptors)',
    'ObjectCreate(Foo.prototype, descriptors)',
    'Object.create(Foo.prototype, descriptors)',
  ],
  invalid: [
    {
      code: 'ObjectCreate(null)',
      output: '{ __proto__: null }',
      errors: [{ messageId: 'error', data: { value: 'null' } }],
    },
    {
      code: 'Object.create(null)',
      output: '{ __proto__: null }',
      errors: [{ messageId: 'error', data: { value: 'null' } }],
    },
    {
      code: 'ObjectCreate(null,)',
      output: '{ __proto__: null }',
      errors: [{ messageId: 'error', data: { value: 'null' } }],
    },
    {
      code: 'Object.create(null,)',
      output: '{ __proto__: null }',
      errors: [{ messageId: 'error', data: { value: 'null' } }],
    },
    {
      code: 'ObjectCreate(Foo)',
      output: '{ __proto__: Foo }',
      errors: [{ messageId: 'error', data: { value: 'Foo' } }],
    },
    {
      code: 'Object.create(Foo)',
      output: '{ __proto__: Foo }',
      errors: [{ messageId: 'error', data: { value: 'Foo' } }],
    },
  ]
});
