'use strict';

var utils = require('./utils.js');

var blockSpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Disallow or enforce spaces inside of blocks after opening block and before closing block",
      url: "https://eslint.style/rules/js/block-spacing"
    },
    fixable: "whitespace",
    schema: [
      { type: "string", enum: ["always", "never"] }
    ],
    messages: {
      missing: "Requires a space {{location}} '{{token}}'.",
      extra: "Unexpected space(s) {{location}} '{{token}}'."
    }
  },
  create(context) {
    const always = context.options[0] !== "never";
    const messageId = always ? "missing" : "extra";
    const sourceCode = context.sourceCode;
    function getOpenBrace(node) {
      if (node.type === "SwitchStatement") {
        if (node.cases.length > 0)
          return sourceCode.getTokenBefore(node.cases[0]);
        return sourceCode.getLastToken(node, 1);
      }
      if (node.type === "StaticBlock")
        return sourceCode.getFirstToken(node, { skip: 1 });
      return sourceCode.getFirstToken(node);
    }
    function isValid(left, right) {
      return !utils.isTokenOnSameLine(left, right) || sourceCode.isSpaceBetweenTokens(left, right) === always;
    }
    function checkSpacingInsideBraces(node) {
      const openBrace = getOpenBrace(node);
      const closeBrace = sourceCode.getLastToken(node);
      const firstToken = sourceCode.getTokenAfter(openBrace, { includeComments: true });
      const lastToken = sourceCode.getTokenBefore(closeBrace, { includeComments: true });
      if (openBrace.type !== "Punctuator" || openBrace.value !== "{" || closeBrace.type !== "Punctuator" || closeBrace.value !== "}" || firstToken === closeBrace) {
        return;
      }
      if (!always && firstToken.type === "Line")
        return;
      if (!isValid(openBrace, firstToken)) {
        let loc = openBrace.loc;
        if (messageId === "extra") {
          loc = {
            start: openBrace.loc.end,
            end: firstToken.loc.start
          };
        }
        context.report({
          node,
          loc,
          messageId,
          data: {
            location: "after",
            token: openBrace.value
          },
          fix(fixer) {
            if (always)
              return fixer.insertTextBefore(firstToken, " ");
            return fixer.removeRange([openBrace.range[1], firstToken.range[0]]);
          }
        });
      }
      if (!isValid(lastToken, closeBrace)) {
        let loc = closeBrace.loc;
        if (messageId === "extra") {
          loc = {
            start: lastToken.loc.end,
            end: closeBrace.loc.start
          };
        }
        context.report({
          node,
          loc,
          messageId,
          data: {
            location: "before",
            token: closeBrace.value
          },
          fix(fixer) {
            if (always)
              return fixer.insertTextAfter(lastToken, " ");
            return fixer.removeRange([lastToken.range[1], closeBrace.range[0]]);
          }
        });
      }
    }
    return {
      BlockStatement: checkSpacingInsideBraces,
      StaticBlock: checkSpacingInsideBraces,
      SwitchStatement: checkSpacingInsideBraces
    };
  }
});

exports.blockSpacing = blockSpacing;
