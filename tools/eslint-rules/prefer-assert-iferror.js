/**
 * @fileoverview Prohibit the `if (err) throw err;` pattern
 * @author Teddy Katz
 */

'use strict';

const utils = require('./rules-utils.js');

module.exports = {
  meta: {
    fixable: 'code',
  },
  create(context) {
    const sourceCode = context.sourceCode;
    let assertImported = false;

    function hasSameTokens(nodeA, nodeB) {
      const aTokens = sourceCode.getTokens(nodeA);
      const bTokens = sourceCode.getTokens(nodeB);

      return aTokens.length === bTokens.length &&
        aTokens.every((token, index) => {
          return token.type === bTokens[index].type &&
            token.value === bTokens[index].value;
        });
    }

    function checkAssertNode(node) {
      if (utils.isRequired(node, ['assert'])) {
        assertImported = true;
      }
    }

    return {
      'CallExpression': (node) => checkAssertNode(node),
      'IfStatement': (node) => {
        const firstStatement = node.consequent.type === 'BlockStatement' ?
          node.consequent.body[0] :
          node.consequent;
        if (
          firstStatement &&
          firstStatement.type === 'ThrowStatement' &&
          hasSameTokens(node.test, firstStatement.argument)
        ) {
          const argument = sourceCode.getText(node.test);
          context.report({
            node: firstStatement,
            message: 'Use assert.ifError({{argument}}) instead.',
            data: { argument },
            fix: (fixer) => {
              if (assertImported) {
                return fixer.replaceText(
                  node,
                  `assert.ifError(${argument});`,
                );
              }
            },
          });
        }
      },
    };
  },
};
