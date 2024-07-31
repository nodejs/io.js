'use strict';

var utils = require('./utils.js');

var arrayElementNewline = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce line breaks after each array element",
      url: "https://eslint.style/rules/js/array-element-newline"
    },
    fixable: "whitespace",
    schema: {
      definitions: {
        basicConfig: {
          oneOf: [
            {
              type: "string",
              enum: ["always", "never", "consistent"]
            },
            {
              type: "object",
              properties: {
                consistent: {
                  type: "boolean"
                },
                multiline: {
                  type: "boolean"
                },
                minItems: {
                  type: ["integer", "null"],
                  minimum: 0
                }
              },
              additionalProperties: false
            }
          ]
        }
      },
      type: "array",
      items: [
        {
          oneOf: [
            {
              $ref: "#/definitions/basicConfig"
            },
            {
              type: "object",
              properties: {
                ArrayExpression: {
                  $ref: "#/definitions/basicConfig"
                },
                ArrayPattern: {
                  $ref: "#/definitions/basicConfig"
                }
              },
              additionalProperties: false,
              minProperties: 1
            }
          ]
        }
      ]
    },
    messages: {
      unexpectedLineBreak: "There should be no linebreak here.",
      missingLineBreak: "There should be a linebreak after this element."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    function normalizeOptionValue(providedOption) {
      let consistent = false;
      let multiline = false;
      let minItems;
      const option = providedOption || "always";
      if (!option || option === "always" || typeof option === "object" && option.minItems === 0) {
        minItems = 0;
      } else if (option === "never") {
        minItems = Number.POSITIVE_INFINITY;
      } else if (option === "consistent") {
        consistent = true;
        minItems = Number.POSITIVE_INFINITY;
      } else {
        consistent = Boolean(option.consistent);
        multiline = Boolean(option.multiline);
        minItems = option.minItems || Number.POSITIVE_INFINITY;
      }
      return { consistent, multiline, minItems };
    }
    function normalizeOptions(options) {
      if (options && (options.ArrayExpression || options.ArrayPattern)) {
        let expressionOptions, patternOptions;
        if (options.ArrayExpression)
          expressionOptions = normalizeOptionValue(options.ArrayExpression);
        if (options.ArrayPattern)
          patternOptions = normalizeOptionValue(options.ArrayPattern);
        return { ArrayExpression: expressionOptions, ArrayPattern: patternOptions };
      }
      const value = normalizeOptionValue(options);
      return { ArrayExpression: value, ArrayPattern: value };
    }
    function reportNoLineBreak(token) {
      const tokenBefore = sourceCode.getTokenBefore(token, { includeComments: true });
      context.report({
        loc: {
          start: tokenBefore.loc.end,
          end: token.loc.start
        },
        messageId: "unexpectedLineBreak",
        fix(fixer) {
          if (utils.isCommentToken(tokenBefore))
            return null;
          if (!utils.isTokenOnSameLine(tokenBefore, token))
            return fixer.replaceTextRange([tokenBefore.range[1], token.range[0]], " ");
          const twoTokensBefore = sourceCode.getTokenBefore(tokenBefore, { includeComments: true });
          if (utils.isCommentToken(twoTokensBefore))
            return null;
          return fixer.replaceTextRange([twoTokensBefore.range[1], tokenBefore.range[0]], "");
        }
      });
    }
    function reportRequiredLineBreak(token) {
      const tokenBefore = sourceCode.getTokenBefore(token, { includeComments: true });
      context.report({
        loc: {
          start: tokenBefore.loc.end,
          end: token.loc.start
        },
        messageId: "missingLineBreak",
        fix(fixer) {
          return fixer.replaceTextRange([tokenBefore.range[1], token.range[0]], "\n");
        }
      });
    }
    function check(node) {
      const elements = node.elements;
      const normalizedOptions = normalizeOptions(context.options[0]);
      const options = normalizedOptions[node.type];
      if (!options)
        return;
      let elementBreak = false;
      if (options.multiline) {
        elementBreak = elements.filter((element) => element !== null).some((element) => element.loc.start.line !== element.loc.end.line);
      }
      let linebreaksCount = 0;
      for (let i = 0; i < node.elements.length; i++) {
        const element = node.elements[i];
        const previousElement = elements[i - 1];
        if (i === 0 || element === null || previousElement === null)
          continue;
        const commaToken = sourceCode.getFirstTokenBetween(previousElement, element, utils.isCommaToken);
        const lastTokenOfPreviousElement = sourceCode.getTokenBefore(commaToken);
        const firstTokenOfCurrentElement = sourceCode.getTokenAfter(commaToken);
        if (!utils.isTokenOnSameLine(lastTokenOfPreviousElement, firstTokenOfCurrentElement))
          linebreaksCount++;
      }
      const needsLinebreaks = elements.length >= options.minItems || options.multiline && elementBreak || options.consistent && linebreaksCount > 0 && linebreaksCount < node.elements.length;
      elements.forEach((element, i) => {
        const previousElement = elements[i - 1];
        if (i === 0 || element === null || previousElement === null)
          return;
        const commaToken = sourceCode.getFirstTokenBetween(previousElement, element, utils.isCommaToken);
        const lastTokenOfPreviousElement = sourceCode.getTokenBefore(commaToken);
        const firstTokenOfCurrentElement = sourceCode.getTokenAfter(commaToken);
        if (needsLinebreaks) {
          if (utils.isTokenOnSameLine(lastTokenOfPreviousElement, firstTokenOfCurrentElement))
            reportRequiredLineBreak(firstTokenOfCurrentElement);
        } else {
          if (!utils.isTokenOnSameLine(lastTokenOfPreviousElement, firstTokenOfCurrentElement))
            reportNoLineBreak(firstTokenOfCurrentElement);
        }
      });
    }
    return {
      ArrayPattern: check,
      ArrayExpression: check
    };
  }
});

exports.arrayElementNewline = arrayElementNewline;
