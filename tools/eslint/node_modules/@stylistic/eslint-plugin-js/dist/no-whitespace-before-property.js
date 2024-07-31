'use strict';

var utils = require('./utils.js');

var noWhitespaceBeforeProperty = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Disallow whitespace before properties",
      url: "https://eslint.style/rules/js/no-whitespace-before-property"
    },
    fixable: "whitespace",
    schema: [],
    messages: {
      unexpectedWhitespace: "Unexpected whitespace before property {{propName}}."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    function reportError(node, leftToken, rightToken) {
      context.report({
        node,
        messageId: "unexpectedWhitespace",
        data: {
          propName: sourceCode.getText(node.property)
        },
        fix(fixer) {
          let replacementText = "";
          if (!node.computed && !node.optional && utils.isDecimalInteger(node.object)) {
            return null;
          }
          if (sourceCode.commentsExistBetween(leftToken, rightToken))
            return null;
          if (node.optional)
            replacementText = "?.";
          else if (!node.computed)
            replacementText = ".";
          return fixer.replaceTextRange([leftToken.range[1], rightToken.range[0]], replacementText);
        }
      });
    }
    return {
      MemberExpression(node) {
        let rightToken;
        let leftToken;
        if (!utils.isTokenOnSameLine(node.object, node.property))
          return;
        if (node.computed) {
          rightToken = sourceCode.getTokenBefore(node.property, utils.isOpeningBracketToken);
          leftToken = sourceCode.getTokenBefore(rightToken, node.optional ? 1 : 0);
        } else {
          rightToken = sourceCode.getFirstToken(node.property);
          leftToken = sourceCode.getTokenBefore(rightToken, 1);
        }
        if (sourceCode.isSpaceBetweenTokens(leftToken, rightToken))
          reportError(node, leftToken, rightToken);
      }
    };
  }
});

exports.noWhitespaceBeforeProperty = noWhitespaceBeforeProperty;
