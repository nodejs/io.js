'use strict';

var utils = require('./utils.js');

var commaStyle = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent comma style",
      url: "https://eslint.style/rules/js/comma-style"
    },
    fixable: "code",
    schema: [
      {
        type: "string",
        enum: ["first", "last"]
      },
      {
        type: "object",
        properties: {
          exceptions: {
            type: "object",
            additionalProperties: {
              type: "boolean"
            }
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      unexpectedLineBeforeAndAfterComma: "Bad line breaking before and after ','.",
      expectedCommaFirst: "',' should be placed first.",
      expectedCommaLast: "',' should be placed last."
    }
  },
  create(context) {
    var _a;
    const style = context.options[0] || "last";
    const sourceCode = context.sourceCode;
    const exceptions = {
      ArrayPattern: true,
      ArrowFunctionExpression: true,
      CallExpression: true,
      FunctionDeclaration: true,
      FunctionExpression: true,
      ImportDeclaration: true,
      ObjectPattern: true,
      NewExpression: true
    };
    if (context.options.length === 2 && Object.prototype.hasOwnProperty.call(context.options[1], "exceptions")) {
      (_a = context.options)[1] ?? (_a[1] = { exceptions: {} });
      const rawExceptions = context.options[1].exceptions;
      const keys = Object.keys(rawExceptions);
      for (let i = 0; i < keys.length; i++)
        exceptions[keys[i]] = rawExceptions[keys[i]];
    }
    function getReplacedText(styleType, text) {
      switch (styleType) {
        case "between":
          return `,${text.replace(utils.LINEBREAK_MATCHER, "")}`;
        case "first":
          return `${text},`;
        case "last":
          return `,${text}`;
        default:
          return "";
      }
    }
    function getFixerFunction(styleType, previousItemToken, commaToken, currentItemToken) {
      const text = sourceCode.text.slice(previousItemToken.range[1], commaToken.range[0]) + sourceCode.text.slice(commaToken.range[1], currentItemToken.range[0]);
      const range = [previousItemToken.range[1], currentItemToken.range[0]];
      return function(fixer) {
        return fixer.replaceTextRange(range, getReplacedText(styleType, text));
      };
    }
    function validateCommaItemSpacing(previousItemToken, commaToken, currentItemToken, reportItem) {
      if (utils.isTokenOnSameLine(commaToken, currentItemToken) && utils.isTokenOnSameLine(previousItemToken, commaToken)) ; else if (!utils.isTokenOnSameLine(commaToken, currentItemToken) && !utils.isTokenOnSameLine(previousItemToken, commaToken)) {
        const comment = sourceCode.getCommentsAfter(commaToken)[0];
        const styleType = comment && comment.type === "Block" && utils.isTokenOnSameLine(commaToken, comment) ? style : "between";
        context.report({
          node: reportItem,
          loc: commaToken.loc,
          messageId: "unexpectedLineBeforeAndAfterComma",
          fix: getFixerFunction(styleType, previousItemToken, commaToken, currentItemToken)
        });
      } else if (style === "first" && !utils.isTokenOnSameLine(commaToken, currentItemToken)) {
        context.report({
          node: reportItem,
          loc: commaToken.loc,
          messageId: "expectedCommaFirst",
          fix: getFixerFunction(style, previousItemToken, commaToken, currentItemToken)
        });
      } else if (style === "last" && utils.isTokenOnSameLine(commaToken, currentItemToken)) {
        context.report({
          node: reportItem,
          loc: commaToken.loc,
          messageId: "expectedCommaLast",
          fix: getFixerFunction(style, previousItemToken, commaToken, currentItemToken)
        });
      }
    }
    function validateComma(node, property) {
      const items = node[property];
      const arrayLiteral = node.type === "ArrayExpression" || node.type === "ArrayPattern";
      if (items.length > 1 || arrayLiteral) {
        let previousItemToken = sourceCode.getFirstToken(node);
        items.forEach((item) => {
          const commaToken = item ? sourceCode.getTokenBefore(item) : previousItemToken;
          const currentItemToken = item ? sourceCode.getFirstToken(item) : sourceCode.getTokenAfter(commaToken);
          const reportItem = item || currentItemToken;
          if (utils.isCommaToken(commaToken))
            validateCommaItemSpacing(previousItemToken, commaToken, currentItemToken, reportItem);
          if (item) {
            const tokenAfterItem = sourceCode.getTokenAfter(item, utils.isNotClosingParenToken);
            previousItemToken = tokenAfterItem ? sourceCode.getTokenBefore(tokenAfterItem) : sourceCode.ast.tokens[sourceCode.ast.tokens.length - 1];
          } else {
            previousItemToken = currentItemToken;
          }
        });
        if (arrayLiteral) {
          const lastToken = sourceCode.getLastToken(node);
          const nextToLastToken = sourceCode.getTokenBefore(lastToken);
          if (utils.isCommaToken(nextToLastToken)) {
            validateCommaItemSpacing(
              sourceCode.getTokenBefore(nextToLastToken),
              nextToLastToken,
              lastToken,
              lastToken
            );
          }
        }
      }
    }
    const nodes = {};
    if (!exceptions.VariableDeclaration) {
      nodes.VariableDeclaration = function(node) {
        validateComma(node, "declarations");
      };
    }
    if (!exceptions.ObjectExpression) {
      nodes.ObjectExpression = function(node) {
        validateComma(node, "properties");
      };
    }
    if (!exceptions.ObjectPattern) {
      nodes.ObjectPattern = function(node) {
        validateComma(node, "properties");
      };
    }
    if (!exceptions.ArrayExpression) {
      nodes.ArrayExpression = function(node) {
        validateComma(node, "elements");
      };
    }
    if (!exceptions.ArrayPattern) {
      nodes.ArrayPattern = function(node) {
        validateComma(node, "elements");
      };
    }
    if (!exceptions.FunctionDeclaration) {
      nodes.FunctionDeclaration = function(node) {
        validateComma(node, "params");
      };
    }
    if (!exceptions.FunctionExpression) {
      nodes.FunctionExpression = function(node) {
        validateComma(node, "params");
      };
    }
    if (!exceptions.ArrowFunctionExpression) {
      nodes.ArrowFunctionExpression = function(node) {
        validateComma(node, "params");
      };
    }
    if (!exceptions.CallExpression) {
      nodes.CallExpression = function(node) {
        validateComma(node, "arguments");
      };
    }
    if (!exceptions.ImportDeclaration) {
      nodes.ImportDeclaration = function(node) {
        validateComma(node, "specifiers");
      };
    }
    if (!exceptions.NewExpression) {
      nodes.NewExpression = function(node) {
        validateComma(node, "arguments");
      };
    }
    return nodes;
  }
});

exports.commaStyle = commaStyle;
