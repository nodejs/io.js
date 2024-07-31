'use strict';

var utils = require('./utils.js');

var wrapRegex = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require parenthesis around regex literals",
      url: "https://eslint.style/rules/js/wrap-regex"
    },
    schema: [],
    fixable: "code",
    messages: {
      requireParens: "Wrap the regexp literal in parens to disambiguate the slash."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    return {
      Literal(node) {
        const token = sourceCode.getFirstToken(node);
        const nodeType = token.type;
        if (nodeType === "RegularExpression") {
          const beforeToken = sourceCode.getTokenBefore(node);
          const afterToken = sourceCode.getTokenAfter(node);
          const { parent } = node;
          if (parent.type === "MemberExpression" && parent.object === node && !(beforeToken && beforeToken.value === "(" && afterToken && afterToken.value === ")")) {
            context.report({
              node,
              messageId: "requireParens",
              fix: (fixer) => fixer.replaceText(node, `(${sourceCode.getText(node)})`)
            });
          }
        }
      }
    };
  }
});

exports.wrapRegex = wrapRegex;
