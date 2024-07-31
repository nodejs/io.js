/**
 * @fileoverview Ensure modules are not required twice at top level of a module
 * @author devsnek
 */
'use strict';

const { isRequireCall, isString } = require('./rules-utils.js');

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

const secondLevelTypes = [
  'FunctionDeclaration', 'FunctionExpression', 'ArrowFunctionExpression',
  'ClassBody', 'MethodDefinition',
];

function isTopLevel(node) {
  while (!secondLevelTypes.includes(node.type)) {
    node = node.parent;
    if (!node) {
      return true;
    }
  }
  return false;
}

module.exports = {
  create(context) {
    if (context.languageOptions.sourceType === 'module') {
      return {};
    }

    function getRequiredModuleNameFromCall(node) {
      // Node has arguments and first argument is string
      if (node.arguments.length && isString(node.arguments[0])) {
        return node.arguments[0].value.trim();
      }

      return undefined;
    }

    const required = new Set();

    const rules = {
      CallExpression: (node) => {
        if (isRequireCall(node) && isTopLevel(node)) {
          const moduleName = getRequiredModuleNameFromCall(node);
          if (moduleName === undefined) {
            return;
          }
          if (required.has(moduleName)) {
            context.report(
              node,
              '\'{{moduleName}}\' require is duplicated.',
              { moduleName },
            );
          } else {
            required.add(moduleName);
          }
        }
      },
    };

    return rules;
  },
};
