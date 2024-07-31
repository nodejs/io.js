'use strict';

var utils = require('./utils.js');

var commaSpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing before and after commas",
      url: "https://eslint.style/rules/js/comma-spacing"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "object",
        properties: {
          before: {
            type: "boolean",
            default: false
          },
          after: {
            type: "boolean",
            default: true
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      missing: "A space is required {{loc}} ','.",
      unexpected: "There should be no space {{loc}} ','."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const tokensAndComments = sourceCode.tokensAndComments;
    const options = {
      before: context.options[0] ? context.options[0].before : false,
      after: context.options[0] ? context.options[0].after : true
    };
    const commaTokensToIgnore = [];
    function report(node, loc, otherNode) {
      context.report({
        node,
        fix(fixer) {
          if (options[loc]) {
            if (loc === "before")
              return fixer.insertTextBefore(node, " ");
            return fixer.insertTextAfter(node, " ");
          }
          let start, end;
          const newText = "";
          if (loc === "before") {
            start = otherNode.range[1];
            end = node.range[0];
          } else {
            start = node.range[1];
            end = otherNode.range[0];
          }
          return fixer.replaceTextRange([start, end], newText);
        },
        messageId: options[loc] ? "missing" : "unexpected",
        data: {
          loc
        }
      });
    }
    function addNullElementsToIgnoreList(node) {
      let previousToken = sourceCode.getFirstToken(node);
      node.elements.forEach((element) => {
        let token;
        if (element === null) {
          token = sourceCode.getTokenAfter(previousToken);
          if (utils.isCommaToken(token))
            commaTokensToIgnore.push(token);
        } else {
          token = sourceCode.getTokenAfter(element);
        }
        previousToken = token;
      });
    }
    return {
      "Program:exit": function() {
        tokensAndComments.forEach((token, i) => {
          if (!utils.isCommaToken(token))
            return;
          const previousToken = tokensAndComments[i - 1];
          const nextToken = tokensAndComments[i + 1];
          if (previousToken && !utils.isCommaToken(previousToken) && !commaTokensToIgnore.includes(token) && utils.isTokenOnSameLine(previousToken, token) && options.before !== sourceCode.isSpaceBetweenTokens(previousToken, token)) {
            report(token, "before", previousToken);
          }
          if (nextToken && !utils.isCommaToken(nextToken) && !utils.isClosingParenToken(nextToken) && !utils.isClosingBracketToken(nextToken) && !utils.isClosingBraceToken(nextToken) && !(!options.after && nextToken.type === "Line") && utils.isTokenOnSameLine(token, nextToken) && options.after !== sourceCode.isSpaceBetweenTokens(token, nextToken)) {
            report(token, "after", nextToken);
          }
        });
      },
      "ArrayExpression": addNullElementsToIgnoreList,
      "ArrayPattern": addNullElementsToIgnoreList
    };
  }
});

exports.commaSpacing = commaSpacing;
