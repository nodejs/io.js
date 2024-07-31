'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}

common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/crypto-check');

const message = 'Please add a hasCrypto check to allow this test to be ' +
                'skipped when Node is built "--without-ssl".';

new RuleTester().run('crypto-check', rule, {
  valid: [
    'foo',
    'crypto',
    `
    if (!common.hasCrypto) {
      common.skip("missing crypto");
    }
    require("crypto");
    `,
    `
    if (!common.hasCrypto) {
      common.skip("missing crypto");
    }
    internalBinding("crypto");
    `,
  ],
  invalid: [
    {
      code: 'require("common")\n' +
            'require("crypto")\n' +
            'if (!common.hasCrypto) {\n' +
            '  common.skip("missing crypto");\n' +
            '}',
      errors: [{ message }]
    },
    {
      code: 'require("common")\n' +
            'require("crypto")',
      errors: [{ message }],
      output: 'require("common")\n' +
              'if (!common.hasCrypto) {' +
              ' common.skip("missing crypto");' +
              '}\n' +
              'require("crypto")'
    },
    {
      code: 'require("common")\n' +
            'if (common.foo) {}\n' +
            'require("crypto")',
      errors: [{ message }],
      output: 'require("common")\n' +
              'if (!common.hasCrypto) {' +
              ' common.skip("missing crypto");' +
              '}\n' +
              'if (common.foo) {}\n' +
              'require("crypto")'
    },
    {
      code: 'require("common")\n' +
            'if (common.foo) {}\n' +
            'internalBinding("crypto")',
      errors: [{ message }],
      output: 'require("common")\n' +
              'if (!common.hasCrypto) {' +
              ' common.skip("missing crypto");' +
              '}\n' +
              'if (common.foo) {}\n' +
              'internalBinding("crypto")'
    },
  ]
});
