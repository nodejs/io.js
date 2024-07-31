'use strict';

var utils = require('./utils.js');

var functionParenNewline = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent line breaks inside function parentheses",
      url: "https://eslint.style/rules/js/function-paren-newline"
    },
    fixable: "whitespace",
    schema: [
      {
        oneOf: [
          {
            type: "string",
            enum: ["always", "never", "consistent", "multiline", "multiline-arguments"]
          },
          {
            type: "object",
            properties: {
              minItems: {
                type: "integer",
                minimum: 0
              }
            },
            additionalProperties: false
          }
        ]
      }
    ],
    messages: {
      expectedBefore: "Expected newline before ')'.",
      expectedAfter: "Expected newline after '('.",
      expectedBetween: "Expected newline between arguments/params.",
      unexpectedBefore: "Unexpected newline before ')'.",
      unexpectedAfter: "Unexpected newline after '('."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const rawOption = context.options[0] || "multiline";
    const multilineOption = rawOption === "multiline";
    const multilineArgumentsOption = rawOption === "multiline-arguments";
    const consistentOption = rawOption === "consistent";
    let minItems;
    if (typeof rawOption === "object")
      minItems = rawOption.minItems;
    else if (rawOption === "always")
      minItems = 0;
    else if (rawOption === "never")
      minItems = Infinity;
    function shouldHaveNewlines(elements, hasLeftNewline) {
      if (multilineArgumentsOption && elements.length === 1)
        return hasLeftNewline;
      if (multilineOption || multilineArgumentsOption)
        return elements.some((element, index) => index !== elements.length - 1 && element.loc.end.line !== elements[index + 1].loc.start.line);
      if (consistentOption)
        return hasLeftNewline;
      return minItems == null || elements.length >= minItems;
    }
    function validateParens(parens, elements) {
      const leftParen = parens.leftParen;
      const rightParen = parens.rightParen;
      const tokenAfterLeftParen = sourceCode.getTokenAfter(leftParen);
      const tokenBeforeRightParen = sourceCode.getTokenBefore(rightParen);
      const hasLeftNewline = !utils.isTokenOnSameLine(leftParen, tokenAfterLeftParen);
      const hasRightNewline = !utils.isTokenOnSameLine(tokenBeforeRightParen, rightParen);
      const needsNewlines = shouldHaveNewlines(elements, hasLeftNewline);
      if (hasLeftNewline && !needsNewlines) {
        context.report({
          node: leftParen,
          messageId: "unexpectedAfter",
          fix(fixer) {
            return sourceCode.getText().slice(leftParen.range[1], tokenAfterLeftParen.range[0]).trim() ? null : fixer.removeRange([leftParen.range[1], tokenAfterLeftParen.range[0]]);
          }
        });
      } else if (!hasLeftNewline && needsNewlines) {
        context.report({
          node: leftParen,
          messageId: "expectedAfter",
          fix: (fixer) => fixer.insertTextAfter(leftParen, "\n")
        });
      }
      if (hasRightNewline && !needsNewlines) {
        context.report({
          node: rightParen,
          messageId: "unexpectedBefore",
          fix(fixer) {
            return sourceCode.getText().slice(tokenBeforeRightParen.range[1], rightParen.range[0]).trim() ? null : fixer.removeRange([tokenBeforeRightParen.range[1], rightParen.range[0]]);
          }
        });
      } else if (!hasRightNewline && needsNewlines) {
        context.report({
          node: rightParen,
          messageId: "expectedBefore",
          fix: (fixer) => fixer.insertTextBefore(rightParen, "\n")
        });
      }
    }
    function validateArguments(parens, elements) {
      const leftParen = parens.leftParen;
      const tokenAfterLeftParen = sourceCode.getTokenAfter(leftParen);
      const hasLeftNewline = !utils.isTokenOnSameLine(leftParen, tokenAfterLeftParen);
      const needsNewlines = shouldHaveNewlines(elements, hasLeftNewline);
      for (let i = 0; i <= elements.length - 2; i++) {
        const currentElement = elements[i];
        const nextElement = elements[i + 1];
        const hasNewLine = currentElement.loc.end.line !== nextElement.loc.start.line;
        if (!hasNewLine && needsNewlines) {
          context.report({
            node: currentElement,
            messageId: "expectedBetween",
            fix: (fixer) => fixer.insertTextBefore(nextElement, "\n")
          });
        }
      }
    }
    function getParenTokens(node) {
      const isOpeningParenTokenOutsideTypeParameter = () => {
        let typeParameterOpeningLevel = 0;
        return (token) => {
          if (token.type === "Punctuator" && token.value === "<")
            typeParameterOpeningLevel += 1;
          if (token.type === "Punctuator" && token.value === ">")
            typeParameterOpeningLevel -= 1;
          return typeParameterOpeningLevel !== 0 ? false : utils.isOpeningParenToken(token);
        };
      };
      switch (node.type) {
        case "NewExpression":
          if (!node.arguments.length && !(utils.isOpeningParenToken(sourceCode.getLastToken(node, { skip: 1 })) && utils.isClosingParenToken(sourceCode.getLastToken(node)) && node.callee.range[1] < node.range[1])) {
            return null;
          }
        case "CallExpression":
          return {
            leftParen: sourceCode.getTokenAfter(node.callee, isOpeningParenTokenOutsideTypeParameter()),
            rightParen: sourceCode.getLastToken(node)
          };
        case "FunctionDeclaration":
        case "FunctionExpression": {
          const leftParen = sourceCode.getFirstToken(node, isOpeningParenTokenOutsideTypeParameter());
          const rightParen = node.params.length ? sourceCode.getTokenAfter(node.params[node.params.length - 1], utils.isClosingParenToken) : sourceCode.getTokenAfter(leftParen);
          return { leftParen, rightParen };
        }
        case "ArrowFunctionExpression": {
          const firstToken = sourceCode.getFirstToken(node, { skip: node.async ? 1 : 0 });
          if (!utils.isOpeningParenToken(firstToken)) {
            return null;
          }
          const rightParen = node.params.length ? sourceCode.getTokenAfter(node.params[node.params.length - 1], utils.isClosingParenToken) : sourceCode.getTokenAfter(firstToken);
          return {
            leftParen: firstToken,
            rightParen
          };
        }
        case "ImportExpression": {
          const leftParen = sourceCode.getFirstToken(node, 1);
          const rightParen = sourceCode.getLastToken(node);
          return { leftParen, rightParen };
        }
        default:
          throw new TypeError(`unexpected node with type ${node.type}`);
      }
    }
    return {
      [[
        "ArrowFunctionExpression",
        "CallExpression",
        "FunctionDeclaration",
        "FunctionExpression",
        "ImportExpression",
        "NewExpression"
      ].join(", ")](node) {
        const parens = getParenTokens(node);
        let params;
        if (node.type === "ImportExpression")
          params = [node.source];
        else if (utils.isFunction(node))
          params = node.params;
        else
          params = node.arguments;
        if (parens) {
          validateParens(parens, params);
          if (multilineArgumentsOption)
            validateArguments(parens, params);
        }
      }
    };
  }
});

exports.functionParenNewline = functionParenNewline;
