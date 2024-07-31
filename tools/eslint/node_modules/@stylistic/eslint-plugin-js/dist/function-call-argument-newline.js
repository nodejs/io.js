'use strict';

var utils = require('./utils.js');

var functionCallArgumentNewline = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce line breaks between arguments of a function call",
      url: "https://eslint.style/rules/js/function-call-argument-newline"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "string",
        enum: ["always", "never", "consistent"]
      }
    ],
    messages: {
      unexpectedLineBreak: "There should be no line break here.",
      missingLineBreak: "There should be a line break after this argument."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const checkers = {
      unexpected: {
        messageId: "unexpectedLineBreak",
        check: (prevToken, currentToken) => prevToken.loc.end.line !== currentToken.loc.start.line,
        createFix: (token, tokenBefore) => (fixer) => fixer.replaceTextRange([tokenBefore.range[1], token.range[0]], " ")
      },
      missing: {
        messageId: "missingLineBreak",
        check: (prevToken, currentToken) => prevToken.loc.end.line === currentToken.loc.start.line,
        createFix: (token, tokenBefore) => (fixer) => fixer.replaceTextRange([tokenBefore.range[1], token.range[0]], "\n")
      }
    };
    function checkArguments(node, checker) {
      for (let i = 1; i < node.arguments.length; i++) {
        const prevArgToken = sourceCode.getLastToken(node.arguments[i - 1]);
        const currentArgToken = sourceCode.getFirstToken(node.arguments[i]);
        if (checker.check(prevArgToken, currentArgToken)) {
          const tokenBefore = sourceCode.getTokenBefore(
            currentArgToken,
            { includeComments: true }
          );
          const hasLineCommentBefore = tokenBefore.type === "Line";
          context.report({
            node,
            loc: {
              start: tokenBefore.loc.end,
              end: currentArgToken.loc.start
            },
            messageId: checker.messageId,
            fix: hasLineCommentBefore ? null : checker.createFix(currentArgToken, tokenBefore)
          });
        }
      }
    }
    function check(node) {
      if (node.arguments.length < 2)
        return;
      const option = context.options[0] || "always";
      if (option === "never") {
        checkArguments(node, checkers.unexpected);
      } else if (option === "always") {
        checkArguments(node, checkers.missing);
      } else if (option === "consistent") {
        const firstArgToken = sourceCode.getLastToken(node.arguments[0]);
        const secondArgToken = sourceCode.getFirstToken(node.arguments[1]);
        if (firstArgToken?.loc.end.line === secondArgToken?.loc.start.line)
          checkArguments(node, checkers.unexpected);
        else
          checkArguments(node, checkers.missing);
      }
    }
    return {
      CallExpression: check,
      NewExpression: check
    };
  }
});

exports.functionCallArgumentNewline = functionCallArgumentNewline;
