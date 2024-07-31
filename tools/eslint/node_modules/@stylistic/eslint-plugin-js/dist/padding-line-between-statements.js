'use strict';

var utils = require('./utils.js');

const LT = `[${Array.from(utils.LINEBREAKS).join("")}]`;
const PADDING_LINE_SEQUENCE = new RegExp(
  String.raw`^(\s*?${LT})\s*${LT}(\s*;?)$`,
  "u"
);
const CJS_EXPORT = /^(?:module\s*\.\s*)?exports(?:\s*\.|\s*\[|$)/u;
const CJS_IMPORT = /^require\(/u;
function newKeywordTester(keyword) {
  return {
    test: (node, sourceCode) => sourceCode.getFirstToken(node)?.value === keyword
  };
}
function newSinglelineKeywordTester(keyword) {
  return {
    test: (node, sourceCode) => node.loc.start.line === node.loc.end.line && sourceCode.getFirstToken(node)?.value === keyword
  };
}
function newMultilineKeywordTester(keyword) {
  return {
    test: (node, sourceCode) => node.loc.start.line !== node.loc.end.line && sourceCode.getFirstToken(node)?.value === keyword
  };
}
function newNodeTypeTester(type) {
  return {
    test: (node) => node.type === type
  };
}
function isIIFEStatement(node) {
  if (node.type === "ExpressionStatement") {
    let call = utils.skipChainExpression(node.expression);
    if (call.type === "UnaryExpression")
      call = utils.skipChainExpression(call.argument);
    return call.type === "CallExpression" && utils.isFunction(call.callee);
  }
  return false;
}
function isBlockLikeStatement(sourceCode, node) {
  if (node.type === "DoWhileStatement" && node.body.type === "BlockStatement")
    return true;
  if (isIIFEStatement(node))
    return true;
  const lastToken = sourceCode.getLastToken(node, utils.isNotSemicolonToken);
  const belongingNode = lastToken && utils.isClosingBraceToken(lastToken) ? sourceCode.getNodeByRangeIndex(lastToken.range[0]) : null;
  return Boolean(belongingNode) && (belongingNode?.type === "BlockStatement" || belongingNode?.type === "SwitchStatement");
}
function getActualLastToken(sourceCode, node) {
  const semiToken = sourceCode.getLastToken(node);
  const prevToken = sourceCode.getTokenBefore(semiToken);
  const nextToken = sourceCode.getTokenAfter(semiToken);
  const isSemicolonLessStyle = Boolean(
    prevToken && nextToken && prevToken.range[0] >= node.range[0] && utils.isSemicolonToken(semiToken) && semiToken.loc.start.line !== prevToken.loc.end.line && semiToken.loc.end.line === nextToken.loc.start.line
  );
  return isSemicolonLessStyle ? prevToken : semiToken;
}
function replacerToRemovePaddingLines(_, trailingSpaces, indentSpaces) {
  return trailingSpaces + indentSpaces;
}
function verifyForAny() {
}
function verifyForNever(context, _, nextNode, paddingLines) {
  if (paddingLines.length === 0)
    return;
  context.report({
    node: nextNode,
    messageId: "unexpectedBlankLine",
    fix(fixer) {
      if (paddingLines.length >= 2)
        return null;
      const prevToken = paddingLines[0][0];
      const nextToken = paddingLines[0][1];
      const start = prevToken.range[1];
      const end = nextToken.range[0];
      const text = context.sourceCode.text.slice(start, end).replace(PADDING_LINE_SEQUENCE, replacerToRemovePaddingLines);
      return fixer.replaceTextRange([start, end], text);
    }
  });
}
function verifyForAlways(context, prevNode, nextNode, paddingLines) {
  if (paddingLines.length > 0)
    return;
  context.report({
    node: nextNode,
    messageId: "expectedBlankLine",
    fix(fixer) {
      const sourceCode = context.sourceCode;
      let prevToken = getActualLastToken(sourceCode, prevNode);
      const nextToken = sourceCode.getFirstTokenBetween(
        prevToken,
        nextNode,
        {
          includeComments: true,
          /**
           * Skip the trailing comments of the previous node.
           * This inserts a blank line after the last trailing comment.
           *
           * For example:
           *
           *     foo(); // trailing comment.
           *     // comment.
           *     bar();
           *
           * Get fixed to:
           *
           *     foo(); // trailing comment.
           *
           *     // comment.
           *     bar();
           * @param token The token to check.
           * @returns `true` if the token is not a trailing comment.
           * @private
           */
          filter(token) {
            if (utils.isTokenOnSameLine(prevToken, token)) {
              prevToken = token;
              return false;
            }
            return true;
          }
        }
      ) || nextNode;
      const insertText = utils.isTokenOnSameLine(prevToken, nextToken) ? "\n\n" : "\n";
      return fixer.insertTextAfter(prevToken, insertText);
    }
  });
}
const PaddingTypes = {
  any: { verify: verifyForAny },
  never: { verify: verifyForNever },
  always: { verify: verifyForAlways }
};
const StatementTypes = {
  "*": { test: () => true },
  "block-like": {
    test: (node, sourceCode) => isBlockLikeStatement(sourceCode, node)
  },
  "cjs-export": {
    test: (node, sourceCode) => node.type === "ExpressionStatement" && node.expression.type === "AssignmentExpression" && CJS_EXPORT.test(sourceCode.getText(node.expression.left))
  },
  "cjs-import": {
    test: (node, sourceCode) => node.type === "VariableDeclaration" && node.declarations.length > 0 && Boolean(node.declarations[0].init) && CJS_IMPORT.test(sourceCode.getText(node.declarations[0].init))
  },
  "directive": {
    test: utils.isDirective
  },
  "expression": {
    test: (node) => node.type === "ExpressionStatement" && !utils.isDirective(node)
  },
  "iife": {
    test: isIIFEStatement
  },
  "multiline-block-like": {
    test: (node, sourceCode) => node.loc.start.line !== node.loc.end.line && isBlockLikeStatement(sourceCode, node)
  },
  "multiline-expression": {
    test: (node) => node.loc.start.line !== node.loc.end.line && node.type === "ExpressionStatement" && !utils.isDirective(node)
  },
  "multiline-const": newMultilineKeywordTester("const"),
  "multiline-let": newMultilineKeywordTester("let"),
  "multiline-var": newMultilineKeywordTester("var"),
  "singleline-const": newSinglelineKeywordTester("const"),
  "singleline-let": newSinglelineKeywordTester("let"),
  "singleline-var": newSinglelineKeywordTester("var"),
  "block": newNodeTypeTester("BlockStatement"),
  "empty": newNodeTypeTester("EmptyStatement"),
  "function": newNodeTypeTester("FunctionDeclaration"),
  "break": newKeywordTester("break"),
  "case": newKeywordTester("case"),
  "class": newKeywordTester("class"),
  "const": newKeywordTester("const"),
  "continue": newKeywordTester("continue"),
  "debugger": newKeywordTester("debugger"),
  "default": newKeywordTester("default"),
  "do": newKeywordTester("do"),
  "export": newKeywordTester("export"),
  "for": newKeywordTester("for"),
  "if": newKeywordTester("if"),
  "import": newKeywordTester("import"),
  "let": newKeywordTester("let"),
  "return": newKeywordTester("return"),
  "switch": newKeywordTester("switch"),
  "throw": newKeywordTester("throw"),
  "try": newKeywordTester("try"),
  "var": newKeywordTester("var"),
  "while": newKeywordTester("while"),
  "with": newKeywordTester("with")
};
var paddingLineBetweenStatements = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require or disallow padding lines between statements",
      url: "https://eslint.style/rules/js/padding-line-between-statements"
    },
    fixable: "whitespace",
    schema: {
      definitions: {
        paddingType: {
          type: "string",
          enum: Object.keys(PaddingTypes)
        },
        statementType: {
          anyOf: [
            { type: "string", enum: Object.keys(StatementTypes) },
            {
              type: "array",
              items: { type: "string", enum: Object.keys(StatementTypes) },
              minItems: 1,
              uniqueItems: true
            }
          ]
        }
      },
      type: "array",
      items: {
        type: "object",
        properties: {
          blankLine: { $ref: "#/definitions/paddingType" },
          prev: { $ref: "#/definitions/statementType" },
          next: { $ref: "#/definitions/statementType" }
        },
        additionalProperties: false,
        required: ["blankLine", "prev", "next"]
      }
    },
    messages: {
      unexpectedBlankLine: "Unexpected blank line before this statement.",
      expectedBlankLine: "Expected blank line before this statement."
    }
  },
  create(context) {
    const sourceCode = context.sourceCode;
    const configureList = context.options || [];
    let scopeInfo = null;
    function enterScope() {
      scopeInfo = {
        upper: scopeInfo,
        prevNode: null
      };
    }
    function exitScope() {
      scopeInfo = scopeInfo?.upper;
    }
    function match(node, type) {
      let innerStatementNode = node;
      while (innerStatementNode.type === "LabeledStatement")
        innerStatementNode = innerStatementNode.body;
      if (Array.isArray(type))
        return type.some(match.bind(null, innerStatementNode));
      return StatementTypes[type].test(innerStatementNode, sourceCode);
    }
    function getPaddingType(prevNode, nextNode) {
      for (let i = configureList.length - 1; i >= 0; --i) {
        const configure = configureList[i];
        const matched = match(prevNode, configure.prev) && match(nextNode, configure.next);
        if (matched)
          return PaddingTypes[configure.blankLine];
      }
      return PaddingTypes.any;
    }
    function getPaddingLineSequences(prevNode, nextNode) {
      const pairs = [];
      let prevToken = getActualLastToken(sourceCode, prevNode);
      if (nextNode.loc.start.line - prevToken.loc.end.line >= 2) {
        do {
          const token = sourceCode.getTokenAfter(
            prevToken,
            { includeComments: true }
          );
          if (token.loc.start.line - prevToken.loc.end.line >= 2)
            pairs.push([prevToken, token]);
          prevToken = token;
        } while (prevToken.range[0] < nextNode.range[0]);
      }
      return pairs;
    }
    function verify(node) {
      const parentType = node.parent.type;
      const validParent = utils.STATEMENT_LIST_PARENTS.has(parentType) || parentType === "SwitchStatement";
      if (!validParent)
        return;
      const prevNode = scopeInfo?.prevNode;
      if (prevNode) {
        const type = getPaddingType(prevNode, node);
        const paddingLines = getPaddingLineSequences(prevNode, node);
        type.verify(context, prevNode, node, paddingLines);
      }
      scopeInfo.prevNode = node;
    }
    function verifyThenEnterScope(node) {
      verify(node);
      enterScope();
    }
    return {
      "Program": enterScope,
      "BlockStatement": enterScope,
      "SwitchStatement": enterScope,
      "StaticBlock": enterScope,
      "Program:exit": exitScope,
      "BlockStatement:exit": exitScope,
      "SwitchStatement:exit": exitScope,
      "StaticBlock:exit": exitScope,
      ":statement": verify,
      "SwitchCase": verifyThenEnterScope,
      "SwitchCase:exit": exitScope
    };
  }
});

exports.paddingLineBetweenStatements = paddingLineBetweenStatements;
