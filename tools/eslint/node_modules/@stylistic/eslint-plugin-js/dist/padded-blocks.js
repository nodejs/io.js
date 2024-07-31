'use strict';

var utils = require('./utils.js');

var paddedBlocks = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow padding within blocks",
      url: "https://eslint.style/rules/js/padded-blocks"
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
              blocks: {
                type: "string",
                enum: ["always", "never"]
              },
              switches: {
                type: "string",
                enum: ["always", "never"]
              },
              classes: {
                type: "string",
                enum: ["always", "never"]
              }
            },
            additionalProperties: false,
            minProperties: 1
          }
        ]
      },
      {
        type: "object",
        properties: {
          allowSingleLineBlocks: {
            type: "boolean"
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      alwaysPadBlock: "Block must be padded by blank lines.",
      neverPadBlock: "Block must not be padded by blank lines."
    }
  },
  create(context) {
    const options = {};
    const typeOptions = context.options[0] || "always";
    const exceptOptions = context.options[1] || {};
    if (typeof typeOptions === "string") {
      const shouldHavePadding = typeOptions === "always";
      options.blocks = shouldHavePadding;
      options.switches = shouldHavePadding;
      options.classes = shouldHavePadding;
    } else {
      if (Object.prototype.hasOwnProperty.call(typeOptions, "blocks"))
        options.blocks = typeOptions.blocks === "always";
      if (Object.prototype.hasOwnProperty.call(typeOptions, "switches"))
        options.switches = typeOptions.switches === "always";
      if (Object.prototype.hasOwnProperty.call(typeOptions, "classes"))
        options.classes = typeOptions.classes === "always";
    }
    if (Object.prototype.hasOwnProperty.call(exceptOptions, "allowSingleLineBlocks"))
      options.allowSingleLineBlocks = exceptOptions.allowSingleLineBlocks === true;
    const sourceCode = context.sourceCode;
    function getOpenBrace(node) {
      if (node.type === "SwitchStatement")
        return sourceCode.getTokenBefore(node.cases[0]);
      if (node.type === "StaticBlock")
        return sourceCode.getFirstToken(node, { skip: 1 });
      return sourceCode.getFirstToken(node);
    }
    function isComment(node) {
      return node.type === "Line" || node.type === "Block";
    }
    function isPaddingBetweenTokens(first, second) {
      return second.loc.start.line - first.loc.end.line >= 2;
    }
    function getFirstBlockToken(token) {
      let prev;
      let first = token;
      do {
        prev = first;
        first = sourceCode.getTokenAfter(first, { includeComments: true });
      } while (isComment(first) && first.loc.start.line === prev.loc.end.line);
      return first;
    }
    function getLastBlockToken(token) {
      let last = token;
      let next;
      do {
        next = last;
        last = sourceCode.getTokenBefore(last, { includeComments: true });
      } while (isComment(last) && last.loc.end.line === next.loc.start.line);
      return last;
    }
    function requirePaddingFor(node) {
      switch (node.type) {
        case "BlockStatement":
        case "StaticBlock":
          return options.blocks;
        case "SwitchStatement":
          return options.switches;
        case "ClassBody":
          return options.classes;
        default:
          throw new Error("unreachable");
      }
    }
    function checkPadding(node) {
      const openBrace = getOpenBrace(node);
      const firstBlockToken = getFirstBlockToken(openBrace);
      const tokenBeforeFirst = sourceCode.getTokenBefore(firstBlockToken, { includeComments: true });
      const closeBrace = sourceCode.getLastToken(node);
      const lastBlockToken = getLastBlockToken(closeBrace);
      const tokenAfterLast = sourceCode.getTokenAfter(lastBlockToken, { includeComments: true });
      const blockHasTopPadding = isPaddingBetweenTokens(tokenBeforeFirst, firstBlockToken);
      const blockHasBottomPadding = isPaddingBetweenTokens(lastBlockToken, tokenAfterLast);
      if (options.allowSingleLineBlocks && utils.isTokenOnSameLine(tokenBeforeFirst, tokenAfterLast))
        return;
      if (requirePaddingFor(node)) {
        if (!blockHasTopPadding) {
          context.report({
            node,
            loc: {
              start: tokenBeforeFirst.loc.start,
              end: firstBlockToken.loc.start
            },
            fix(fixer) {
              return fixer.insertTextAfter(tokenBeforeFirst, "\n");
            },
            messageId: "alwaysPadBlock"
          });
        }
        if (!blockHasBottomPadding) {
          context.report({
            node,
            loc: {
              end: tokenAfterLast.loc.start,
              start: lastBlockToken.loc.end
            },
            fix(fixer) {
              return fixer.insertTextBefore(tokenAfterLast, "\n");
            },
            messageId: "alwaysPadBlock"
          });
        }
      } else {
        if (blockHasTopPadding) {
          context.report({
            node,
            loc: {
              start: tokenBeforeFirst.loc.start,
              end: firstBlockToken.loc.start
            },
            fix(fixer) {
              return fixer.replaceTextRange([tokenBeforeFirst.range[1], firstBlockToken.range[0] - firstBlockToken.loc.start.column], "\n");
            },
            messageId: "neverPadBlock"
          });
        }
        if (blockHasBottomPadding) {
          context.report({
            node,
            loc: {
              end: tokenAfterLast.loc.start,
              start: lastBlockToken.loc.end
            },
            messageId: "neverPadBlock",
            fix(fixer) {
              return fixer.replaceTextRange([lastBlockToken.range[1], tokenAfterLast.range[0] - tokenAfterLast.loc.start.column], "\n");
            }
          });
        }
      }
    }
    const rule = {};
    if (Object.prototype.hasOwnProperty.call(options, "switches")) {
      rule.SwitchStatement = function(node) {
        if (node.cases.length === 0)
          return;
        checkPadding(node);
      };
    }
    if (Object.prototype.hasOwnProperty.call(options, "blocks")) {
      rule.BlockStatement = function(node) {
        if (node.body.length === 0)
          return;
        checkPadding(node);
      };
      rule.StaticBlock = rule.BlockStatement;
    }
    if (Object.prototype.hasOwnProperty.call(options, "classes")) {
      rule.ClassBody = function(node) {
        if (node.body.length === 0)
          return;
        checkPadding(node);
      };
    }
    return rule;
  }
});

exports.paddedBlocks = paddedBlocks;
