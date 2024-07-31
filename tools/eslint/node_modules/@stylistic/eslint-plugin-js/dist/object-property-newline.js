'use strict';

var utils = require('./utils.js');

var objectPropertyNewline = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce placing object properties on separate lines",
      url: "https://eslint.style/rules/js/object-property-newline"
    },
    schema: [
      {
        type: "object",
        properties: {
          allowAllPropertiesOnSameLine: {
            type: "boolean",
            default: false
          },
          allowMultiplePropertiesPerLine: {
            // Deprecated
            type: "boolean",
            default: false
          }
        },
        additionalProperties: false
      }
    ],
    fixable: "whitespace",
    messages: {
      propertiesOnNewlineAll: "Object properties must go on a new line if they aren't all on the same line.",
      propertiesOnNewline: "Object properties must go on a new line."
    }
  },
  create(context) {
    const allowSameLine = context.options[0] && (context.options[0].allowAllPropertiesOnSameLine || context.options[0].allowMultiplePropertiesPerLine);
    const messageId = allowSameLine ? "propertiesOnNewlineAll" : "propertiesOnNewline";
    const sourceCode = context.sourceCode;
    return {
      ObjectExpression(node) {
        if (allowSameLine) {
          if (node.properties.length > 1) {
            const firstTokenOfFirstProperty = sourceCode.getFirstToken(node.properties[0]);
            const lastTokenOfLastProperty = sourceCode.getLastToken(node.properties[node.properties.length - 1]);
            if (firstTokenOfFirstProperty.loc.end.line === lastTokenOfLastProperty.loc.start.line) {
              return;
            }
          }
        }
        for (let i = 1; i < node.properties.length; i++) {
          const lastTokenOfPreviousProperty = sourceCode.getLastToken(node.properties[i - 1]);
          const firstTokenOfCurrentProperty = sourceCode.getFirstToken(node.properties[i]);
          if (lastTokenOfPreviousProperty.loc.end.line === firstTokenOfCurrentProperty.loc.start.line) {
            context.report({
              node,
              loc: firstTokenOfCurrentProperty.loc,
              messageId,
              fix(fixer) {
                const comma = sourceCode.getTokenBefore(firstTokenOfCurrentProperty);
                const rangeAfterComma = [comma.range[1], firstTokenOfCurrentProperty.range[0]];
                if (sourceCode.text.slice(rangeAfterComma[0], rangeAfterComma[1]).trim())
                  return null;
                return fixer.replaceTextRange(rangeAfterComma, "\n");
              }
            });
          }
        }
      }
    };
  }
});

exports.objectPropertyNewline = objectPropertyNewline;
