'use strict';

const common = require('../common');
if (!common.hasCrypto)
  common.skip('missing crypto');
if (!common.hasIntl)
  common.skip('missing Intl');
common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/documented-deprecation-codes');

const mdFile = 'doc/api/deprecations.md';

const invalidCode = 'UNDOCUMENTED INVALID CODE';

new RuleTester().run('documented-deprecation-codes', rule, {
  valid: [
    `
    deprecate(function() {
        return this.getHeaders();
      }, 'OutgoingMessage.prototype._headers is deprecated', 'DEP0066')
    `,
  ],
  invalid: [
    {
      code: `
        deprecate(function foo(){}, 'bar', '${invalidCode}');
      `,
      errors: [
        {
          message: `"${invalidCode}" does not match the expected pattern`,
          line: 2
        },
        {
          message: `"${invalidCode}" is not documented in ${mdFile}`,
          line: 2
        },
      ]
    },
  ]
});
