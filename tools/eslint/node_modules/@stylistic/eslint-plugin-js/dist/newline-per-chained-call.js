'use strict';

var utils = require('./utils.js');

var newlinePerChainedCall = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require a newline after each call in a method chain",
      url: "https://eslint.style/rules/js/newline-per-chained-call"
    },
    fixable: "whitespace",
    schema: [{
      type: "object",
      properties: {
        ignoreChainWithDepth: {
          type: "integer",
          minimum: 1,
          maximum: 10,
          default: 2
        }
      },
      additionalProperties: false
    }],
    messages: {
      expected: "Expected line break before `{{callee}}`."
    }
  },
  create(context) {
    const options = context.options[0] || {};
    const ignoreChainWithDepth = options.ignoreChainWithDepth || 2;
    const sourceCode = context.sourceCode;
    function getPrefix(node) {
      if (node.computed) {
        if (node.optional)
          return "?.[";
        return "[";
      }
      if (node.optional)
        return "?.";
      return ".";
    }
    function getPropertyText(node) {
      const prefix = getPrefix(node);
      const lines = sourceCode.getText(node.property).split(utils.LINEBREAK_MATCHER);
      const suffix = node.computed && lines.length === 1 ? "]" : "";
      return prefix + lines[0] + suffix;
    }
    return {
      "CallExpression:exit": function(node) {
        const callee = utils.skipChainExpression(node.callee);
        if (callee.type !== "MemberExpression")
          return;
        let parent = utils.skipChainExpression(callee.object);
        let depth = 1;
        while (parent && "callee" in parent && parent.callee) {
          depth += 1;
          const parentCallee = utils.skipChainExpression(parent.callee);
          if (!("object" in parentCallee))
            break;
          parent = utils.skipChainExpression(parentCallee.object);
        }
        if (depth > ignoreChainWithDepth && utils.isTokenOnSameLine(callee.object, callee.property)) {
          const firstTokenAfterObject = sourceCode.getTokenAfter(callee.object, utils.isNotClosingParenToken);
          context.report({
            node: callee.property,
            loc: {
              start: firstTokenAfterObject.loc.start,
              end: callee.loc.end
            },
            messageId: "expected",
            data: {
              callee: getPropertyText(callee)
            },
            fix(fixer) {
              return fixer.insertTextBefore(firstTokenAfterObject, "\n");
            }
          });
        }
      }
    };
  }
});

exports.newlinePerChainedCall = newlinePerChainedCall;
