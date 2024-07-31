'use strict';

var utils = require('./utils.js');

const DEFAULT_OPTIONS = Object.freeze({
  arrays: "never",
  objects: "never",
  imports: "never",
  exports: "never",
  functions: "never"
});
const closeBraces = ["}", "]", ")", ">"];
function isTrailingCommaAllowed(lastItem) {
  return lastItem.type !== "RestElement";
}
function normalizeOptions(optionValue, ecmaVersion) {
  if (typeof optionValue === "string") {
    return {
      arrays: optionValue,
      objects: optionValue,
      imports: optionValue,
      exports: optionValue,
      functions: !ecmaVersion || ecmaVersion === "latest" ? optionValue : ecmaVersion < 2017 ? "ignore" : optionValue
    };
  }
  if (typeof optionValue === "object" && optionValue !== null) {
    return {
      arrays: optionValue.arrays || DEFAULT_OPTIONS.arrays,
      objects: optionValue.objects || DEFAULT_OPTIONS.objects,
      imports: optionValue.imports || DEFAULT_OPTIONS.imports,
      exports: optionValue.exports || DEFAULT_OPTIONS.exports,
      functions: optionValue.functions || DEFAULT_OPTIONS.functions
    };
  }
  return DEFAULT_OPTIONS;
}
var commaDangle = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow trailing commas",
      url: "https://eslint.style/rules/js/comma-dangle"
    },
    fixable: "code",
    schema: {
      definitions: {
        value: {
          type: "string",
          enum: [
            "always-multiline",
            "always",
            "never",
            "only-multiline"
          ]
        },
        valueWithIgnore: {
          type: "string",
          enum: [
            "always-multiline",
            "always",
            "ignore",
            "never",
            "only-multiline"
          ]
        }
      },
      type: "array",
      items: [
        {
          oneOf: [
            {
              $ref: "#/definitions/value"
            },
            {
              type: "object",
              properties: {
                arrays: { $ref: "#/definitions/valueWithIgnore" },
                objects: { $ref: "#/definitions/valueWithIgnore" },
                imports: { $ref: "#/definitions/valueWithIgnore" },
                exports: { $ref: "#/definitions/valueWithIgnore" },
                functions: { $ref: "#/definitions/valueWithIgnore" }
              },
              additionalProperties: false
            }
          ]
        }
      ],
      additionalItems: false
    },
    messages: {
      unexpected: "Unexpected trailing comma.",
      missing: "Missing trailing comma."
    }
  },
  create(context) {
    const ecmaVersion = context?.languageOptions?.ecmaVersion ?? context.parserOptions.ecmaVersion;
    const options = normalizeOptions(context.options[0], ecmaVersion);
    const sourceCode = context.sourceCode;
    function getLastItem(node) {
      function last(array) {
        return array[array.length - 1];
      }
      switch (node.type) {
        case "ObjectExpression":
        case "ObjectPattern":
          return last(node.properties);
        case "ArrayExpression":
        case "ArrayPattern":
          return last(node.elements);
        case "ImportDeclaration":
        case "ExportNamedDeclaration":
          return last(node.specifiers);
        case "FunctionDeclaration":
        case "FunctionExpression":
        case "ArrowFunctionExpression":
          return last(node.params);
        case "CallExpression":
        case "NewExpression":
          return last(node.arguments);
        default:
          return null;
      }
    }
    function getTrailingToken(node, lastItem) {
      switch (node.type) {
        case "ObjectExpression":
        case "ArrayExpression":
        case "CallExpression":
        case "NewExpression":
          return sourceCode.getLastToken(node, 1);
        default: {
          const nextToken = sourceCode.getTokenAfter(lastItem);
          if (utils.isCommaToken(nextToken))
            return nextToken;
          return sourceCode.getLastToken(lastItem);
        }
      }
    }
    function isMultiline(node) {
      const lastItem = getLastItem(node);
      if (!lastItem)
        return false;
      const penultimateToken = getTrailingToken(node, lastItem);
      if (!penultimateToken)
        return false;
      const lastToken = sourceCode.getTokenAfter(penultimateToken);
      if (!lastToken)
        return false;
      return lastToken.loc.end.line !== penultimateToken.loc.end.line;
    }
    function forbidTrailingComma(node) {
      const lastItem = getLastItem(node);
      if (!lastItem || node.type === "ImportDeclaration" && lastItem.type !== "ImportSpecifier")
        return;
      const trailingToken = getTrailingToken(node, lastItem);
      if (trailingToken && utils.isCommaToken(trailingToken)) {
        context.report({
          node: lastItem,
          loc: trailingToken.loc,
          messageId: "unexpected",
          *fix(fixer) {
            yield fixer.remove(trailingToken);
            yield fixer.insertTextBefore(sourceCode.getTokenBefore(trailingToken), "");
            yield fixer.insertTextAfter(sourceCode.getTokenAfter(trailingToken), "");
          }
        });
      }
    }
    function forceTrailingComma(node) {
      const lastItem = getLastItem(node);
      if (!lastItem || node.type === "ImportDeclaration" && lastItem.type !== "ImportSpecifier")
        return;
      if (!isTrailingCommaAllowed(lastItem)) {
        forbidTrailingComma(node);
        return;
      }
      const trailingToken = getTrailingToken(node, lastItem);
      if (!trailingToken || trailingToken.value === ",")
        return;
      const nextToken = sourceCode.getTokenAfter(trailingToken);
      if (!nextToken || !closeBraces.includes(nextToken.value))
        return;
      context.report({
        node: lastItem,
        loc: {
          start: trailingToken.loc.end,
          end: utils.getNextLocation(sourceCode, trailingToken.loc.end)
        },
        messageId: "missing",
        *fix(fixer) {
          yield fixer.insertTextAfter(trailingToken, ",");
          yield fixer.insertTextBefore(trailingToken, "");
          yield fixer.insertTextAfter(sourceCode.getTokenAfter(trailingToken), "");
        }
      });
    }
    function forceTrailingCommaIfMultiline(node) {
      if (isMultiline(node))
        forceTrailingComma(node);
      else
        forbidTrailingComma(node);
    }
    function allowTrailingCommaIfMultiline(node) {
      if (!isMultiline(node))
        forbidTrailingComma(node);
    }
    const predicate = {
      "always": forceTrailingComma,
      "always-multiline": forceTrailingCommaIfMultiline,
      "only-multiline": allowTrailingCommaIfMultiline,
      "never": forbidTrailingComma,
      ignore() {
      }
    };
    return {
      ObjectExpression: predicate[options.objects],
      ObjectPattern: predicate[options.objects],
      ArrayExpression: predicate[options.arrays],
      ArrayPattern: predicate[options.arrays],
      ImportDeclaration: predicate[options.imports],
      ExportNamedDeclaration: predicate[options.exports],
      FunctionDeclaration: predicate[options.functions],
      FunctionExpression: predicate[options.functions],
      ArrowFunctionExpression: predicate[options.functions],
      CallExpression: predicate[options.functions],
      NewExpression: predicate[options.functions]
    };
  }
});

exports.commaDangle = commaDangle;
