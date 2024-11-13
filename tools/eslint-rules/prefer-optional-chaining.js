'use strict';

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

module.exports = {
  meta: {
    docs: {
      description: 'Prefer optional chaining',
      category: 'suggestion',
    },
    fixable: 'code',
    schema: [],
  },

  create(context) {
    const sourceCode = context.getSourceCode();

    // Helper function: Checks if two nodes have identical tokens
    function equalTokens(left, right) {
      const leftTokens = sourceCode.getTokens(left);
      const rightTokens = sourceCode.getTokens(right);
      return (
        leftTokens.length === rightTokens.length &&
        leftTokens.every((tokenL, i) => tokenL.type === rightTokens[i].type && tokenL.value === rightTokens[i].value)
      );
    }

    // Check if a sequence of two nodes forms a valid member expression chain
    function isValidMemberExpressionPair(left, right) {
      return (
        right.type === 'MemberExpression' &&
        equalTokens(left, right.object)
      );
    }

    // Generate the optional chaining expression
    function generateOptionalChaining(ops, first, last) {
      return ops.slice(first, last + 1).reduce((chain, node, i) => {
        const property = node.computed ?
          `[${sourceCode.getText(node.property)}]` :
          sourceCode.getText(node.property);
        return i === 0 ? sourceCode.getText(node) : `${chain}?.${property}`;
      }, '');
    }

    return {
      'LogicalExpression[operator=&&]:exit'(node) {
        // Early return if part of a larger `&&` chain
        if (node.parent.type === 'LogicalExpression' && node.parent.operator === '&&') {
          return;
        }

        const ops = [];
        let current = node;

        // Collect `&&` expressions into the ops array
        while (current.type === 'LogicalExpression' && current.operator === '&&') {
          ops.unshift(current.right); // Add right operand
          current = current.left;
        }
        ops.unshift(current); // Add the leftmost operand

        // Find the first valid member expression sequence
        let first = 0;
        while (first < ops.length - 1 && !isValidMemberExpressionPair(ops[first], ops[first + 1])) {
          first++;
        }

        // No valid sequence found
        if (first === ops.length - 1) return;

        context.report({
          node,
          message: 'Prefer optional chaining.',
          fix(fixer) {
            // Find the last valid member expression sequence
            let last = first;
            while (last < ops.length - 1 && isValidMemberExpressionPair(ops[last], ops[last + 1])) {
              last++;
            }

            return fixer.replaceTextRange(
              [ops[first].range[0], ops[last].range[1]],
              generateOptionalChaining(ops, first, last),
            );
          },
        });
      },
    };
  },
};
