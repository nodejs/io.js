'use strict';

var utils = require('./utils.js');

function isCalleeOfNewExpression(node) {
  const maybeCallee = node.parent?.type === "ChainExpression" ? node.parent : node;
  return maybeCallee.parent?.type === "NewExpression" && maybeCallee.parent.callee === maybeCallee;
}
var wrapIife = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require parentheses around immediate `function` invocations",
      url: "https://eslint.style/rules/js/wrap-iife"
    },
    schema: [
      {
        type: "string",
        enum: ["outside", "inside", "any"]
      },
      {
        type: "object",
        properties: {
          functionPrototypeMethods: {
            type: "boolean",
            default: false
          }
        },
        additionalProperties: false
      }
    ],
    fixable: "code",
    messages: {
      wrapInvocation: "Wrap an immediate function invocation in parentheses.",
      wrapExpression: "Wrap only the function expression in parens.",
      moveInvocation: "Move the invocation into the parens that contain the function."
    }
  },
  create(context) {
    const style = context.options[0] || "outside";
    const includeFunctionPrototypeMethods = context.options[1] && context.options[1].functionPrototypeMethods;
    const sourceCode = context.sourceCode;
    function isWrappedInAnyParens(node) {
      return utils.isParenthesised(sourceCode, node);
    }
    function isWrappedInGroupingParens(node) {
      return utils.isParenthesized(1, node, sourceCode);
    }
    function getFunctionNodeFromIIFE(node) {
      const callee = utils.skipChainExpression(node.callee);
      if (callee.type === "FunctionExpression")
        return callee;
      if (includeFunctionPrototypeMethods && callee.type === "MemberExpression" && callee.object.type === "FunctionExpression" && (utils.getStaticPropertyName(callee) === "call" || utils.getStaticPropertyName(callee) === "apply")) {
        return callee.object;
      }
      return null;
    }
    return {
      CallExpression(node) {
        const innerNode = getFunctionNodeFromIIFE(node);
        if (!innerNode)
          return;
        const isCallExpressionWrapped = isWrappedInAnyParens(node);
        const isFunctionExpressionWrapped = isWrappedInAnyParens(innerNode);
        if (!isCallExpressionWrapped && !isFunctionExpressionWrapped) {
          context.report({
            node,
            messageId: "wrapInvocation",
            fix(fixer) {
              const nodeToSurround = style === "inside" ? innerNode : node;
              return fixer.replaceText(nodeToSurround, `(${sourceCode.getText(nodeToSurround)})`);
            }
          });
        } else if (style === "inside" && !isFunctionExpressionWrapped) {
          context.report({
            node,
            messageId: "wrapExpression",
            fix(fixer) {
              if (isWrappedInGroupingParens(node) && !isCalleeOfNewExpression(node)) {
                const parenAfter = sourceCode.getTokenAfter(node);
                return fixer.replaceTextRange(
                  [
                    innerNode.range[1],
                    parenAfter.range[1]
                  ],
                  `)${sourceCode.getText().slice(innerNode.range[1], parenAfter.range[0])}`
                );
              }
              return fixer.replaceText(innerNode, `(${sourceCode.getText(innerNode)})`);
            }
          });
        } else if (style === "outside" && !isCallExpressionWrapped) {
          context.report({
            node,
            messageId: "moveInvocation",
            fix(fixer) {
              const parenAfter = sourceCode.getTokenAfter(innerNode);
              return fixer.replaceTextRange(
                [
                  parenAfter.range[0],
                  node.range[1]
                ],
                `${sourceCode.getText().slice(parenAfter.range[1], node.range[1])})`
              );
            }
          });
        }
      }
    };
  }
});

exports.wrapIife = wrapIife;
