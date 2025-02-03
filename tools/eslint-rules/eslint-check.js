/**
 * @fileoverview Check that common.skipIfEslintMissing is used if
 *               the eslint module is required.
 */
'use strict';

const utils = require('./rules-utils.js');

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------
const msg = 'Please add a skipIfEslintMissing() call to allow this test to ' +
            'be skipped when Node.js is built from a source tarball.';

module.exports = {
  meta: {
    fixable: 'code',
  },
  create: function(context) {
    const missingCheckNodes = [];
    let commonModuleNode = null;
    let hasEslintCheck = false;

    function testEslintUsage(context, node) {
      if (utils.isRequired(node, ['../../tools/eslint/node_modules/eslint'])) {
        missingCheckNodes.push(node);
      }

      if (utils.isCommonModule(node)) {
        commonModuleNode = node;
      }
    }

    function checkMemberExpression(context, node) {
      if (utils.usesCommonProperty(node, ['skipIfEslintMissing'])) {
        hasEslintCheck = true;
      }
    }

    function reportIfMissing(context) {
      if (!hasEslintCheck) {
        missingCheckNodes.forEach((node) => {
          context.report({
            node,
            message: msg,
            fix: (fixer) => {
              if (commonModuleNode) {
                return fixer.insertTextAfter(
                  commonModuleNode,
                  '\ncommon.skipIfEslintMissing();',
                );
              }
            },
          });
        });
      }
    }

    return {
      'CallExpression': (node) => testEslintUsage(context, node),
      'MemberExpression': (node) => checkMemberExpression(context, node),
      'Program:exit': () => reportIfMissing(context),
    };
  },
};
