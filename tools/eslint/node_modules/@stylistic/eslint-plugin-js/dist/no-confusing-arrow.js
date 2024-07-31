'use strict';

var utils = require('./utils.js');

function isConditional(node) {
  return node.type === "ConditionalExpression";
}
var noConfusingArrow = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Disallow arrow functions where they could be confused with comparisons",
      url: "https://eslint.style/rules/js/no-confusing-arrow"
    },
    fixable: "code",
    schema: [{
      type: "object",
      properties: {
        allowParens: { type: "boolean", default: true },
        onlyOneSimpleParam: { type: "boolean", default: false }
      },
      additionalProperties: false
    }],
    messages: {
      confusing: "Arrow function used ambiguously with a conditional expression."
    }
  },
  create(context) {
    const config = context.options[0] || {};
    const allowParens = config.allowParens || config.allowParens === void 0;
    const onlyOneSimpleParam = config.onlyOneSimpleParam;
    const sourceCode = context.sourceCode;
    function checkArrowFunc(node) {
      const body = node.body;
      if (isConditional(body) && !(allowParens && utils.isParenthesised(sourceCode, body)) && !(onlyOneSimpleParam && !(node.params.length === 1 && node.params[0].type === "Identifier"))) {
        context.report({
          node,
          messageId: "confusing",
          fix(fixer) {
            return allowParens ? fixer.replaceText(node.body, `(${sourceCode.getText(node.body)})`) : null;
          }
        });
      }
    }
    return {
      ArrowFunctionExpression: checkArrowFunc
    };
  }
});

exports.noConfusingArrow = noConfusingArrow;
