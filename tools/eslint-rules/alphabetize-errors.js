'use strict';

const { isDefiningError } = require('./rules-utils.js');

const prefix = 'Out of ASCIIbetical order - ';
const opStr = ' >= ';

function errorForNode(node) {
  return node.expression.arguments[0].value;
}

const requireInternalErrorsSelector =
 'VariableDeclarator' +
   '[init.type="CallExpression"]' +
   '[init.callee.name="require"]' +
   '[init.arguments.length=1]' +
   '[init.arguments.0.value="internal/errors"]';
const codesSelector = requireInternalErrorsSelector + '>*>Property[key.name="codes"]';

module.exports = {
  meta: {
    schema: [{
      type: 'object',
      properties: {
        checkErrorDeclarations: { type: 'boolean' },
      },
      additionalProperties: false,
    }],
  },
  create: function(context) {
    let previousNode;
    return {
      ExpressionStatement: function(node) {
        if (context.options.every((option) => option.checkErrorDeclarations !== true) || !isDefiningError(node)) return;
        if (!previousNode) {
          previousNode = node;
          return;
        }
        const prev = errorForNode(previousNode);
        const curr = errorForNode(node);
        previousNode = node;
        if (prev >= curr) {
          const message = [prefix, prev, opStr, curr].join('');
          context.report({ node, message });
        }
      },

      [requireInternalErrorsSelector](node) {
        if (node.id.type !== 'ObjectPattern') {
          context.report({ node, message: 'Use destructuring to define error codes used in the file' });
        }
      },

      [codesSelector](node) {
        if (node.value.type !== 'ObjectPattern') {
          context.report({ node, message: 'Use destructuring to define error codes used in the file' });
          return;
        }
        if (node.value.loc.start.line === node.value.loc.end.line) {
          context.report({ node, message: 'Use multiline destructuring for error codes' });
        }
      },

      [requireInternalErrorsSelector + ' Property:not(:first-child)'](node) {
        const { properties } = node.parent;
        const prev = properties[properties.indexOf(node) - 1].key.name;
        const curr = node.key.name;
        if (prev >= curr) {
          const message = [prefix, prev, opStr, curr].join('');
          context.report({ node, message });
        }
      },
    };
  },
};
