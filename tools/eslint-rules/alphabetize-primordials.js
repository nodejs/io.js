'use strict';

const prefix = 'Out of ASCIIbetical order - ';
const opStr = ' >= ';

module.exports = {
  meta: {
    schema: [{
      type: 'object',
      properties: {
        enforceTopPosition: { type: 'boolean' },
      },
      additionalProperties: false,
    }],
  },
  create: function(context) {
    const enforceTopPosition = context.options.every((option) => option.enforceTopPosition !== false);
    return {
      'VariableDeclaration:not(Program>:first-child+*) > VariableDeclarator[init.name="primordials"]'(node) {
        if (enforceTopPosition) {
          context.report({
            node,
            message: 'destructuring from primordials should be the first expression after "use strict"',
          });
        }
      },

      'VariableDeclaration > VariableDeclarator[init.name="primordials"]'({ id: node }) {
        const { loc } = node;
        if (loc.start.line === loc.end.line) {
          context.report({
            node,
            message: 'destructuring from primordials should be multiline',
          });
        }
      },

      'VariableDeclaration > VariableDeclarator[init.name="primordials"] Property:not(:first-child)'(node) {
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
