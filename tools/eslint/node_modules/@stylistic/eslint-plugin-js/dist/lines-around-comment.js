'use strict';

var utils = require('./utils.js');

function getEmptyLineNums(lines) {
  const emptyLines = lines.map((line, i) => ({
    code: line.trim(),
    num: i + 1
  })).filter((line) => !line.code).map((line) => line.num);
  return emptyLines;
}
function getCommentLineNums(comments) {
  const lines = [];
  comments.forEach((token) => {
    const start = token.loc.start.line;
    const end = token.loc.end.line;
    lines.push(start, end);
  });
  return lines;
}
var linesAroundComment = utils.createRule({
  meta: {
    type: "layout",
    docs: {
      description: "Require empty lines around comments",
      url: "https://eslint.style/rules/js/lines-around-comment"
    },
    fixable: "whitespace",
    schema: [
      {
        type: "object",
        properties: {
          beforeBlockComment: {
            type: "boolean",
            default: true
          },
          afterBlockComment: {
            type: "boolean",
            default: false
          },
          beforeLineComment: {
            type: "boolean",
            default: false
          },
          afterLineComment: {
            type: "boolean",
            default: false
          },
          allowBlockStart: {
            type: "boolean",
            default: false
          },
          allowBlockEnd: {
            type: "boolean",
            default: false
          },
          allowClassStart: {
            type: "boolean"
          },
          allowClassEnd: {
            type: "boolean"
          },
          allowObjectStart: {
            type: "boolean"
          },
          allowObjectEnd: {
            type: "boolean"
          },
          allowArrayStart: {
            type: "boolean"
          },
          allowArrayEnd: {
            type: "boolean"
          },
          ignorePattern: {
            type: "string"
          },
          applyDefaultIgnorePatterns: {
            type: "boolean"
          },
          afterHashbangComment: {
            type: "boolean",
            default: false
          }
        },
        additionalProperties: false
      }
    ],
    messages: {
      after: "Expected line after comment.",
      before: "Expected line before comment."
    }
  },
  create(context) {
    const options = Object.assign({}, context.options[0]);
    const ignorePattern = options.ignorePattern;
    const defaultIgnoreRegExp = utils.COMMENTS_IGNORE_PATTERN;
    const customIgnoreRegExp = ignorePattern && new RegExp(ignorePattern, "u");
    const applyDefaultIgnorePatterns = options.applyDefaultIgnorePatterns !== false;
    options.beforeBlockComment = typeof options.beforeBlockComment !== "undefined" ? options.beforeBlockComment : true;
    const sourceCode = context.sourceCode;
    const lines = sourceCode.lines;
    const numLines = lines.length + 1;
    const comments = sourceCode.getAllComments();
    const commentLines = getCommentLineNums(comments);
    const emptyLines = getEmptyLineNums(lines);
    const commentAndEmptyLines = new Set(commentLines.concat(emptyLines));
    function codeAroundComment(token) {
      let currentToken = token;
      do
        currentToken = sourceCode.getTokenBefore(currentToken, { includeComments: true });
      while (currentToken && utils.isCommentToken(currentToken));
      if (currentToken && utils.isTokenOnSameLine(currentToken, token))
        return true;
      currentToken = token;
      do
        currentToken = sourceCode.getTokenAfter(currentToken, { includeComments: true });
      while (currentToken && utils.isCommentToken(currentToken));
      if (currentToken && utils.isTokenOnSameLine(token, currentToken))
        return true;
      return false;
    }
    function isParentNodeType(parent, nodeType) {
      return parent.type === nodeType;
    }
    function getParentNodeOfToken(token) {
      const node = sourceCode.getNodeByRangeIndex(token.range[0]);
      if (node && node.type === "StaticBlock") {
        const openingBrace = sourceCode.getFirstToken(node, { skip: 1 });
        return openingBrace && token.range[0] >= openingBrace.range[0] ? node : null;
      }
      return node;
    }
    function isCommentAtParentStart(token, nodeType) {
      const parent = getParentNodeOfToken(token);
      if (parent && isParentNodeType(parent, nodeType)) {
        let parentStartNodeOrToken = parent;
        if (parent.type === "StaticBlock") {
          parentStartNodeOrToken = sourceCode.getFirstToken(parent, { skip: 1 });
        } else if (parent.type === "SwitchStatement") {
          parentStartNodeOrToken = sourceCode.getTokenAfter(parent.discriminant, {
            filter: utils.isOpeningBraceToken
          });
        }
        return !!parentStartNodeOrToken && token.loc.start.line - parentStartNodeOrToken.loc.start.line === 1;
      }
      return false;
    }
    function isCommentAtParentEnd(token, nodeType) {
      const parent = getParentNodeOfToken(token);
      return !!parent && isParentNodeType(parent, nodeType) && parent.loc.end.line - token.loc.end.line === 1;
    }
    function isCommentAtBlockStart(token) {
      return isCommentAtParentStart(token, "ClassBody") || isCommentAtParentStart(token, "BlockStatement") || isCommentAtParentStart(token, "StaticBlock") || isCommentAtParentStart(token, "SwitchCase") || isCommentAtParentStart(token, "SwitchStatement");
    }
    function isCommentAtBlockEnd(token) {
      return isCommentAtParentEnd(token, "ClassBody") || isCommentAtParentEnd(token, "BlockStatement") || isCommentAtParentEnd(token, "StaticBlock") || isCommentAtParentEnd(token, "SwitchCase") || isCommentAtParentEnd(token, "SwitchStatement");
    }
    function isCommentAtClassStart(token) {
      return isCommentAtParentStart(token, "ClassBody");
    }
    function isCommentAtClassEnd(token) {
      return isCommentAtParentEnd(token, "ClassBody");
    }
    function isCommentAtObjectStart(token) {
      return isCommentAtParentStart(token, "ObjectExpression") || isCommentAtParentStart(token, "ObjectPattern");
    }
    function isCommentAtObjectEnd(token) {
      return isCommentAtParentEnd(token, "ObjectExpression") || isCommentAtParentEnd(token, "ObjectPattern");
    }
    function isCommentAtArrayStart(token) {
      return isCommentAtParentStart(token, "ArrayExpression") || isCommentAtParentStart(token, "ArrayPattern");
    }
    function isCommentAtArrayEnd(token) {
      return isCommentAtParentEnd(token, "ArrayExpression") || isCommentAtParentEnd(token, "ArrayPattern");
    }
    function checkForEmptyLine(token, opts) {
      if (applyDefaultIgnorePatterns && defaultIgnoreRegExp.test(token.value))
        return;
      if (customIgnoreRegExp && customIgnoreRegExp.test(token.value))
        return;
      let after = opts.after;
      let before = opts.before;
      const prevLineNum = token.loc.start.line - 1;
      const nextLineNum = token.loc.end.line + 1;
      const commentIsNotAlone = codeAroundComment(token);
      const blockStartAllowed = options.allowBlockStart && isCommentAtBlockStart(token) && !(options.allowClassStart === false && isCommentAtClassStart(token));
      const blockEndAllowed = options.allowBlockEnd && isCommentAtBlockEnd(token) && !(options.allowClassEnd === false && isCommentAtClassEnd(token));
      const classStartAllowed = options.allowClassStart && isCommentAtClassStart(token);
      const classEndAllowed = options.allowClassEnd && isCommentAtClassEnd(token);
      const objectStartAllowed = options.allowObjectStart && isCommentAtObjectStart(token);
      const objectEndAllowed = options.allowObjectEnd && isCommentAtObjectEnd(token);
      const arrayStartAllowed = options.allowArrayStart && isCommentAtArrayStart(token);
      const arrayEndAllowed = options.allowArrayEnd && isCommentAtArrayEnd(token);
      const exceptionStartAllowed = blockStartAllowed || classStartAllowed || objectStartAllowed || arrayStartAllowed;
      const exceptionEndAllowed = blockEndAllowed || classEndAllowed || objectEndAllowed || arrayEndAllowed;
      if (prevLineNum < 1)
        before = false;
      if (nextLineNum >= numLines)
        after = false;
      if (commentIsNotAlone)
        return;
      const previousTokenOrComment = sourceCode.getTokenBefore(token, { includeComments: true });
      const nextTokenOrComment = sourceCode.getTokenAfter(token, { includeComments: true });
      if (!exceptionStartAllowed && before && !commentAndEmptyLines.has(prevLineNum) && !(utils.isCommentToken(previousTokenOrComment) && utils.isTokenOnSameLine(previousTokenOrComment, token))) {
        const lineStart = token.range[0] - token.loc.start.column;
        const range = [lineStart, lineStart];
        context.report({
          node: token,
          messageId: "before",
          fix(fixer) {
            return fixer.insertTextBeforeRange(range, "\n");
          }
        });
      }
      if (!exceptionEndAllowed && after && !commentAndEmptyLines.has(nextLineNum) && !(utils.isCommentToken(nextTokenOrComment) && utils.isTokenOnSameLine(token, nextTokenOrComment))) {
        context.report({
          node: token,
          messageId: "after",
          fix(fixer) {
            return fixer.insertTextAfter(token, "\n");
          }
        });
      }
    }
    return {
      Program() {
        comments.forEach((token) => {
          if (token.type === "Line") {
            if (options.beforeLineComment || options.afterLineComment) {
              checkForEmptyLine(token, {
                after: options.afterLineComment,
                before: options.beforeLineComment
              });
            }
          } else if (token.type === "Block") {
            if (options.beforeBlockComment || options.afterBlockComment) {
              checkForEmptyLine(token, {
                after: options.afterBlockComment,
                before: options.beforeBlockComment
              });
            }
          } else if (token.type === "Shebang") {
            if (options.afterHashbangComment) {
              checkForEmptyLine(token, {
                after: options.afterHashbangComment,
                before: false
              });
            }
          }
        });
      }
    };
  }
});

exports.linesAroundComment = linesAroundComment;
