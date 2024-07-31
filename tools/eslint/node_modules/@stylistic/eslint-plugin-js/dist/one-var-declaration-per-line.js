'use strict';

var utils = require('./utils.js');

var oneVarDeclarationPerLine = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow newlines around variable declarations",
      url: "https://eslint.style/rules/js/one-var-declaration-per-line"
    },
    schema: [
      {
        type: "string",
        enum: ["always", "initializations"]
      }
    ],
    fixable: "whitespace",
    messages: {
      expectVarOnNewline: "Expected variable declaration to be on a new line."
    }
  },
  create(context) {
    const always = context.options[0] === "always";
    function isForTypeSpecifier(keyword) {
      return keyword === "ForStatement" || keyword === "ForInStatement" || keyword === "ForOfStatement";
    }
    function checkForNewLine(node) {
      if (isForTypeSpecifier(node.parent.type))
        return;
      const declarations = node.declarations;
      let prev;
      declarations.forEach((current) => {
        if (prev && prev.loc.end.line === current.loc.start.line) {
          if (always || prev.init || current.init) {
            context.report({
              node,
              messageId: "expectVarOnNewline",
              loc: current.loc,
              fix: (fixer) => fixer.insertTextBefore(current, "\n")
            });
          }
        }
        prev = current;
      });
    }
    return {
      VariableDeclaration: checkForNewLine
    };
  }
});

exports.oneVarDeclarationPerLine = oneVarDeclarationPerLine;
