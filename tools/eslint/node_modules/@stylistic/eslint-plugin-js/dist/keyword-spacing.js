'use strict';

var utils = require('./utils.js');

const PREV_TOKEN = /^[)\]}>]$/u;
const NEXT_TOKEN = /^(?:[([{<~!]|\+\+?|--?)$/u;
const PREV_TOKEN_M = /^[)\]}>*]$/u;
const NEXT_TOKEN_M = /^[{*]$/u;
const TEMPLATE_OPEN_PAREN = /\$\{$/u;
const TEMPLATE_CLOSE_PAREN = /^\}/u;
const CHECK_TYPE = /^(?:JSXElement|RegularExpression|String|Template|PrivateIdentifier)$/u;
const KEYS = utils.keywords.concat(["as", "async", "await", "from", "get", "let", "of", "set", "yield"]);
(function() {
  KEYS.sort();
  for (let i = 1; i < KEYS.length; ++i) {
    if (KEYS[i] === KEYS[i - 1])
      throw new Error(`Duplication was found in the keyword list: ${KEYS[i]}`);
  }
})();
function isOpenParenOfTemplate(token) {
  return token.type === "Template" && TEMPLATE_OPEN_PAREN.test(token.value);
}
function isCloseParenOfTemplate(token) {
  return token.type === "Template" && TEMPLATE_CLOSE_PAREN.test(token.value);
}
var keywordSpacing = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Enforce consistent spacing before and after keywords",
      url: "https://eslint.style/rules/js/keyword-spacing"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "object",
        properties: {
          before: { type: "boolean", default: true },
          after: { type: "boolean", default: true },
          overrides: {
            type: "object",
            properties: KEYS.reduce((retv, key) => {
              retv[key] = {
                type: "object",
                properties: {
                  before: { type: "boolean" },
                  after: { type: "boolean" }
                },
                additionalProperties: false
              };
              return retv;
            }, {}),
            additionalProperties: false
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      expectedBefore: 'Expected space(s) before "{{value}}".',
      expectedAfter: 'Expected space(s) after "{{value}}".',
      unexpectedBefore: 'Unexpected space(s) before "{{value}}".',
      unexpectedAfter: 'Unexpected space(s) after "{{value}}".'
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const tokensToIgnore = /* @__PURE__ */ new WeakSet();
    function expectSpaceBefore(token, pattern) {
      const prevToken = sourceCode.getTokenBefore(token);
      if (prevToken && (CHECK_TYPE.test(prevToken.type) || pattern.test(prevToken.value)) && !isOpenParenOfTemplate(prevToken) && !tokensToIgnore.has(prevToken) && utils.isTokenOnSameLine(prevToken, token) && !sourceCode.isSpaceBetweenTokens(prevToken, token)) {
        context.report({
          loc: token.loc,
          messageId: "expectedBefore",
          // @ts-expect-error index signature for string is missing
          data: token,
          fix(fixer) {
            return fixer.insertTextBefore(token, " ");
          }
        });
      }
    }
    function unexpectSpaceBefore(token, pattern) {
      const prevToken = sourceCode.getTokenBefore(token);
      if (prevToken && (CHECK_TYPE.test(prevToken.type) || pattern.test(prevToken.value)) && !isOpenParenOfTemplate(prevToken) && !tokensToIgnore.has(prevToken) && utils.isTokenOnSameLine(prevToken, token) && sourceCode.isSpaceBetweenTokens(prevToken, token)) {
        context.report({
          loc: { start: prevToken.loc.end, end: token.loc.start },
          messageId: "unexpectedBefore",
          // @ts-expect-error index signature for string is missing
          data: token,
          fix(fixer) {
            return fixer.removeRange([prevToken.range[1], token.range[0]]);
          }
        });
      }
    }
    function expectSpaceAfter(token, pattern) {
      const nextToken = sourceCode.getTokenAfter(token);
      if (nextToken && (CHECK_TYPE.test(nextToken.type) || pattern.test(nextToken.value)) && !isCloseParenOfTemplate(nextToken) && !tokensToIgnore.has(nextToken) && utils.isTokenOnSameLine(token, nextToken) && !sourceCode.isSpaceBetweenTokens(token, nextToken)) {
        context.report({
          loc: token.loc,
          messageId: "expectedAfter",
          // @ts-expect-error index signature for string is missing
          data: token,
          fix(fixer) {
            return fixer.insertTextAfter(token, " ");
          }
        });
      }
    }
    function unexpectSpaceAfter(token, pattern) {
      const nextToken = sourceCode.getTokenAfter(token);
      if (nextToken && (CHECK_TYPE.test(nextToken.type) || pattern.test(nextToken.value)) && !isCloseParenOfTemplate(nextToken) && !tokensToIgnore.has(nextToken) && utils.isTokenOnSameLine(token, nextToken) && sourceCode.isSpaceBetweenTokens(token, nextToken)) {
        context.report({
          loc: { start: token.loc.end, end: nextToken.loc.start },
          messageId: "unexpectedAfter",
          // @ts-expect-error index signature for string is missing
          data: token,
          fix(fixer) {
            return fixer.removeRange([token.range[1], nextToken.range[0]]);
          }
        });
      }
    }
    function parseOptions(options = {}) {
      const before = options.before !== false;
      const after = options.after !== false;
      const defaultValue = {
        before: before ? expectSpaceBefore : unexpectSpaceBefore,
        after: after ? expectSpaceAfter : unexpectSpaceAfter
      };
      const overrides = options && options.overrides || {};
      const retv = /* @__PURE__ */ Object.create(null);
      for (let i = 0; i < KEYS.length; ++i) {
        const key = KEYS[i];
        const override = overrides[key];
        if (override) {
          const thisBefore = "before" in override ? override.before : before;
          const thisAfter = "after" in override ? override.after : after;
          retv[key] = {
            before: thisBefore ? expectSpaceBefore : unexpectSpaceBefore,
            after: thisAfter ? expectSpaceAfter : unexpectSpaceAfter
          };
        } else {
          retv[key] = defaultValue;
        }
      }
      return retv;
    }
    const checkMethodMap = parseOptions(context.options[0]);
    function checkSpacingBefore(token, pattern) {
      checkMethodMap[token.value].before(token, pattern || PREV_TOKEN);
    }
    function checkSpacingAfter(token, pattern) {
      checkMethodMap[token.value].after(token, pattern || NEXT_TOKEN);
    }
    function checkSpacingAround(token) {
      checkSpacingBefore(token);
      checkSpacingAfter(token);
    }
    function checkSpacingAroundFirstToken(node) {
      const firstToken = node && sourceCode.getFirstToken(node);
      if (firstToken && firstToken.type === "Keyword")
        checkSpacingAround(firstToken);
    }
    function checkSpacingBeforeFirstToken(node) {
      const firstToken = node && sourceCode.getFirstToken(node);
      if (firstToken && firstToken.type === "Keyword")
        checkSpacingBefore(firstToken);
    }
    function checkSpacingAroundTokenBefore(node) {
      if (node) {
        const token = sourceCode.getTokenBefore(node, utils.isKeywordToken);
        if (token)
          checkSpacingAround(token);
      }
    }
    function checkSpacingForFunction(node) {
      const firstToken = node && sourceCode.getFirstToken(node);
      if (firstToken && (firstToken.type === "Keyword" && firstToken.value === "function" || firstToken.value === "async")) {
        checkSpacingBefore(firstToken);
      }
    }
    function checkSpacingForClass(node) {
      checkSpacingAroundFirstToken(node);
      checkSpacingAroundTokenBefore(node.superClass);
    }
    function checkSpacingForIfStatement(node) {
      checkSpacingAroundFirstToken(node);
      checkSpacingAroundTokenBefore(node.alternate);
    }
    function checkSpacingForTryStatement(node) {
      checkSpacingAroundFirstToken(node);
      checkSpacingAroundFirstToken(node.handler);
      checkSpacingAroundTokenBefore(node.finalizer);
    }
    function checkSpacingForDoWhileStatement(node) {
      checkSpacingAroundFirstToken(node);
      checkSpacingAroundTokenBefore(node.test);
    }
    function checkSpacingForForInStatement(node) {
      checkSpacingAroundFirstToken(node);
      const inToken = sourceCode.getTokenBefore(node.right, utils.isNotOpeningParenToken);
      const previousToken = sourceCode.getTokenBefore(inToken);
      if (previousToken.type !== "PrivateIdentifier")
        checkSpacingBefore(inToken);
      checkSpacingAfter(inToken);
    }
    function checkSpacingForForOfStatement(node) {
      if (node.await) {
        checkSpacingBefore(sourceCode.getFirstToken(node, 0));
        checkSpacingAfter(sourceCode.getFirstToken(node, 1));
      } else {
        checkSpacingAroundFirstToken(node);
      }
      const ofToken = sourceCode.getTokenBefore(node.right, utils.isNotOpeningParenToken);
      const previousToken = sourceCode.getTokenBefore(ofToken);
      if (previousToken.type !== "PrivateIdentifier")
        checkSpacingBefore(ofToken);
      checkSpacingAfter(ofToken);
    }
    function checkSpacingForModuleDeclaration(node) {
      const firstToken = sourceCode.getFirstToken(node);
      checkSpacingBefore(firstToken, PREV_TOKEN_M);
      checkSpacingAfter(firstToken, NEXT_TOKEN_M);
      if (node.type === "ExportDefaultDeclaration")
        checkSpacingAround(sourceCode.getTokenAfter(firstToken));
      if (node.type === "ExportAllDeclaration" && node.exported) {
        const asToken = sourceCode.getTokenBefore(node.exported);
        checkSpacingBefore(asToken, PREV_TOKEN_M);
        checkSpacingAfter(asToken, NEXT_TOKEN_M);
      }
      if ("source" in node && node.source) {
        const fromToken = sourceCode.getTokenBefore(node.source);
        checkSpacingBefore(fromToken, PREV_TOKEN_M);
        checkSpacingAfter(fromToken, NEXT_TOKEN_M);
      }
    }
    function checkSpacingForImportSpecifier(node) {
      if (node.imported.range[0] !== node.local.range[0]) {
        const asToken = sourceCode.getTokenBefore(node.local);
        checkSpacingBefore(asToken, PREV_TOKEN_M);
      }
    }
    function checkSpacingForExportSpecifier(node) {
      if (node.local.range[0] !== node.exported.range[0]) {
        const asToken = sourceCode.getTokenBefore(node.exported);
        checkSpacingBefore(asToken, PREV_TOKEN_M);
        checkSpacingAfter(asToken, NEXT_TOKEN_M);
      }
    }
    function checkSpacingForImportNamespaceSpecifier(node) {
      const asToken = sourceCode.getFirstToken(node, 1);
      checkSpacingBefore(asToken, PREV_TOKEN_M);
    }
    function checkSpacingForProperty(node) {
      if ("static" in node && node.static)
        checkSpacingAroundFirstToken(node);
      if (node.kind === "get" || node.kind === "set" || ("method" in node && node.method || node.type === "MethodDefinition") && "async" in node.value && node.value.async) {
        const token = sourceCode.getTokenBefore(
          node.key,
          (tok) => {
            switch (tok.value) {
              case "get":
              case "set":
              case "async":
                return true;
              default:
                return false;
            }
          }
        );
        if (!token)
          throw new Error("Failed to find token get, set, or async beside method name");
        checkSpacingAround(token);
      }
    }
    function checkSpacingForAwaitExpression(node) {
      checkSpacingBefore(sourceCode.getFirstToken(node));
    }
    return {
      // Statements
      "DebuggerStatement": checkSpacingAroundFirstToken,
      "WithStatement": checkSpacingAroundFirstToken,
      // Statements - Control flow
      "BreakStatement": checkSpacingAroundFirstToken,
      "ContinueStatement": checkSpacingAroundFirstToken,
      "ReturnStatement": checkSpacingAroundFirstToken,
      "ThrowStatement": checkSpacingAroundFirstToken,
      "TryStatement": checkSpacingForTryStatement,
      // Statements - Choice
      "IfStatement": checkSpacingForIfStatement,
      "SwitchStatement": checkSpacingAroundFirstToken,
      "SwitchCase": checkSpacingAroundFirstToken,
      // Statements - Loops
      "DoWhileStatement": checkSpacingForDoWhileStatement,
      "ForInStatement": checkSpacingForForInStatement,
      "ForOfStatement": checkSpacingForForOfStatement,
      "ForStatement": checkSpacingAroundFirstToken,
      "WhileStatement": checkSpacingAroundFirstToken,
      // Statements - Declarations
      "ClassDeclaration": checkSpacingForClass,
      "ExportNamedDeclaration": checkSpacingForModuleDeclaration,
      "ExportDefaultDeclaration": checkSpacingForModuleDeclaration,
      "ExportAllDeclaration": checkSpacingForModuleDeclaration,
      "FunctionDeclaration": checkSpacingForFunction,
      "ImportDeclaration": checkSpacingForModuleDeclaration,
      "VariableDeclaration": checkSpacingAroundFirstToken,
      // Expressions
      "ArrowFunctionExpression": checkSpacingForFunction,
      "AwaitExpression": checkSpacingForAwaitExpression,
      "ClassExpression": checkSpacingForClass,
      "FunctionExpression": checkSpacingForFunction,
      "NewExpression": checkSpacingBeforeFirstToken,
      "Super": checkSpacingBeforeFirstToken,
      "ThisExpression": checkSpacingBeforeFirstToken,
      "UnaryExpression": checkSpacingBeforeFirstToken,
      "YieldExpression": checkSpacingBeforeFirstToken,
      // Others
      "ImportSpecifier": checkSpacingForImportSpecifier,
      "ExportSpecifier": checkSpacingForExportSpecifier,
      "ImportNamespaceSpecifier": checkSpacingForImportNamespaceSpecifier,
      "MethodDefinition": checkSpacingForProperty,
      "PropertyDefinition": checkSpacingForProperty,
      "StaticBlock": checkSpacingAroundFirstToken,
      "Property": checkSpacingForProperty,
      // To avoid conflicts with `space-infix-ops`, e.g. `a > this.b`
      "BinaryExpression[operator='>']": function(node) {
        const operatorToken = sourceCode.getTokenBefore(node.right, utils.isNotOpeningParenToken);
        tokensToIgnore.add(operatorToken);
      }
    };
  }
});

exports.keywordSpacing = keywordSpacing;
