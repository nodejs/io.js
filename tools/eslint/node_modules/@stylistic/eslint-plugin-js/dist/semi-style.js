'use strict';

var utils = require('./utils.js');

const SELECTOR = [
  "BreakStatement",
  "ContinueStatement",
  "DebuggerStatement",
  "DoWhileStatement",
  "ExportAllDeclaration",
  "ExportDefaultDeclaration",
  "ExportNamedDeclaration",
  "ExpressionStatement",
  "ImportDeclaration",
  "ReturnStatement",
  "ThrowStatement",
  "VariableDeclaration",
  "PropertyDefinition"
].join(",");
function getChildren(node) {
  const t = node.type;
  if (t === "BlockStatement" || t === "StaticBlock" || t === "Program" || t === "ClassBody") {
    return node.body;
  }
  if (t === "SwitchCase")
    return node.consequent;
  return null;
}
function isLastChild(node) {
  if (!node.parent)
    return true;
  const t = node.parent.type;
  if (t === "IfStatement" && node.parent.consequent === node && node.parent.alternate) {
    return true;
  }
  if (t === "DoWhileStatement") {
    return true;
  }
  const nodeList = getChildren(node.parent);
  return nodeList !== null && nodeList[nodeList.length - 1] === node;
}
var semiStyle = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce location of semicolons",
      url: "https://eslint.style/rules/js/semi-style"
    },
    schema: [{ type: "string", enum: ["last", "first"] }],
    fixable: "whitespace",
    messages: {
      expectedSemiColon: "Expected this semicolon to be at {{pos}}."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const option = context.options[0] || "last";
    function check(semiToken, expected) {
      const prevToken = sourceCode.getTokenBefore(semiToken);
      const nextToken = sourceCode.getTokenAfter(semiToken);
      const prevIsSameLine = !prevToken || utils.isTokenOnSameLine(prevToken, semiToken);
      const nextIsSameLine = !nextToken || utils.isTokenOnSameLine(semiToken, nextToken);
      if (expected === "last" && !prevIsSameLine || expected === "first" && !nextIsSameLine) {
        context.report({
          loc: semiToken.loc,
          messageId: "expectedSemiColon",
          data: {
            pos: expected === "last" ? "the end of the previous line" : "the beginning of the next line"
          },
          fix(fixer) {
            if (prevToken && nextToken && sourceCode.commentsExistBetween(prevToken, nextToken))
              return null;
            const start = prevToken ? prevToken.range[1] : semiToken.range[0];
            const end = nextToken ? nextToken.range[0] : semiToken.range[1];
            const text = expected === "last" ? ";\n" : "\n;";
            return fixer.replaceTextRange([start, end], text);
          }
        });
      }
    }
    return {
      [SELECTOR](node) {
        if (option === "first" && isLastChild(node))
          return;
        const lastToken = sourceCode.getLastToken(node);
        if (utils.isSemicolonToken(lastToken))
          check(lastToken, option);
      },
      ForStatement(node) {
        const firstSemi = node.init && sourceCode.getTokenAfter(node.init, utils.isSemicolonToken);
        const secondSemi = node.test && sourceCode.getTokenAfter(node.test, utils.isSemicolonToken);
        if (firstSemi)
          check(firstSemi, "last");
        if (secondSemi)
          check(secondSemi, "last");
      }
    };
  }
});

exports.semiStyle = semiStyle;
