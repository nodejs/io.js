/**
 * @fileoverview Require `common` module first in our tests.
 */
'use strict';

const path = require('path');
const { isRequireCall, isString } = require('./rules-utils.js');

//------------------------------------------------------------------------------
// Rule Definition
//------------------------------------------------------------------------------

module.exports = {
  create(context) {
    const requiredModule = 'common';
    const isESM = context.languageOptions.sourceType === 'module';
    const foundModules = [];

    /**
     * Function to check if the path is a module and return its name.
     * @param {string} str The path to check
     * @returns {string} module name
     */
    function getModuleName(str) {
      if (str.startsWith('../') && str.endsWith('/common/index.mjs')) {
        return 'common';
      }

      return path.basename(str);
    }

    /**
     * Function to check if a node has an argument that is a module and
     * return its name.
     * @param {ASTNode} node The node to check
     * @returns {undefined | string} module name or undefined
     */
    function getModuleNameFromCall(node) {
    // Node has arguments and first argument is string
      if (node.arguments.length && isString(node.arguments[0])) {
        return getModuleName(node.arguments[0].value.trim());
      }

      return undefined;
    }

    const rules = {
      'Program:exit'(node) {
      // The common module should be loaded in the first place.
        const notLoadedFirst = foundModules.indexOf(requiredModule) !== 0;
        if (notLoadedFirst) {
          context.report(
            node,
            'Mandatory module "{{moduleName}}" must be loaded ' +
          'before any other modules.',
            { moduleName: requiredModule },
          );
        }
      },
    };

    if (isESM) {
      rules.ImportDeclaration = (node) => {
        const moduleName = getModuleName(node.source.value);
        if (moduleName) {
          foundModules.push(moduleName);
        }
      };
    } else {
      rules.CallExpression = (node) => {
        if (isRequireCall(node)) {
          const moduleName = getModuleNameFromCall(node);

          if (moduleName) {
            foundModules.push(moduleName);
          }
        }
      };
    }

    return rules;
  },
};
