'use strict';

var utils = require('./utils.js');

var spaceInfixOps = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require spacing around infix operators",
      url: "https://eslint.style/rules/js/space-infix-ops"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "object",
        properties: {
          int32Hint: {
            type: "boolean",
            default: false
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      missingSpace: "Operator '{{operator}}' must be spaced."
    }
  },
  create(context) {
    const int32Hint = context.options[0] ? context.options[0].int32Hint === true : false;
    const sourceCode = context.sourceCode;
    function getFirstNonSpacedToken(left, right, op) {
      const operator = sourceCode.getFirstTokenBetween(left, right, (token) => token.value === op);
      const prev = sourceCode.getTokenBefore(operator);
      const next = sourceCode.getTokenAfter(operator);
      if (!sourceCode.isSpaceBetweenTokens(prev, operator) || !sourceCode.isSpaceBetweenTokens(operator, next))
        return operator;
      return null;
    }
    function report(mainNode, culpritToken) {
      context.report({
        node: mainNode,
        loc: culpritToken.loc,
        messageId: "missingSpace",
        data: {
          operator: culpritToken.value
        },
        fix(fixer) {
          const previousToken = sourceCode.getTokenBefore(culpritToken);
          const afterToken = sourceCode.getTokenAfter(culpritToken);
          let fixString = "";
          if (culpritToken.range[0] - previousToken.range[1] === 0)
            fixString = " ";
          fixString += culpritToken.value;
          if (afterToken.range[0] - culpritToken.range[1] === 0)
            fixString += " ";
          return fixer.replaceText(culpritToken, fixString);
        }
      });
    }
    function checkBinary(node) {
      const leftNode = "typeAnnotation" in node.left && node.left.typeAnnotation ? node.left.typeAnnotation : node.left;
      const rightNode = node.right;
      const operator = "operator" in node && node.operator ? node.operator : "=";
      const nonSpacedNode = getFirstNonSpacedToken(leftNode, rightNode, operator);
      if (nonSpacedNode) {
        if (!(int32Hint && sourceCode.getText(node).endsWith("|0")))
          report(node, nonSpacedNode);
      }
    }
    function checkConditional(node) {
      const nonSpacedConsequentNode = getFirstNonSpacedToken(node.test, node.consequent, "?");
      const nonSpacedAlternateNode = getFirstNonSpacedToken(node.consequent, node.alternate, ":");
      if (nonSpacedConsequentNode)
        report(node, nonSpacedConsequentNode);
      if (nonSpacedAlternateNode)
        report(node, nonSpacedAlternateNode);
    }
    function checkVar(node) {
      const leftNode = node.id.typeAnnotation ? node.id.typeAnnotation : node.id;
      const rightNode = node.init;
      if (rightNode) {
        const nonSpacedNode = getFirstNonSpacedToken(leftNode, rightNode, "=");
        if (nonSpacedNode)
          report(node, nonSpacedNode);
      }
    }
    return {
      AssignmentExpression: checkBinary,
      AssignmentPattern: checkBinary,
      BinaryExpression: checkBinary,
      LogicalExpression: checkBinary,
      ConditionalExpression: checkConditional,
      VariableDeclarator: checkVar,
      PropertyDefinition(node) {
        if (!node.value)
          return;
        const operatorToken = sourceCode.getTokenBefore(node.value, utils.isEqToken);
        const leftToken = sourceCode.getTokenBefore(operatorToken);
        const rightToken = sourceCode.getTokenAfter(operatorToken);
        if (!sourceCode.isSpaceBetweenTokens(leftToken, operatorToken) || !sourceCode.isSpaceBetweenTokens(operatorToken, rightToken)) {
          report(node, operatorToken);
        }
      }
    };
  }
});

exports.spaceInfixOps = spaceInfixOps;
