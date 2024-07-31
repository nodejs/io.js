'use strict';

var utils = require('./utils.js');

var spaceBeforeFunctionParen = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing before `function` definition opening parenthesis",
      url: "https://eslint.style/rules/js/space-before-function-paren"
    },
    fixable: "whitespace",
    schema: [
      {
        oneOf: [
          {
            type: "string",
            enum: ["always", "never"]
          },
          {
            type: "object",
            properties: {
              anonymous: {
                type: "string",
                enum: ["always", "never", "ignore"]
              },
              named: {
                type: "string",
                enum: ["always", "never", "ignore"]
              },
              asyncArrow: {
                type: "string",
                enum: ["always", "never", "ignore"]
              }
            },
            additionalProperties: false
          }
        ]
      }
    ],
    messages: {
      unexpectedSpace: "Unexpected space before function parentheses.",
      missingSpace: "Missing space before function parentheses."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const baseConfig = typeof context.options[0] === "string" ? context.options[0] : "always";
    const overrideConfig = typeof context.options[0] === "object" ? context.options[0] : {};
    function isNamedFunction(node) {
      if (node.id)
        return true;
      const parent = node.parent;
      return parent.type === "MethodDefinition" || parent.type === "Property" && (parent.kind === "get" || parent.kind === "set" || parent.method);
    }
    function getConfigForFunction(node) {
      if (node.type === "ArrowFunctionExpression") {
        if (node.async && utils.isOpeningParenToken(sourceCode.getFirstToken(node, { skip: 1 })))
          return overrideConfig.asyncArrow || baseConfig;
      } else if (isNamedFunction(node)) {
        return overrideConfig.named || baseConfig;
      } else if (!node.generator) {
        return overrideConfig.anonymous || baseConfig;
      }
      return "ignore";
    }
    function checkFunction(node) {
      const functionConfig = getConfigForFunction(node);
      if (functionConfig === "ignore")
        return;
      const rightToken = sourceCode.getFirstToken(node, utils.isOpeningParenToken);
      const leftToken = sourceCode.getTokenBefore(rightToken);
      const hasSpacing = sourceCode.isSpaceBetweenTokens(leftToken, rightToken);
      if (hasSpacing && functionConfig === "never") {
        context.report({
          node,
          loc: {
            start: leftToken.loc.end,
            end: rightToken.loc.start
          },
          messageId: "unexpectedSpace",
          fix(fixer) {
            const comments = sourceCode.getCommentsBefore(rightToken);
            if (comments.some((comment) => comment.type === "Line"))
              return null;
            return fixer.replaceTextRange(
              [leftToken.range[1], rightToken.range[0]],
              comments.reduce((text, comment) => text + sourceCode.getText(comment), "")
            );
          }
        });
      } else if (!hasSpacing && functionConfig === "always") {
        context.report({
          node,
          loc: rightToken.loc,
          messageId: "missingSpace",
          fix: (fixer) => fixer.insertTextAfter(leftToken, " ")
        });
      }
    }
    return {
      ArrowFunctionExpression: checkFunction,
      FunctionDeclaration: checkFunction,
      FunctionExpression: checkFunction
    };
  }
});

exports.spaceBeforeFunctionParen = spaceBeforeFunctionParen;
