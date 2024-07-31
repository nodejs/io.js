/**
 * @fileoverview null objects can be created with `ObjectCreate(null)`, or with
 *               syntax: `{ __proto__: null }`. This linter rule forces use of
 *               syntax over ObjectCreate.
 * @author Jordan Harband <ljharb@gmail.com>
 */
'use strict';

// Cribbed from `eslint-module-utils/declaredScope`
function declaredScope(context, node, name) {
  const references = context.sourceCode.getScope(node).references;
  const reference = references.find((x) => x.identifier.name === name);
  if (!reference) return undefined;
  return reference.resolved.scope.type;
}

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

module.exports = {
  meta: {
    fixable: 'code',
    messages: {
      error: 'Use `{ __proto__: {{value}} }` instead of `ObjectCreate({{value}})` or `Object.create({{value}})`.',
    },
  },
  create(context) {
    return {
      /* eslint @stylistic/js/max-len: 0 */
      'CallExpression[arguments.length=1]:matches(\
        [callee.type="Identifier"][callee.name="ObjectCreate"],\
        [callee.type="MemberExpression"][callee.object.name="Object"][callee.property.name="create"]\
      )'(node) {
        if (node.callee.type === 'MemberExpression') {
          const scope = declaredScope(context, node, node.callee.object);
          if (scope && scope !== 'module' && scope !== 'global') {
            return;
          }
        }
        const value = context.sourceCode.getText(node.arguments[0]);
        context.report({
          node,
          messageId: 'error',
          data: { value },
          fix(fixer) {
            return fixer.replaceText(node, `{ __proto__: ${value} }`);
          },
        });
      },
    };
  },
};
