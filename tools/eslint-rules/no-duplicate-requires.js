/**
 * @fileoverview Ensure modules are not required twice at top level of a module
 * @author devsnek
 * @author RedYetiDev
 */
'use strict';

const { isRequireCall, isString } = require('./rules-utils.js');

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

const topLevelTypes = new Set([
  'FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression',
  'ClassBody', 'MethodDefinition',
]);

const isTopLevel = (node) => {
  while (node) {
    if (topLevelTypes.has(node.type)) return false;
    node = node.parent;
  }
  return true;
};

module.exports = {
  create(context) {
    if (context.languageOptions.sourceType === 'module') {
      return {};
    }

    const requiredModules = new Set();

    return {
      CallExpression(node) {
        if (isRequireCall(node)) {
          const [firstArg] = node.arguments;
          if (isString(firstArg)) {
            const moduleName = firstArg.value.trim();
            if (requiredModules.has(moduleName)) {
              context.report({
                node,
                message: `'${moduleName}' require is duplicated.`,
              });
            } else if (isTopLevel(node)) {
              requiredModules.add(moduleName);
            }
          }
        }
      },
    };
  },
};
