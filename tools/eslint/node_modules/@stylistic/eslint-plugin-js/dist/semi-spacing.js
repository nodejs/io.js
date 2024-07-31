'use strict';

var utils = require('./utils.js');

var semiSpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing before and after semicolons",
      url: "https://eslint.style/rules/js/semi-spacing"
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
      unexpectedWhitespaceBefore: "Unexpected whitespace before semicolon.",
      unexpectedWhitespaceAfter: "Unexpected whitespace after semicolon.",
      missingWhitespaceBefore: "Missing whitespace before semicolon.",
      missingWhitespaceAfter: "Missing whitespace after semicolon."
    }
  },
  create(context) {
    const config = context.options[0];
    const sourceCode = context.sourceCode;
    let requireSpaceBefore = false;
    let requireSpaceAfter = true;
    if (typeof config === "object") {
      requireSpaceBefore = config.before;
      requireSpaceAfter = config.after;
    }
    function hasLeadingSpace(token) {
      const tokenBefore = sourceCode.getTokenBefore(token);
      return tokenBefore && utils.isTokenOnSameLine(tokenBefore, token) && sourceCode.isSpaceBetweenTokens(tokenBefore, token);
    }
    function hasTrailingSpace(token) {
      const tokenAfter = sourceCode.getTokenAfter(token);
      return tokenAfter && utils.isTokenOnSameLine(token, tokenAfter) && sourceCode.isSpaceBetweenTokens(token, tokenAfter);
    }
    function isLastTokenInCurrentLine(token) {
      const tokenAfter = sourceCode.getTokenAfter(token);
      return !(tokenAfter && utils.isTokenOnSameLine(token, tokenAfter));
    }
    function isFirstTokenInCurrentLine(token) {
      const tokenBefore = sourceCode.getTokenBefore(token);
      return !(tokenBefore && utils.isTokenOnSameLine(token, tokenBefore));
    }
    function isBeforeClosingParen(token) {
      const nextToken = sourceCode.getTokenAfter(token);
      return nextToken && utils.isClosingBraceToken(nextToken) || utils.isClosingParenToken(nextToken);
    }
    function checkSemicolonSpacing(token, node) {
      if (utils.isSemicolonToken(token)) {
        if (hasLeadingSpace(token)) {
          if (!requireSpaceBefore) {
            const tokenBefore = sourceCode.getTokenBefore(token);
            const loc = {
              start: tokenBefore.loc.end,
              end: token.loc.start
            };
            context.report({
              node,
              loc,
              messageId: "unexpectedWhitespaceBefore",
              fix(fixer) {
                return fixer.removeRange([tokenBefore.range[1], token.range[0]]);
              }
            });
          }
        } else {
          if (requireSpaceBefore) {
            const loc = token.loc;
            context.report({
              node,
              loc,
              messageId: "missingWhitespaceBefore",
              fix(fixer) {
                return fixer.insertTextBefore(token, " ");
              }
            });
          }
        }
        if (!isFirstTokenInCurrentLine(token) && !isLastTokenInCurrentLine(token) && !isBeforeClosingParen(token)) {
          if (hasTrailingSpace(token)) {
            if (!requireSpaceAfter) {
              const tokenAfter = sourceCode.getTokenAfter(token);
              const loc = {
                start: token.loc.end,
                end: tokenAfter.loc.start
              };
              context.report({
                node,
                loc,
                messageId: "unexpectedWhitespaceAfter",
                fix(fixer) {
                  return fixer.removeRange([token.range[1], tokenAfter.range[0]]);
                }
              });
            }
          } else {
            if (requireSpaceAfter) {
              const loc = token.loc;
              context.report({
                node,
                loc,
                messageId: "missingWhitespaceAfter",
                fix(fixer) {
                  return fixer.insertTextAfter(token, " ");
                }
              });
            }
          }
        }
      }
    }
    function checkNode(node) {
      const token = sourceCode.getLastToken(node);
      checkSemicolonSpacing(token, node);
    }
    return {
      VariableDeclaration: checkNode,
      ExpressionStatement: checkNode,
      BreakStatement: checkNode,
      ContinueStatement: checkNode,
      DebuggerStatement: checkNode,
      DoWhileStatement: checkNode,
      ReturnStatement: checkNode,
      ThrowStatement: checkNode,
      ImportDeclaration: checkNode,
      ExportNamedDeclaration: checkNode,
      ExportAllDeclaration: checkNode,
      ExportDefaultDeclaration: checkNode,
      ForStatement(node) {
        if (node.init)
          checkSemicolonSpacing(sourceCode.getTokenAfter(node.init), node);
        if (node.test)
          checkSemicolonSpacing(sourceCode.getTokenAfter(node.test), node);
      },
      PropertyDefinition: checkNode
    };
  }
});

exports.semiSpacing = semiSpacing;
