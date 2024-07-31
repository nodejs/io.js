'use strict';

var utils = require('./utils.js');

function switchQuote(str) {
  const newQuote = this.quote;
  const oldQuote = str[0];
  if (newQuote === oldQuote)
    return str;
  return newQuote + str.slice(1, -1).replace(/\\(\$\{|\r\n?|\n|.)|["'`]|\$\{|(\r\n?|\n)/gu, (match, escaped, newline) => {
    if (escaped === oldQuote || oldQuote === "`" && escaped === "${")
      return escaped;
    if (match === newQuote || newQuote === "`" && match === "${")
      return `\\${match}`;
    if (newline && oldQuote === "`")
      return "\\n";
    return match;
  }) + newQuote;
}
const QUOTE_SETTINGS = {
  double: {
    quote: '"',
    alternateQuote: "'",
    description: "doublequote",
    convert: switchQuote
  },
  single: {
    quote: "'",
    alternateQuote: '"',
    description: "singlequote",
    convert: switchQuote
  },
  backtick: {
    quote: "`",
    alternateQuote: '"',
    description: "backtick",
    convert: switchQuote
  }
};
const UNESCAPED_LINEBREAK_PATTERN = new RegExp(String.raw`(^|[^\\])(\\\\)*[${Array.from(utils.LINEBREAKS).join("")}]`, "u");
const AVOID_ESCAPE = "avoid-escape";
var quotes = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce the consistent use of either backticks, double, or single quotes",
      url: "https://eslint.style/rules/js/quotes"
    },
    fixable: "code",
    schema: [
      {
        type: "string",
        enum: ["single", "double", "backtick"]
      },
      {
        anyOf: [
          {
            type: "string",
            enum: ["avoid-escape"]
          },
          {
            type: "object",
            properties: {
              avoidEscape: {
                type: "boolean"
              },
              allowTemplateLiterals: {
                type: "boolean"
              },
              ignoreStringLiterals: {
                type: "boolean"
              }
            },
            additionalProperties: false
          }
        ]
      }
    ],
    messages: {
      wrongQuotes: "Strings must use {{description}}."
    }
  },
  create(context) {
    const quoteOption = context.options[0];
    const settings = QUOTE_SETTINGS[quoteOption || "double"];
    const options = context.options[1];
    const allowTemplateLiterals = options && typeof options === "object" && options.allowTemplateLiterals === true;
    const ignoreStringLiterals = options && typeof options === "object" && options.ignoreStringLiterals === true;
    const sourceCode = context.sourceCode;
    let avoidEscape = options && typeof options === "object" && options.avoidEscape === true;
    if (options === AVOID_ESCAPE)
      avoidEscape = true;
    function isJSXLiteral(node) {
      if (!node.parent)
        return false;
      return node.parent.type === "JSXAttribute" || node.parent.type === "JSXElement" || node.parent.type === "JSXFragment";
    }
    function isDirective(node) {
      return node.type === "ExpressionStatement" && node.expression.type === "Literal" && typeof node.expression.value === "string" && !utils.isParenthesised(sourceCode, node.expression);
    }
    function isExpressionInOrJustAfterDirectivePrologue(node) {
      if (!node.parent)
        return false;
      if (!utils.isTopLevelExpressionStatement(node.parent))
        return false;
      const block = node.parent.parent;
      if (!block || !("body" in block) || !Array.isArray(block.body))
        return false;
      for (let i = 0; i < block.body.length; ++i) {
        const statement = block.body[i];
        if (statement === node.parent)
          return true;
        if (!isDirective(statement))
          break;
      }
      return false;
    }
    function isAllowedAsNonBacktick(node) {
      const parent = node.parent;
      if (!parent)
        return false;
      switch (parent.type) {
        case "ExpressionStatement":
          return !utils.isParenthesised(sourceCode, node) && isExpressionInOrJustAfterDirectivePrologue(node);
        case "Property":
        case "PropertyDefinition":
        case "MethodDefinition":
          return parent.key === node && !parent.computed;
        case "ImportDeclaration":
        case "ExportNamedDeclaration":
          return parent.source === node;
        case "ExportAllDeclaration":
          return parent.exported === node || parent.source === node;
        case "ImportSpecifier":
          return parent.imported === node;
        case "ExportSpecifier":
          return parent.local === node || parent.exported === node;
        case "ImportAttribute":
          return parent.value === node;
        default:
          return false;
      }
    }
    function isUsingFeatureOfTemplateLiteral(node) {
      const hasTag = node.parent.type === "TaggedTemplateExpression" && node === node.parent.quasi;
      if (hasTag)
        return true;
      const hasStringInterpolation = node.expressions.length > 0;
      if (hasStringInterpolation)
        return true;
      const isMultilineString = node.quasis.length >= 1 && UNESCAPED_LINEBREAK_PATTERN.test(node.quasis[0].value.raw);
      if (isMultilineString)
        return true;
      return false;
    }
    return {
      Literal(node) {
        if (ignoreStringLiterals)
          return;
        const val = node.value;
        const rawVal = node.raw;
        if (settings && typeof val === "string") {
          let isValid = quoteOption === "backtick" && isAllowedAsNonBacktick(node) || isJSXLiteral(node) || utils.isSurroundedBy(rawVal, settings.quote);
          if (!isValid && avoidEscape)
            isValid = utils.isSurroundedBy(rawVal, settings.alternateQuote) && rawVal.includes(settings.quote);
          if (!isValid) {
            context.report({
              node,
              messageId: "wrongQuotes",
              data: {
                description: settings.description
              },
              fix(fixer) {
                if (quoteOption === "backtick" && utils.hasOctalOrNonOctalDecimalEscapeSequence(rawVal)) {
                  return null;
                }
                return fixer.replaceText(node, settings.convert(node.raw));
              }
            });
          }
        }
      },
      TemplateLiteral(node) {
        if (allowTemplateLiterals || quoteOption === "backtick" || isUsingFeatureOfTemplateLiteral(node)) {
          return;
        }
        if (avoidEscape && sourceCode.getText(node).includes(settings.quote))
          return;
        context.report({
          node,
          messageId: "wrongQuotes",
          data: {
            description: settings.description
          },
          fix(fixer) {
            if (utils.isTopLevelExpressionStatement(node.parent) && !utils.isParenthesised(sourceCode, node)) {
              return null;
            }
            return fixer.replaceText(node, settings.convert(sourceCode.getText(node)));
          }
        });
      }
    };
  }
});

exports.quotes = quotes;
