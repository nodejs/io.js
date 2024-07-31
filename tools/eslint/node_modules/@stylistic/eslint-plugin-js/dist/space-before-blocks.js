'use strict';

var utils = require('./utils.js');

function isFunctionBody(node) {
  const parent = node.parent;
  return node.type === "BlockStatement" && utils.isFunction(parent) && parent.body === node;
}
var spaceBeforeBlocks = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing before blocks",
      url: "https://eslint.style/rules/js/space-before-blocks"
    },
    fixable: "whitespace",
    schema: [
      {
        oneOf: [
          {
            type: "string",
            enum: ["always", "never"]
          },
          {
            type: "object",
            properties: {
              keywords: {
                type: "string",
                enum: ["always", "never", "off"]
              },
              functions: {
                type: "string",
                enum: ["always", "never", "off"]
              },
              classes: {
                type: "string",
                enum: ["always", "never", "off"]
              }
            },
            additionalProperties: false
          }
        ]
      }
    ],
    messages: {
      unexpectedSpace: "Unexpected space before opening brace.",
      missingSpace: "Missing space before opening brace."
    }
  },
  create(context) {
    const config = context.options[0];
    const sourceCode = context.sourceCode;
    let alwaysFunctions = true;
    let alwaysKeywords = true;
    let alwaysClasses = true;
    let neverFunctions = false;
    let neverKeywords = false;
    let neverClasses = false;
    if (typeof config === "object") {
      alwaysFunctions = config.functions === "always";
      alwaysKeywords = config.keywords === "always";
      alwaysClasses = config.classes === "always";
      neverFunctions = config.functions === "never";
      neverKeywords = config.keywords === "never";
      neverClasses = config.classes === "never";
    } else if (config === "never") {
      alwaysFunctions = false;
      alwaysKeywords = false;
      alwaysClasses = false;
      neverFunctions = true;
      neverKeywords = true;
      neverClasses = true;
    }
    function isConflicted(precedingToken, node) {
      return utils.isArrowToken(precedingToken) || utils.isKeywordToken(precedingToken) && !isFunctionBody(node) || utils.isColonToken(precedingToken) && "parent" in node && node.parent && node.parent.type === "SwitchCase" && precedingToken === utils.getSwitchCaseColonToken(node.parent, sourceCode);
    }
    function checkPrecedingSpace(node) {
      const precedingToken = sourceCode.getTokenBefore(node);
      if (precedingToken && !isConflicted(precedingToken, node) && utils.isTokenOnSameLine(precedingToken, node)) {
        const hasSpace = sourceCode.isSpaceBetweenTokens(precedingToken, node);
        let requireSpace;
        let requireNoSpace;
        if (isFunctionBody(node)) {
          requireSpace = alwaysFunctions;
          requireNoSpace = neverFunctions;
        } else if (node.type === "ClassBody") {
          requireSpace = alwaysClasses;
          requireNoSpace = neverClasses;
        } else {
          requireSpace = alwaysKeywords;
          requireNoSpace = neverKeywords;
        }
        if (requireSpace && !hasSpace) {
          context.report({
            node,
            messageId: "missingSpace",
            fix(fixer) {
              return fixer.insertTextBefore(node, " ");
            }
          });
        } else if (requireNoSpace && hasSpace) {
          context.report({
            node,
            messageId: "unexpectedSpace",
            fix(fixer) {
              return fixer.removeRange([precedingToken.range[1], node.range[0]]);
            }
          });
        }
      }
    }
    function checkSpaceBeforeCaseBlock(node) {
      const cases = node.cases;
      let openingBrace;
      if (cases.length > 0)
        openingBrace = sourceCode.getTokenBefore(cases[0]);
      else
        openingBrace = sourceCode.getLastToken(node, 1);
      checkPrecedingSpace(openingBrace);
    }
    return {
      BlockStatement: checkPrecedingSpace,
      ClassBody: checkPrecedingSpace,
      SwitchStatement: checkSpaceBeforeCaseBlock
    };
  }
});

exports.spaceBeforeBlocks = spaceBeforeBlocks;
