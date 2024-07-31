'use strict';

var utils = require('./utils.js');

var functionCallSpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow spacing between function identifiers and their invocations",
      url: "https://eslint.style/rules/js/function-call-spacing"
    },
    fixable: "whitespace",
    schema: {
      anyOf: [
        {
          type: "array",
          items: [
            {
              type: "string",
              enum: ["never"]
            }
          ],
          minItems: 0,
          maxItems: 1
        },
        {
          type: "array",
          items: [
            {
              type: "string",
              enum: ["always"]
            },
            {
              type: "object",
              properties: {
                allowNewlines: {
                  type: "boolean"
                }
              },
              additionalProperties: false
            }
          ],
          minItems: 0,
          maxItems: 2
        }
      ]
    },
    messages: {
      unexpectedWhitespace: "Unexpected whitespace between function name and paren.",
      unexpectedNewline: "Unexpected newline between function name and paren.",
      missing: "Missing space between function name and paren."
    }
  },
  create(context) {
    const never = context.options[0] !== "always";
    const allowNewlines = !never && context.options[1] && context.options[1].allowNewlines;
    const sourceCode = context.sourceCode;
    const text = sourceCode.getText();
    function checkSpacing(node, leftToken, rightToken) {
      const textBetweenTokens = text.slice(leftToken.range[1], rightToken.range[0]).replace(/\/\*.*?\*\//gu, "");
      const hasWhitespace = /\s/u.test(textBetweenTokens);
      const hasNewline = hasWhitespace && utils.LINEBREAK_MATCHER.test(textBetweenTokens);
      if (never && hasWhitespace) {
        context.report({
          node,
          loc: {
            start: leftToken.loc.end,
            end: {
              line: rightToken.loc.start.line,
              column: rightToken.loc.start.column - 1
            }
          },
          messageId: "unexpectedWhitespace",
          fix(fixer) {
            if (sourceCode.commentsExistBetween(leftToken, rightToken))
              return null;
            if ("optional" in node && node.optional)
              return fixer.replaceTextRange([leftToken.range[1], rightToken.range[0]], "?.");
            if (hasNewline)
              return null;
            return fixer.removeRange([leftToken.range[1], rightToken.range[0]]);
          }
        });
      } else if (!never && !hasWhitespace) {
        context.report({
          node,
          loc: {
            start: {
              line: leftToken.loc.end.line,
              column: leftToken.loc.end.column - 1
            },
            end: rightToken.loc.start
          },
          messageId: "missing",
          fix(fixer) {
            if ("optional" in node && node.optional)
              return null;
            return fixer.insertTextBefore(rightToken, " ");
          }
        });
      } else if (!never && !allowNewlines && hasNewline) {
        context.report({
          node,
          loc: {
            start: leftToken.loc.end,
            end: rightToken.loc.start
          },
          messageId: "unexpectedNewline",
          fix(fixer) {
            if (!("optional" in node) || !node.optional)
              return null;
            if (sourceCode.commentsExistBetween(leftToken, rightToken))
              return null;
            const range = [leftToken.range[1], rightToken.range[0]];
            const qdToken = sourceCode.getTokenAfter(leftToken);
            if (qdToken.range[0] === leftToken.range[1])
              return fixer.replaceTextRange(range, "?. ");
            if (qdToken.range[1] === rightToken.range[0])
              return fixer.replaceTextRange(range, " ?.");
            return fixer.replaceTextRange(range, " ?. ");
          }
        });
      }
    }
    return {
      "CallExpression, NewExpression": function(node) {
        const lastToken = sourceCode.getLastToken(node);
        const lastCalleeToken = sourceCode.getLastToken(node.callee);
        const parenToken = sourceCode.getFirstTokenBetween(lastCalleeToken, lastToken, utils.isOpeningParenToken);
        const prevToken = parenToken && sourceCode.getTokenBefore(parenToken, utils.isNotQuestionDotToken);
        if (!(parenToken && parenToken.range[1] < node.range[1]))
          return;
        checkSpacing(node, prevToken, parenToken);
      },
      ImportExpression(node) {
        const leftToken = sourceCode.getFirstToken(node);
        const rightToken = sourceCode.getTokenAfter(leftToken);
        checkSpacing(node, leftToken, rightToken);
      }
    };
  }
});

exports.functionCallSpacing = functionCallSpacing;
