'use strict';

var utils = require('./utils.js');

var maxStatementsPerLine = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce a maximum number of statements allowed per line",
      url: "https://eslint.style/rules/js/max-statements-per-line"
    },
    schema: [
      {
        type: "object",
        properties: {
          max: {
            type: "integer",
            minimum: 1,
            default: 1
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      exceed: "This line has {{numberOfStatementsOnThisLine}} {{statements}}. Maximum allowed is {{maxStatementsPerLine}}."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const options = context.options[0] || {};
    const maxStatementsPerLine = typeof options.max !== "undefined" ? options.max : 1;
    let lastStatementLine = 0;
    let numberOfStatementsOnThisLine = 0;
    let firstExtraStatement = null;
    const SINGLE_CHILD_ALLOWED = /^(?:(?:DoWhile|For|ForIn|ForOf|If|Labeled|While)Statement|Export(?:Default|Named)Declaration)$/u;
    function reportFirstExtraStatementAndClear() {
      if (firstExtraStatement) {
        context.report({
          node: firstExtraStatement,
          messageId: "exceed",
          data: {
            numberOfStatementsOnThisLine,
            maxStatementsPerLine,
            statements: numberOfStatementsOnThisLine === 1 ? "statement" : "statements"
          }
        });
      }
      firstExtraStatement = null;
    }
    function getActualLastToken(node) {
      return sourceCode.getLastToken(node, utils.isNotSemicolonToken);
    }
    function enterStatement(node) {
      const line = node.loc.start.line;
      if (node.parent && SINGLE_CHILD_ALLOWED.test(node.parent.type) && (!("alternate" in node.parent) || node.parent.alternate !== node)) {
        return;
      }
      if (line === lastStatementLine) {
        numberOfStatementsOnThisLine += 1;
      } else {
        reportFirstExtraStatementAndClear();
        numberOfStatementsOnThisLine = 1;
        lastStatementLine = line;
      }
      if (numberOfStatementsOnThisLine === maxStatementsPerLine + 1)
        firstExtraStatement = firstExtraStatement || node;
    }
    function leaveStatement(node) {
      const line = getActualLastToken(node).loc.end.line;
      if (line !== lastStatementLine) {
        reportFirstExtraStatementAndClear();
        numberOfStatementsOnThisLine = 1;
        lastStatementLine = line;
      }
    }
    return {
      "BreakStatement": enterStatement,
      "ClassDeclaration": enterStatement,
      "ContinueStatement": enterStatement,
      "DebuggerStatement": enterStatement,
      "DoWhileStatement": enterStatement,
      "ExpressionStatement": enterStatement,
      "ForInStatement": enterStatement,
      "ForOfStatement": enterStatement,
      "ForStatement": enterStatement,
      "FunctionDeclaration": enterStatement,
      "IfStatement": enterStatement,
      "ImportDeclaration": enterStatement,
      "LabeledStatement": enterStatement,
      "ReturnStatement": enterStatement,
      "SwitchStatement": enterStatement,
      "ThrowStatement": enterStatement,
      "TryStatement": enterStatement,
      "VariableDeclaration": enterStatement,
      "WhileStatement": enterStatement,
      "WithStatement": enterStatement,
      "ExportNamedDeclaration": enterStatement,
      "ExportDefaultDeclaration": enterStatement,
      "ExportAllDeclaration": enterStatement,
      "BreakStatement:exit": leaveStatement,
      "ClassDeclaration:exit": leaveStatement,
      "ContinueStatement:exit": leaveStatement,
      "DebuggerStatement:exit": leaveStatement,
      "DoWhileStatement:exit": leaveStatement,
      "ExpressionStatement:exit": leaveStatement,
      "ForInStatement:exit": leaveStatement,
      "ForOfStatement:exit": leaveStatement,
      "ForStatement:exit": leaveStatement,
      "FunctionDeclaration:exit": leaveStatement,
      "IfStatement:exit": leaveStatement,
      "ImportDeclaration:exit": leaveStatement,
      "LabeledStatement:exit": leaveStatement,
      "ReturnStatement:exit": leaveStatement,
      "SwitchStatement:exit": leaveStatement,
      "ThrowStatement:exit": leaveStatement,
      "TryStatement:exit": leaveStatement,
      "VariableDeclaration:exit": leaveStatement,
      "WhileStatement:exit": leaveStatement,
      "WithStatement:exit": leaveStatement,
      "ExportNamedDeclaration:exit": leaveStatement,
      "ExportDefaultDeclaration:exit": leaveStatement,
      "ExportAllDeclaration:exit": leaveStatement,
      "Program:exit": reportFirstExtraStatementAndClear
    };
  }
});

exports.maxStatementsPerLine = maxStatementsPerLine;
