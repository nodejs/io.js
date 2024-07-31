'use strict';

const common = require('../common');
if ((!common.hasCrypto) || (!common.hasIntl)) {
  common.skip('ESLint tests require crypto and Intl');
}
common.skipIfEslintMissing();

const RuleTester = require('../../tools/eslint/node_modules/eslint').RuleTester;
const rule = require('../../tools/eslint-rules/inspector-check');

const message = 'Please add a skipIfInspectorDisabled() call to allow this ' +
                'test to be skipped when Node is built ' +
                '\'--without-inspector\'.';

new RuleTester().run('inspector-check', rule, {
  valid: [
    'foo;',
    'require("common")\n' +
      'common.skipIfInspectorDisabled();\n' +
      'require("inspector")',
  ],
  invalid: [
    {
      code: 'require("common")\n' +
            'require("inspector")',
      errors: [{ message }],
      output: 'require("common")\n' +
              'common.skipIfInspectorDisabled();\n' +
              'require("inspector")'
    },
  ]
});
