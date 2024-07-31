'use strict';

var utils = require('./utils.js');

var braceStyle = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent brace style for blocks",
      url: "https://eslint.style/rules/js/brace-style"
    },
    schema: [
      {
        type: "string",
        enum: ["1tbs", "stroustrup", "allman"]
      },
      {
        type: "object",
        properties: {
          allowSingleLine: {
            type: "boolean",
            default: false
          }
        },
        additionalProperties: false
      }
    ],
    fixable: "whitespace",
    messages: {
      nextLineOpen: "Opening curly brace does not appear on the same line as controlling statement.",
      sameLineOpen: "Opening curly brace appears on the same line as controlling statement.",
      blockSameLine: "Statement inside of curly braces should be on next line.",
      nextLineClose: "Closing curly brace does not appear on the same line as the subsequent block.",
      singleLineClose: "Closing curly brace should be on the same line as opening curly brace or on the line after the previous block.",
      sameLineClose: "Closing curly brace appears on the same line as the subsequent block."
    }
  },
  create(context) {
    const style = context.options[0] || "1tbs";
    const params = context.options[1] || {};
    const sourceCode = context.sourceCode;
    function removeNewlineBetween(firstToken, secondToken) {
      const textRange = [firstToken.range[1], secondToken.range[0]];
      const textBetween = sourceCode.text.slice(textRange[0], textRange[1]);
      if (textBetween.trim())
        return null;
      return (fixer) => fixer.replaceTextRange(textRange, " ");
    }
    function validateCurlyPair(openingCurly, closingCurly) {
      const tokenBeforeOpeningCurly = sourceCode.getTokenBefore(openingCurly);
      const tokenAfterOpeningCurly = sourceCode.getTokenAfter(openingCurly);
      const tokenBeforeClosingCurly = sourceCode.getTokenBefore(closingCurly);
      const singleLineException = params.allowSingleLine && utils.isTokenOnSameLine(openingCurly, closingCurly);
      if (style !== "allman" && !utils.isTokenOnSameLine(tokenBeforeOpeningCurly, openingCurly)) {
        context.report({
          node: openingCurly,
          messageId: "nextLineOpen",
          fix: removeNewlineBetween(
            tokenBeforeOpeningCurly,
            openingCurly
          )
        });
      }
      if (style === "allman" && utils.isTokenOnSameLine(tokenBeforeOpeningCurly, openingCurly) && !singleLineException) {
        context.report({
          node: openingCurly,
          messageId: "sameLineOpen",
          fix: (fixer) => fixer.insertTextBefore(openingCurly, "\n")
        });
      }
      if (utils.isTokenOnSameLine(openingCurly, tokenAfterOpeningCurly) && tokenAfterOpeningCurly !== closingCurly && !singleLineException) {
        context.report({
          node: openingCurly,
          messageId: "blockSameLine",
          fix: (fixer) => fixer.insertTextAfter(openingCurly, "\n")
        });
      }
      if (tokenBeforeClosingCurly !== openingCurly && !singleLineException && utils.isTokenOnSameLine(tokenBeforeClosingCurly, closingCurly)) {
        context.report({
          node: closingCurly,
          messageId: "singleLineClose",
          fix: (fixer) => fixer.insertTextBefore(closingCurly, "\n")
        });
      }
    }
    function validateCurlyBeforeKeyword(curlyToken) {
      const keywordToken = sourceCode.getTokenAfter(curlyToken);
      if (style === "1tbs" && !utils.isTokenOnSameLine(curlyToken, keywordToken)) {
        context.report({
          node: curlyToken,
          messageId: "nextLineClose",
          fix: removeNewlineBetween(curlyToken, keywordToken)
        });
      }
      if (style !== "1tbs" && utils.isTokenOnSameLine(curlyToken, keywordToken)) {
        context.report({
          node: curlyToken,
          messageId: "sameLineClose",
          fix: (fixer) => fixer.insertTextAfter(curlyToken, "\n")
        });
      }
    }
    return {
      BlockStatement(node) {
        if (!utils.STATEMENT_LIST_PARENTS.has(node.parent.type))
          validateCurlyPair(sourceCode.getFirstToken(node), sourceCode.getLastToken(node));
      },
      StaticBlock(node) {
        validateCurlyPair(
          sourceCode.getFirstToken(node, { skip: 1 }),
          // skip the `static` token
          sourceCode.getLastToken(node)
        );
      },
      ClassBody(node) {
        validateCurlyPair(sourceCode.getFirstToken(node), sourceCode.getLastToken(node));
      },
      SwitchStatement(node) {
        const closingCurly = sourceCode.getLastToken(node);
        const openingCurly = sourceCode.getTokenBefore(node.cases.length ? node.cases[0] : closingCurly);
        validateCurlyPair(openingCurly, closingCurly);
      },
      IfStatement(node) {
        if (node.consequent.type === "BlockStatement" && node.alternate) {
          validateCurlyBeforeKeyword(sourceCode.getLastToken(node.consequent));
        }
      },
      TryStatement(node) {
        validateCurlyBeforeKeyword(sourceCode.getLastToken(node.block));
        if (node.handler && node.finalizer) {
          validateCurlyBeforeKeyword(sourceCode.getLastToken(node.handler.body));
        }
      }
    };
  }
});

exports.braceStyle = braceStyle;
